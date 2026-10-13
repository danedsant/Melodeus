#include "streaming/stream_search.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

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

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

std::string quote(const std::filesystem::path& p) {
    return quote(p.string());
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

static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        pattern = "\"" + key + "\" :";
        pos = json.find(pattern);
        if (pos == std::string::npos) return "";
    }
    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // saltar comilla inicial
    std::string val;
    bool escape = false;
    while (pos < json.size()) {
        char c = json[pos++];
        if (escape) {
            if (c == '"') val += '"';
            else if (c == '\\') val += '\\';
            else if (c == '/') val += '/';
            else if (c == 'b') val += '\b';
            else if (c == 'f') val += '\f';
            else if (c == 'n') val += '\n';
            else if (c == 'r') val += '\r';
            else if (c == 't') val += '\t';
            else if (c == 'u') {
                if (pos + 4 <= json.size()) {
                    unsigned int cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = json[pos++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                    }
                    if (cp <= 0x7F) {
                        val += static_cast<char>(cp);
                    } else if (cp <= 0x7FF) {
                        val += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
                        val += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        val += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                        val += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        val += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                val += c;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            val += c;
        }
    }
    return val;
}

static double extract_json_number(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        pattern = "\"" + key + "\" :";
        pos = json.find(pattern);
        if (pos == std::string::npos) return 0.0;
    }
    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return 0.0;
    if (json[pos] == '"') {
        std::string s = extract_json_string(json, key);
        try { return std::stod(s); } catch (...) { return 0.0; }
    }
    std::size_t end_pos = pos;
    while (end_pos < json.size() && (std::isdigit(static_cast<unsigned char>(json[end_pos])) ||
                                     json[end_pos] == '.' || json[end_pos] == '-')) {
        end_pos++;
    }
    if (end_pos > pos) {
        try {
            return std::stod(json.substr(pos, end_pos - pos));
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

} // namespace

bool parse_yt_json_line(const std::string& line, OnlineTrackResult* out) {
    if (out == nullptr || line.empty()) {
        return false;
    }
    std::string title = extract_json_string(line, "title");
    if (title.empty()) {
        return false;
    }
    out->title = std::move(title);
    out->id = extract_json_string(line, "id");
    
    std::string uploader = extract_json_string(line, "uploader");
    if (uploader.empty()) {
        uploader = extract_json_string(line, "channel");
    }
    if (uploader.empty()) {
        uploader = extract_json_string(line, "artist");
    }
    out->uploader = std::move(uploader);
    out->duration = extract_json_number(line, "duration");

    std::string url = extract_json_string(line, "webpage_url");
    if (url.empty()) {
        url = extract_json_string(line, "url");
    }
    if (url.empty() && !out->id.empty()) {
        url = "https://www.youtube.com/watch?v=" + out->id;
    }
    out->url = std::move(url);
    out->already_downloaded = false;
    return true;
}

struct StreamSearchService::Impl {
    std::thread thread;
    std::atomic<bool> searching{false};
    std::atomic<bool> stopping{false};
    HANDLE job = nullptr;

    mutable std::mutex mutex;
    std::vector<OnlineTrackResult> results;
    std::string error;
    SearchPlatform platform = SearchPlatform::YouTube;
    std::string query;

    Impl() {
        HANDLE j = ::CreateJobObjectW(nullptr, nullptr);
        if (j != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ::SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        job = j;
    }

    ~Impl() {
        cancel();
        if (job != nullptr) {
            ::CloseHandle(job);
            job = nullptr;
        }
    }

    void cancel() {
        stopping.store(true, std::memory_order_release);
        if (job != nullptr) {
            ::TerminateJobObject(job, 96);
        }
        if (thread.joinable()) {
            thread.join();
        }
        searching.store(false, std::memory_order_release);
    }
};

StreamSearchService::StreamSearchService() : impl_(std::make_unique<Impl>()) {}
StreamSearchService::~StreamSearchService() = default;

void StreamSearchService::cancel() {
    impl_->cancel();
}

bool StreamSearchService::is_searching() const {
    return impl_->searching.load(std::memory_order_acquire);
}

std::vector<OnlineTrackResult> StreamSearchService::get_results() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->results;
}

std::string StreamSearchService::get_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

SearchPlatform StreamSearchService::current_platform() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->platform;
}

std::string StreamSearchService::current_query() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->query;
}

void StreamSearchService::update_downloaded_status(
    const std::function<bool(const std::string& id, const std::string& title)>& is_downloaded_fn) {
    if (!is_downloaded_fn) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& item : impl_->results) {
        item.already_downloaded = is_downloaded_fn(item.id, item.title);
    }
}

