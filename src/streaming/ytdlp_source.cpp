#include "streaming/ytdlp_source.hpp"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace melodeus::streaming {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    if (w == nullptr) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string quote(const std::filesystem::path& p) {
    return "\"" + p.string() + "\"";
}

struct Pipe {
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;

    bool create(bool rd_inherit = false) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        if (!CreatePipe(&rd, &wr, &sa, 0)) {
            return false;
        }
        if (!rd_inherit) {
            SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        }
        return true;
    }
    void close_rd() {
        if (rd != nullptr) {
            CloseHandle(rd);
            rd = nullptr;
        }
    }
    void close_wr() {
        if (wr != nullptr) {
            CloseHandle(wr);
            wr = nullptr;
        }
    }
};

struct Child {
    PROCESS_INFORMATION pi{};
    bool started = false;
};

// Herencia selectiva (PROC_THREAD_ATTRIBUTE_HANDLE_LIST) + job object:
// mismo patrón que tools/stream_probe.cpp — sin esto el hijo retiene pipes
// y nunca ve el EOF el resto.
Child spawn(std::wstring& cmd_line, HANDLE stdin_h, HANDLE stdout_h, HANDLE job) {
    Child c;
    std::vector<HANDLE> inherit;
    if (stdin_h != nullptr && stdin_h != INVALID_HANDLE_VALUE) {
        inherit.push_back(stdin_h);
    }
    if (stdout_h != nullptr && stdout_h != INVALID_HANDLE_VALUE) {
        inherit.push_back(stdout_h);
    }

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;
    siex.StartupInfo.hStdInput = (stdin_h != nullptr) ? stdin_h : INVALID_HANDLE_VALUE;
    siex.StartupInfo.hStdOutput = (stdout_h != nullptr) ? stdout_h : INVALID_HANDLE_VALUE;
    siex.StartupInfo.hStdError = INVALID_HANDLE_VALUE;

    std::vector<char> attr_buf;
    LPPROC_THREAD_ATTRIBUTE_LIST attr = nullptr;
    if (!inherit.empty()) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attr_buf.resize(size);
        attr = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        if (!InitializeProcThreadAttributeList(attr, 1, 0, &size) ||
            !UpdateProcThreadAttribute(attr, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
                                       inherit.size() * sizeof(HANDLE), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(attr);
            attr = nullptr;
        }
    }
    if (attr != nullptr) {
        siex.lpAttributeList = attr;
    }

    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (attr != nullptr) {
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    const BOOL ok = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, flags, nullptr,
                                   nullptr, &siex.StartupInfo, &c.pi);
    if (attr != nullptr) {
        DeleteProcThreadAttributeList(attr);
    }
    if (!ok) {
        return c;
    }
    if (job != nullptr) {
        AssignProcessToJobObject(job, c.pi.hProcess);
    }
    ResumeThread(c.pi.hThread);
    c.started = true;
    return c;
}

void close_child(Child& c) {
    if (c.started) {
        CloseHandle(c.pi.hThread);
        CloseHandle(c.pi.hProcess);
        c.started = false;
    }
}

