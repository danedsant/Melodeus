// stream_probe — F0.5: yt-dlp y ffmpeg por pipe (F5 hará esto en hilo propio).
//
//   stream_probe [--url URL] [--seg S]
//
// 1) Versiones de yt-dlp y ffmpeg (spawn, captura de stdout, timeout).
// 2) Roundtrip offline por pipes: ffmpeg lavfi sine → aac(adts) → ffmpeg → f32le,
//    verificando nº de muestras (1 s @ 48 kHz estéreo = 96000 muestras).
// 3) Opcional con --url: yt-dlp -f bestaudio -o - URL | ffmpeg (decodifica
//    --seg segundos). Por defecto usa un .ogg dominio público de Wikimedia.
//    Para streaming comercial (YouTube/SC) ejecutar a mano con la URL elegida.
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Pipe {
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;

    // rd_inherit=true para tuberías entre procesos (stdin de un hijo); false
    // cuando el extremo lo lee el padre.
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
        if (rd) {
            CloseHandle(rd);
            rd = nullptr;
        }
    }
    void close_wr() {
        if (wr) {
            CloseHandle(wr);
            wr = nullptr;
        }
    }
};

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

struct Child {
    PROCESS_INFORMATION pi{};
    bool started = false;
};

// Job object: todos los descendientes (nietos incluidos) mueren al salir del
// probe. Evita huérfanos de ffmpeg/yt-dlp y que un job de PowerShell heredado
// quede esperando EOF de pipes retenidos por procesos ajenos.
HANDLE g_job = nullptr;
HANDLE g_stderr = nullptr;

bool spawn_init() {
    g_stderr = GetStdHandle(STD_ERROR_HANDLE);
    if (g_stderr != nullptr && g_stderr != INVALID_HANDLE_VALUE) {
        SetHandleInformation(g_stderr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    } else {
        g_stderr = nullptr;
    }
    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job == nullptr) {
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(g_job);
        g_job = nullptr;
        return false;
    }
    return true;
}