void StreamSearchService::start_search(const std::filesystem::path& ytdlp_bin,
                                       SearchPlatform platform,
                                       const std::string& query,
                                       int max_results) {
    cancel();
    if (query.empty() || ytdlp_bin.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->results.clear();
        impl_->error.clear();
        impl_->platform = platform;
        impl_->query = query;
    }

    impl_->stopping.store(false, std::memory_order_release);
    impl_->searching.store(true, std::memory_order_release);

    impl_->thread = std::thread([this, ytdlp_bin, platform, query, max_results] {
        Pipe pout;
        if (!pout.create(false)) {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->error = "no se pudo crear la tubería de proceso";
            impl_->searching.store(false, std::memory_order_release);
            return;
        }

        std::string search_flag = (platform == SearchPlatform::SoundCloud)
                                      ? ("scsearch" + std::to_string(max_results))
                                      : ("ytsearch" + std::to_string(max_results));

        std::string cmd = quote(ytdlp_bin) +
                          " --dump-json --flat-playlist --no-warnings --default-search " +
                          quote(search_flag) + " " + quote(query);

        std::wstring wcmd = widen(cmd);
        Child c = spawn(wcmd, nullptr, pout.wr, impl_->job);
        pout.close_wr();

        if (!c.started) {
            pout.close_rd();
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->error = "error al iniciar yt-dlp";
            impl_->searching.store(false, std::memory_order_release);
            return;
        }

        std::string output;
        std::vector<char> buf(8192);
        const auto t0 = std::chrono::steady_clock::now();

        for (;;) {
            if (impl_->stopping.load(std::memory_order_acquire)) {
                break;
            }
            DWORD avail = 0;
            if (!PeekNamedPipe(pout.rd, nullptr, 0, nullptr, &avail, nullptr)) {
                break;
            }
            if (avail == 0) {
                if (WaitForSingleObject(c.pi.hProcess, 30) == WAIT_OBJECT_0) {
                    if (!PeekNamedPipe(pout.rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
                        break;
                    }
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - t0)
                                         .count();
                if (elapsed > 20000) { // 20 segundos máximo
                    break;
                }
                continue;
            }

            DWORD to_read = (std::min)(avail, static_cast<DWORD>(buf.size()));
            DWORD got = 0;
            if (!ReadFile(pout.rd, buf.data(), to_read, &got, nullptr) || got == 0) {
                break;
            }
            output.append(buf.data(), got);
            if (output.size() > (1u << 20)) { // 1 MB límite
                break;
            }
        }

        pout.close_rd();
        WaitForSingleObject(c.pi.hProcess, 1000);
        close_child(c);

        if (impl_->stopping.load(std::memory_order_acquire)) {
            impl_->searching.store(false, std::memory_order_release);
            return;
        }

        std::vector<OnlineTrackResult> items;
        std::size_t start = 0;
        while (start < output.size()) {
            std::size_t end = output.find_first_of("\r\n", start);
            if (end == std::string::npos) {
                end = output.size();
            }
            if (end > start) {
                std::string line = output.substr(start, end - start);
                OnlineTrackResult res;
                if (parse_yt_json_line(line, &res)) {
                    res.platform = platform;
                    items.push_back(std::move(res));
                }
            }
            start = end + 1;
        }

        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (items.empty() && !output.empty()) {
                impl_->error = "sin coincidencias o formato desconocido";
            }
            impl_->results = std::move(items);
        }
        impl_->searching.store(false, std::memory_order_release);
    });
}

} // namespace melodeus::streaming