/// Ejecuta `cmd` capturando stdout con tope de tiempo y bytes. El proceso
/// se mata al agotar el tope.
bool run_capture(const std::string& cmd, DWORD timeout_ms, std::string* out, DWORD* exit_code) {
    Pipe pout;
    if (!pout.create(false)) {
        return false;
    }
    std::wstring wcmd = widen(cmd);
    Child c = spawn(wcmd, nullptr, pout.wr, nullptr);
    pout.close_wr();
    if (!c.started) {
        pout.close_rd();
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::string result;
    std::vector<char> buf(4096);
    for (;;) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
        if (elapsed > timeout_ms) {
            TerminateProcess(c.pi.hProcess, 99);
            break;
        }
        // Peek para no bloquear indefinididamente sin datos.
        DWORD avail = 0;
        if (!PeekNamedPipe(pout.rd, nullptr, 0, nullptr, &avail, nullptr)) {
            break;
        }
        if (avail == 0) {
            if (WaitForSingleObject(c.pi.hProcess, 20) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }
        DWORD got = 0;
        if (!ReadFile(pout.rd, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) ||
            got == 0) {
            break;
        }
        result.append(buf.data(), got);
        if (result.size() > (1u << 16)) {
            break;
        }
    }
    WaitForSingleObject(c.pi.hProcess, 5000);
    DWORD code = 99;
    GetExitCodeProcess(c.pi.hProcess, &code);
    close_child(c);
    pout.close_rd();
    if (exit_code != nullptr) {
        *exit_code = code;
    }
    if (out != nullptr) {
        // Primera línea no vacía.
        std::size_t start = 0;
        while (start < result.size()) {
            std::size_t end = result.find_first_of("\r\n", start);
            if (end == std::string::npos) {
                end = result.size();
            }
            if (end > start) {
                *out = result.substr(start, end - start);
                return true;
            }
            start = end + 1;
        }
    }
    return false;
}

bool is_http_url(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string strip_file_scheme(const std::string& url) {
    if (url.rfind("file://", 0) == 0) {
        return url.substr(7);
    }
    return url;
}

} // namespace

YtDlpSource::YtDlpSource(ToolPaths tools, int prefill_frames)
    : tools_(std::move(tools)), jb_(prefill_frames, kChannels),
      title_(tools_.ytdlp.empty() ? std::string() : std::string()), error_() {
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    job_ = job;
}

YtDlpSource::~YtDlpSource() {
    stop();
    if (job_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
    }
}

bool YtDlpSource::open(const char* url) {
    if (url == nullptr || url[0] == '\0') {
        return false;
    }
    const StreamState st = state_.load(std::memory_order_acquire);
    if (st == StreamState::Connecting || st == StreamState::Buffering ||
        st == StreamState::Playing) {
        return false;
    }
    stop();
    stopping_.store(false, std::memory_order_release);
    jb_.reset();
    pushed_frames_.store(0, std::memory_order_relaxed);
    duration_frames_.store(-1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(text_mutex_);
        title_.clear();
        error_.clear();
    }
    state_.store(StreamState::Connecting, std::memory_order_release);
    const std::string u(url);
    thread_ = std::thread([this, u] { worker(u); });
    return true;
}

void YtDlpSource::stop() {
    stopping_.store(true, std::memory_order_release);
    if (job_ != nullptr) {
        // Mata yt-dlp y ffmpeg de la sesión actual (si los hay).
        ::TerminateJobObject(static_cast<HANDLE>(job_), 97);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    jb_.reset();
    state_.store(StreamState::Idle, std::memory_order_release);
}

int YtDlpSource::read(float* out, int frames) {
    return jb_.read(out, frames);
}

bool YtDlpSource::query_title(const std::string& url, std::string* out, int timeout_ms) {
    if (tools_.ytdlp.empty() || out == nullptr) {
        return false;
    }
    const std::string cmd =
        quote(tools_.ytdlp) + " --no-playlist --skip-download --print title " + "\"" + url + "\"";
    DWORD code = 99;
    std::string title;
    if (!run_capture(cmd, static_cast<DWORD>(timeout_ms), &title, &code) || code != 0 ||
        title.empty()) {
        return false;
    }
    *out = title;
    return true;
}

bool YtDlpSource::pull_all(std::vector<float>* out, int timeout_ms) {
    if (out == nullptr || !thread_.joinable()) {
        return false;
    }
    out->clear();
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<float> chunk(4096 * kChannels);
    for (;;) {
        const StreamState st = state_.load(std::memory_order_acquire);
        if (st == StreamState::Ended) {
            break;
        }
        if (st == StreamState::Error || st == StreamState::Idle) {
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
        if (elapsed > timeout_ms) {
            stop();
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "pull_all timeout";
            return false;
        }
        const int got = jb_.read(chunk.data(), 4096);
        if (got > 0) {
            out->insert(out->end(), chunk.begin(),
                        chunk.begin() + static_cast<std::ptrdiff_t>(got) * kChannels);
        } else {
            ::Sleep(5);
        }
    }
    // Drena lo último del ring.
    for (;;) {
        const int got = jb_.read(chunk.data(), 4096);
        if (got <= 0) {
            break;
        }
        out->insert(out->end(), chunk.begin(),
                    chunk.begin() + static_cast<std::ptrdiff_t>(got) * kChannels);
    }
    return true;
}

std::int64_t YtDlpSource::run_session(const std::string& url, bool http, bool* died_early) {
    *died_early = false;
    const std::string url_q = "\"" + url + "\"";

    Pipe p12;
    Pipe p2out;
    Child c1;
    Child c2;

    std::wstring wcmd_ffmpeg;
    std::wstring wcmd_ytdlp;
    const std::string ffmpeg_base =
        quote(tools_.ffmpeg) + " -hide_banner -loglevel error -nostdin ";

    if (http) {
        if (tools_.ytdlp.empty() || tools_.ffmpeg.empty()) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "yt-dlp/ffmpeg no disponibles";
            return 0;
        }
        if (!p12.create(true) || !p2out.create(false)) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "CreatePipe fallo";
            return 0;
        }
        const std::string cmd2 =
            ffmpeg_base + "-i pipe:0 -f f32le -acodec pcm_f32le -ac 2 -ar 48000 pipe:1";
        const std::string cmd1 =
            quote(tools_.ytdlp) + " -f bestaudio -o - --no-part --no-playlist --quiet " + url_q;
        wcmd_ffmpeg = widen(cmd2);
        wcmd_ytdlp = widen(cmd1);
        c2 = spawn(wcmd_ffmpeg, p12.rd, p2out.wr, static_cast<HANDLE>(job_));
        if (!c2.started) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "CreateProcess fallo (ffmpeg)";
            return 0;
        }
        c1 = spawn(wcmd_ytdlp, nullptr, p12.wr, static_cast<HANDLE>(job_));
        if (!c1.started) {
            TerminateProcess(c2.pi.hProcess, 98);
            close_child(c2);
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "CreateProcess fallo (yt-dlp)";
            return 0;
        }
        p12.close_wr();
        p12.close_rd();
    } else {
        // Fichero local: sólo ffmpeg (tests offline y archivos descargados).
        if (tools_.ffmpeg.empty()) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "ffmpeg no disponible";
            return 0;
        }
        if (!p2out.create(false)) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "CreatePipe fallo";
            return 0;
        }
        const std::string cmd =
            ffmpeg_base + "-i " + url_q + " -f f32le -acodec pcm_f32le -ac 2 -ar 48000 pipe:1";
        wcmd_ffmpeg = widen(cmd);
        c1 = spawn(wcmd_ffmpeg, nullptr, p2out.wr, static_cast<HANDLE>(job_));
        if (!c1.started) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            error_ = "CreateProcess fallo (ffmpeg)";
            return 0;
        }
    }
    p2out.close_wr();

    std::int64_t pushed = 0;
    std::vector<char> buffer(1u << 16);
    for (;;) {
        if (stopping_.load(std::memory_order_acquire)) {
            break;
        }
        DWORD got = 0;
        if (!ReadFile(p2out.rd, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) ||
            got == 0) {
            break;
        }
        const auto* floats = reinterpret_cast<const float*>(buffer.data());
        const int frames = static_cast<int>(got / (sizeof(float) * kChannels));
        // Empuja en trozos pequeños para no desbordar el ring y ceder CPU.
        int offset = 0;
        while (offset < frames && !stopping_.load(std::memory_order_acquire)) {
            const int n =
                jb_.push(floats + static_cast<std::size_t>(offset) * kChannels, frames - offset);
            offset += n;
            pushed += n;
            pushed_frames_.store(pushed, std::memory_order_relaxed);
            if (offset < frames) {
                ::Sleep(2); // ring lleno: el consumidor va más lento
            }
        }
        if (state_.load(std::memory_order_relaxed) == StreamState::Connecting) {
            state_.store(StreamState::Buffering, std::memory_order_release);
        }
    }
    p2out.close_rd();

    if (c1.started) {
        if (stopping_.load(std::memory_order_acquire)) {
            TerminateProcess(c1.pi.hProcess, 97);
        }
        WaitForSingleObject(c1.pi.hProcess, 15000);
        close_child(c1);
    }
    if (c2.started) {
        if (stopping_.load(std::memory_order_acquire)) {
            TerminateProcess(c2.pi.hProcess, 97);
        }
        WaitForSingleObject(c2.pi.hProcess, 15000);
        close_child(c2);
    }

    // Muerte dudosa: se esperaba más duración de la que llegó.
    const std::int64_t expected = duration_frames_.load(std::memory_order_relaxed);
    if (!stopping_.load(std::memory_order_acquire) && expected > 0 && pushed < expected * 8 / 10) {
        *died_early = true;
    }
    return pushed;
}