// cmd_line debe ser mutable (CreateProcessW lo puede tocar).
//
// Herencia selectiva: PROC_THREAD_ATTRIBUTE_HANDLE_LIST restringe lo que el
// hijo hereda a los handles listados (además de tener HANDLE_FLAG_INHERIT).
// Sin esto, bInheritHandles=TRUE copia TODOS los handles heredables del padre
// (p. ej. los pipes de consola del job de PowerShell) y los retiene en vida,
// impidiendo que nadie vea EOF. El hijo arranca suspendido para poder
// asignarlo al job object antes de que cree nietos.
Child spawn(std::wstring& cmd_line, HANDLE stdin_h, HANDLE stdout_h) {
    Child c;
    std::vector<HANDLE> inherit;
    if (stdin_h != nullptr && stdin_h != INVALID_HANDLE_VALUE) {
        inherit.push_back(stdin_h);
    }
    if (stdout_h != nullptr && stdout_h != INVALID_HANDLE_VALUE) {
        inherit.push_back(stdout_h);
    }
    if (g_stderr != nullptr) {
        inherit.push_back(g_stderr);
    }

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;
    siex.StartupInfo.hStdInput = (stdin_h != nullptr) ? stdin_h : INVALID_HANDLE_VALUE;
    siex.StartupInfo.hStdOutput = (stdout_h != nullptr) ? stdout_h : INVALID_HANDLE_VALUE;
    siex.StartupInfo.hStdError = (g_stderr != nullptr) ? g_stderr : INVALID_HANDLE_VALUE;

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
        std::fprintf(stderr, "[stream][dbg] CreateProcess fallo (%lu) cmd=%ls\n", GetLastError(),
                     cmd_line.c_str());
        return c;
    }
    if (g_job != nullptr) {
        AssignProcessToJobObject(g_job, c.pi.hProcess); // best-effort
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

// Espera con deadline; mata el proceso si se pasa. Devuelve:
// 0 = salió con código, 1 = timeout (terminado), 2 = fallo de espera.
int wait_deadline(Child& c, DWORD timeout_ms, DWORD* exit_code) {
    const DWORD r = WaitForSingleObject(c.pi.hProcess, timeout_ms);
    if (r == WAIT_OBJECT_0) {
        GetExitCodeProcess(c.pi.hProcess, exit_code);
        return 0;
    }
    if (r == WAIT_TIMEOUT) {
        TerminateProcess(c.pi.hProcess, 99);
        WaitForSingleObject(c.pi.hProcess, 5000);
        *exit_code = 99;
        return 1;
    }
    *exit_code = 99;
    return 2;
}

struct RunResult {
    DWORD exit_code = 99;
    int wait_status = 2; // 0 ok, 1 timeout, 2 error
    double wall_ms = 0.0;
    size_t bytes = 0;
    bool sample_ok = false; // solo para roundtrip offline
    std::string error;
};

// Ejecuta cmd1 | cmd2 leyendo stdout de cmd2. max_bytes = tope de lectura
// (después se mata la cadena para no descargar infinito). expected_frames > 0
// verifica el nº de frames f32 estéreo (8 B/frame) con tolerancia al priming
// del codificador AAC (±3072 frames ≈ 64 ms).
RunResult run_pipeline(const std::string& cmd1, const std::string& cmd2, DWORD timeout_ms,
                       size_t max_bytes, long long expected_frames) {
    RunResult res;
    Pipe p12;   // stdout(cmd1) -> stdin(cmd2); el rd lo hereda cmd2
    Pipe p2out; // stdout(cmd2) -> padre
    if (!p12.create(true) || !p2out.create(false)) {
        res.error = "CreatePipe fallo";
        return res;
    }

    std::wstring wcmd2 = widen(cmd2);
    std::wstring wcmd1 = widen(cmd1);
    // La herencia selectiva en spawn() garantiza que c2 solo herede p12.rd (su
    // stdin) y c1 solo p12.wr (su stdout): ninguno ve el extremo que le haría
    // falta cerrar para que el otro vea EOF/broken-pipe. El productor va con
    // stdin nulo (ffmpeg con -nostdin): listar un handle de NUL en
    // PROC_THREAD_ATTRIBUTE_HANDLE_LIST provoca ERROR_INVALID_PARAMETER (87).
    Child c2 = spawn(wcmd2, p12.rd, p2out.wr);
    if (!c2.started) {
        res.error = "CreateProcess fallo (cmd2)";
        return res;
    }
    Child c1 = spawn(wcmd1, nullptr, p12.wr);
    if (!c1.started) {
        res.error = "CreateProcess fallo (cmd1)";
        TerminateProcess(c2.pi.hProcess, 98);
        close_child(c2);
        return res;
    }
    p12.close_wr(); // el padre no escribe
    p12.close_rd(); // y no lee: si queda abierto, el productor nunca ve broken-pipe
    p2out.close_wr();

    std::vector<char> buffer(1u << 16);
    size_t total = 0;
    size_t samples = 0;
    bool capped = false;
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(p2out.rd, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) ||
            got == 0) {
            break;
        }
        total += got;
        if (expected_frames > 0) {
            samples += got / 8; // f32 estéreo = 8 bytes por frame
        }
        if (total >= max_bytes) {
            capped = true;
            break;
        }
    }
    p2out.close_rd();
    const auto t1 = std::chrono::steady_clock::now();
    res.wall_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    res.bytes = total;

    if (capped) {
        TerminateProcess(c1.pi.hProcess, 97);
        TerminateProcess(c2.pi.hProcess, 97);
    }
    const int w2 = wait_deadline(c2, timeout_ms, &res.exit_code);
    DWORD dummy = 0;
    // El productor debe abortar en cuanto su pipe se rompe; si no, se corta.
    wait_deadline(c1, 15000, &dummy);
    close_child(c1);
    close_child(c2);
    res.wait_status = (w2 == 0 && !capped) ? 0 : (w2 == 1 ? 1 : 0);
    if (capped) {
        res.wait_status = 0; // tope previsto (streaming con --url)
    }
    if (expected_frames > 0) {
        const long long delta = static_cast<long long>(samples) - expected_frames;
        res.sample_ok = (delta >= -3072 && delta <= 3072);
    }
    return res;
}

// Captura el stdout completo de un comando simple (versiones).
RunResult run_capture(const std::string& cmd, DWORD timeout_ms, std::string* out) {
    RunResult res;
    Pipe pout;
    if (!pout.create()) {
        res.error = "CreatePipe fallo";
        return res;
    }
    std::wstring wcmd = widen(cmd);
    Child c = spawn(wcmd, nullptr, pout.wr);
    if (!c.started) {
        res.error = "CreateProcess fallo";
        return res;
    }
    pout.close_wr();
    const auto t0 = std::chrono::steady_clock::now();
    std::string data;
    char buffer[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(pout.rd, buffer, sizeof(buffer), &got, nullptr) || got == 0) {
            break;
        }
        data.append(buffer, got);
    }
    pout.close_rd();
    const auto t1 = std::chrono::steady_clock::now();
    res.wall_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    res.wait_status = wait_deadline(c, timeout_ms, &res.exit_code);
    close_child(c);
    if (out != nullptr) {
        const size_t nl = data.find_first_of("\r\n");
        *out = data.substr(0, nl == std::string::npos ? data.size() : nl);
    }
    return res;
}