void YtDlpSource::worker(const std::string url) {
    const bool http = is_http_url(url);

    if (http) {
        // Título y duración (best effort; la URL sirve de fallback).
        std::string title;
        if (query_title(url, &title, 30000)) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            title_ = title;
        } else {
            std::lock_guard<std::mutex> lock(text_mutex_);
            title_ = url;
        }
        std::string dur;
        const std::string cmd = quote(tools_.ytdlp) +
                                " --no-playlist --skip-download --print duration " + "\"" + url +
                                "\"";
        DWORD code = 99;
        if (run_capture(cmd, 30000, &dur, &code) && code == 0 && !dur.empty()) {
            const double seconds = std::strtod(dur.c_str(), nullptr);
            if (seconds > 1.0) {
                duration_frames_.store(static_cast<std::int64_t>(seconds * kSampleRate),
                                       std::memory_order_relaxed);
            }
        }
    } else {
        std::lock_guard<std::mutex> lock(text_mutex_);
        title_ = strip_file_scheme(url);
    }

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        if (stopping_.load(std::memory_order_acquire)) {
            jb_.finish();
            state_.store(StreamState::Idle, std::memory_order_release);
            return;
        }
        if (attempt > 1) {
            ::Sleep(400); // backoff entre reintentos
            jb_.reset();
            pushed_frames_.store(0, std::memory_order_relaxed);
        }
        bool died_early = false;
        const std::int64_t pushed = run_session(url, http, &died_early);
        if (stopping_.load(std::memory_order_acquire)) {
            jb_.finish();
            state_.store(StreamState::Idle, std::memory_order_release);
            return;
        }
        if (!died_early || !http) {
            // Fin normal (o fichero local corto): drena y termina.
            jb_.finish();
            state_.store(pushed > 0 ? StreamState::Ended : StreamState::Error,
                         std::memory_order_release);
            if (pushed <= 0) {
                std::lock_guard<std::mutex> lock(text_mutex_);
                if (error_.empty()) {
                    error_ = "sin datos del stream";
                }
            }
            return;
        }
        // murió antes de tiempo → reintento automático (DoD F4).
        std::printf("[f4] stream murió antes de tiempo (intento %d/%d), reintentando...\n", attempt,
                    kMaxAttempts);
    }
    jb_.finish();
    {
        std::lock_guard<std::mutex> lock(text_mutex_);
        if (error_.empty()) {
            error_ = "reintentos agotados";
        }
    }
    state_.store(StreamState::Error, std::memory_order_release);
}

} // namespace melodeus::streaming