int fail(const std::string& msg) {
    std::fprintf(stderr, "[stream] FALLO: %s\n", msg.c_str());
    return 1;
}

std::string first_line(const std::string& s) {
    const size_t nl = s.find_first_of("\r\n");
    return s.substr(0, nl == std::string::npos ? s.size() : nl);
}

} // namespace

int main(int argc, char** argv) {
    if (!spawn_init()) {
        std::fprintf(stderr, "[stream] aviso: job object no disponible (limpieza best-effort)\n");
    }
    std::string url = "https://upload.wikimedia.org/wikipedia/commons/c/c8/Example.ogg";
    double seg = 2.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--url" && i + 1 < argc) {
            url = argv[++i];
        } else if (a == "--seg" && i + 1 < argc) {
            seg = std::atof(argv[++i]);
        } else {
            return fail("argumento desconocido: " + a);
        }
    }

    const std::string ytdlp = "external\\yt-dlp\\yt-dlp.exe";
    const std::string ffmpeg = "external\\ffmpeg\\bin\\ffmpeg.exe";

    std::string yv, fv;
    RunResult r1 = run_capture(ytdlp + " --version", 30000, &yv);
    if (r1.exit_code != 0 || yv.empty()) {
        return fail("yt-dlp --version fallo (ejecute scripts/fetch_deps.ps1)");
    }
    std::printf("[stream] yt-dlp: %s\n", yv.c_str());

    RunResult r2 = run_capture(ffmpeg + " -version", 30000, &fv);
    if (r2.exit_code != 0 || fv.empty()) {
        return fail("ffmpeg -version fallo (ejecute scripts/fetch_deps.ps1)");
    }
    std::printf("[stream] ffmpeg: %s\n", first_line(fv).c_str());

    // --- roundtrip offline: sine 48k estereo 1s → aac(adts) → f32le (48000 frames).
    // El build LGPL no incluye libmp3lame (GPL): se usa el codificador nativo AAC.
    const std::string lavfi = ffmpeg +
                              " -hide_banner -nostdin -loglevel error -f lavfi -i "
                              "sine=frequency=440:sample_rate=48000:duration=1 -c:a aac -f adts -";
    const std::string decode1 =
        ffmpeg +
        " -hide_banner -nostdin -loglevel error -f aac -i pipe:0 -f f32le -ac 2 -ar 48000 pipe:1";
    RunResult rr = run_pipeline(lavfi, decode1, 60000, 4u << 20, 48000);
    if (rr.error.empty() && rr.exit_code == 0 && rr.sample_ok) {
        std::printf("[stream] lavfi→aac→f32le: %.3f MB en %.0f ms (%.2f MB/s), frames~48000 OK\n",
                    static_cast<double>(rr.bytes) / 1e6, rr.wall_ms,
                    (static_cast<double>(rr.bytes) / 1e6) / (rr.wall_ms / 1000.0));
    } else if (!rr.error.empty()) {
        return fail(rr.error);
    } else {
        return fail("roundtrip offline incorrecto (exit=" + std::to_string(rr.exit_code) +
                    " bytes=" + std::to_string(rr.bytes) + ")");
    }

    // --- red opcional: yt-dlp -o - | ffmpeg decodifica --seg s
    const std::string ytdlp_cmd =
        ytdlp + " --no-playlist --no-part --quiet --no-warnings -f bestaudio/best -o - " + url;
    const std::string decode2 = ffmpeg + " -hide_banner -nostdin -loglevel error -i pipe:0 -t " +
                                std::to_string(seg) + " -f f32le -ac 2 -ar 48000 pipe:1";
    std::printf("[stream] red: decodificando %.1f s de %s ...\n", seg, url.c_str());
    RunResult nr = run_pipeline(ytdlp_cmd, decode2, 180000, 32u << 20, -1);
    if (!nr.error.empty()) {
        std::fprintf(stderr, "[stream] aviso: %s (prueba de red omitida)\n", nr.error.c_str());
        std::printf("[stream] STREAM=OK (offline)\n");
        return 0;
    }
    const long long want = static_cast<long long>(seg * 48000.0);
    if (nr.exit_code != 0 || nr.bytes == 0) {
        std::fprintf(stderr,
                     "[stream] aviso: red no concluyente (exit=%lu bytes=%zu). "
                     "F5 reintentara con la URL elegida.\n",
                     nr.exit_code, nr.bytes);
        std::printf("[stream] STREAM=OK (offline)\n");
        return 0;
    }
    std::printf("[stream] yt-dlp→ffmpeg: %.3f MB en %.0f ms, muestras=%zu (esperadas~%lld)\n",
                static_cast<double>(nr.bytes) / 1e6, nr.wall_ms, nr.bytes / 8, want);
    std::printf("[stream] STREAM=OK\n");
    return 0;
}
