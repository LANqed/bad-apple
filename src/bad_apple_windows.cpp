// Bad Apple native player for Windows (x64 and ARM32).
// Downloads ASCII frames and MP3 via curl.exe, plays MP3 through the MCI layer
// and renders frames locked to the audio clock, so video never drifts.
//
// winmm is loaded dynamically instead of linked, because the Windows SDK
// ARM32 (arm) import libraries are not always installed and winmm.lib would
// then break the link step.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const char* kFramesUrl =
    "https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1";
const char* kAudioUrl =
    "https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3";
const double kAudioSeconds = 219.09;

typedef UINT(WINAPI* MciSendStringAFn)(LPCSTR, LPSTR, UINT, HWND);

HANDLE g_out = nullptr;
CONSOLE_CURSOR_INFO g_cursor{};
std::string g_alias;
bool g_mci_open = false;
HMODULE g_winmm = nullptr;
MciSendStringAFn g_mci_send_string = nullptr;

bool init_mci() {
    g_winmm = LoadLibraryA("winmm.dll");
    if (!g_winmm) return false;
    g_mci_send_string =
        reinterpret_cast<MciSendStringAFn>(GetProcAddress(g_winmm, "mciSendStringA"));
    return g_mci_send_string != nullptr;
}

int mci(const std::string& command, char* buffer, int capacity) {
    if (!g_mci_send_string) return -1;
    if (buffer && capacity > 0) buffer[0] = '\0';
    return static_cast<int>(
        g_mci_send_string(command.c_str(), buffer, static_cast<UINT>(capacity), nullptr));
}

bool run_hidden(const std::string& command_line, HANDLE* process) {
    std::string mutable_command = command_line;
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessA(nullptr, &mutable_command[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    if (process) {
        *process = pi.hProcess;
        return true;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code == 0;
}

bool download(const std::string& url, const std::string& out) {
    // --ssl-no-revoke keeps schannel from failing when the revocation server
    // is unreachable, which is common on restricted networks.
    std::string cmd = "curl.exe -fL --silent --show-error --ssl-no-revoke --max-time 180 \"" +
                      url + "\" -o \"" + out + "\"";
    return run_hidden(cmd, nullptr);
}

std::string short_path(const std::string& path) {
    int len = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), 0);
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wide[0], len);
    std::wstring short_name(MAX_PATH, 0);
    DWORD written = GetShortPathNameW(wide.c_str(), &short_name[0],
                                      static_cast<DWORD>(short_name.size()));
    if (written == 0) return path;
    int out_len = WideCharToMultiByte(CP_ACP, 0, short_name.c_str(), -1,
                                      nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(out_len), 0);
    WideCharToMultiByte(CP_ACP, 0, short_name.c_str(), -1, &result[0], out_len,
                        nullptr, nullptr);
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

void write_frame(const std::string& text, size_t max_width, size_t max_rows) {
    std::ostringstream os;
    size_t start = 0;
    size_t rows = 0;
    while (start <= text.size() && rows < max_rows) {
        size_t nl = text.find('\n', start);
        std::string line = (nl == std::string::npos) ? text.substr(start)
                                                      : text.substr(start, nl - start);
        os << line;
        if (line.size() < max_width) os << std::string(max_width - line.size(), ' ');
        os << '\n';
        ++rows;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    while (rows < max_rows) {
        os << std::string(max_width, ' ') << '\n';
        ++rows;
    }
    std::string frame = os.str();
    COORD origin = {0, 0};
    SetConsoleCursorPosition(g_out, origin);
    DWORD written = 0;
    WriteConsoleA(g_out, frame.c_str(), static_cast<DWORD>(frame.size()), &written, nullptr);
}

void compute_dimensions(const std::vector<std::string>& frames,
                        size_t* max_width, size_t* max_rows) {
    size_t width = 0, rows = 0;
    for (const auto& text : frames) {
        size_t row = 1, col = 0, start = 0;
        while (true) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? text.size() : nl;
            col = std::max(col, end - start);
            if (nl == std::string::npos) break;
            start = nl + 1;
            ++row;
        }
        width = std::max(width, col);
        rows = std::max(rows, row);
    }
    *max_width = width;
    *max_rows = rows;
}
}  // namespace

int main() {
    char temp_dir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp_dir);
    std::string dir = std::string(temp_dir) + "bad-apple-" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string frames_path = dir + "\\frames.ps1";
    std::string audio_path = dir + "\\audio.mp3";

    printf("Downloading ASCII frames...\n");
    if (!download(kFramesUrl, frames_path)) {
        fprintf(stderr, "frame download failed\n");
        return 1;
    }
    printf("Downloading audio...\n");
    if (!download(kAudioUrl, audio_path)) {
        fprintf(stderr, "audio download failed\n");
        return 1;
    }

    std::ifstream in(frames_path);
    std::vector<std::string> frames;
    std::vector<std::string> buffer;
    bool capturing = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (capturing) {
            if (line == "\"@") {
                std::string frame;
                for (const auto& part : buffer) {
                    frame += part;
                    frame += '\n';
                }
                if (!frame.empty()) frame.pop_back();
                frames.push_back(frame);
                buffer.clear();
                capturing = false;
            } else {
                buffer.push_back(line);
            }
        } else if (line == "@\"") {
            capturing = true;
        }
    }
    if (frames.empty()) {
        fprintf(stderr, "no frames found\n");
        return 1;
    }

    size_t max_width = 0, max_rows = 0;
    compute_dimensions(frames, &max_width, &max_rows);

    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleCursorInfo(g_out, &g_cursor);
    CONSOLE_CURSOR_INFO hidden = {1, FALSE};
    SetConsoleCursorInfo(g_out, &hidden);

    char buffer512[512] = {};
    double fps = static_cast<double>(frames.size()) / kAudioSeconds;
    HANDLE audio_proc = nullptr;
    LARGE_INTEGER freq{}, t0{};
    bool use_mci = false;

    if (init_mci()) {
        std::string short_audio = short_path(audio_path);
        g_alias = "ba" + std::to_string(GetCurrentProcessId());
        std::string open_command = "open \"" + short_audio + "\" type mpegvideo alias " + g_alias;
        use_mci = mci(open_command, buffer512, sizeof(buffer512)) == 0;
    }

    if (use_mci) {
        g_mci_open = true;
        mci("play " + g_alias, buffer512, sizeof(buffer512));
        mci("status " + g_alias + " length", buffer512, sizeof(buffer512));
        long duration_ms = atol(buffer512);
        if (duration_ms > 0) {
            double computed = static_cast<double>(frames.size()) /
                              (static_cast<double>(duration_ms) / 1000.0);
            if (computed >= 1.0 && computed <= 60.0) fps = computed;
        }
        // Verify the device really advances; a silent MCI would freeze video.
        bool advanced = false;
        for (int i = 0; i < 10; ++i) {
            Sleep(150);
            mci("status " + g_alias + " position", buffer512, sizeof(buffer512));
            if (atol(buffer512) > 0) {
                advanced = true;
                break;
            }
            mci("status " + g_alias + " mode", buffer512, sizeof(buffer512));
            if (std::string(buffer512) == "stopped") break;
        }
        if (!advanced) {
            mci("stop " + g_alias, buffer512, sizeof(buffer512));
            mci("close " + g_alias, buffer512, sizeof(buffer512));
            g_mci_open = false;
            use_mci = false;
        }
    }

    if (!use_mci) {
        const char* candidates[] = {
            "mpv --no-video --really-quiet",
            "ffplay -nodisp -autoexit -loglevel quiet",
        };
        for (const char* candidate : candidates) {
            std::string cmd_line = std::string(candidate) + " \"" + audio_path + "\"";
            if (run_hidden(cmd_line, &audio_proc)) break;
        }
        if (!audio_proc) {
            fprintf(stderr, "MCI and mpv/ffplay unavailable\n");
            SetConsoleCursorInfo(g_out, &g_cursor);
            return 1;
        }
        fps = static_cast<double>(frames.size()) / kAudioSeconds;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        Sleep(600);
    }

    int last = -1;
    while (true) {
        int index = 0;
        if (use_mci) {
            char pos[64] = {};
            char mode[64] = {};
            mci("status " + g_alias + " position", pos, sizeof(pos));
            mci("status " + g_alias + " mode", mode, sizeof(mode));
            index = static_cast<int>(atol(pos) * fps / 1000.0);
            if (index < 0) index = 0;
            if (index >= static_cast<int>(frames.size())) break;
            if (index != last) {
                last = index;
                write_frame(frames[static_cast<size_t>(index)], max_width, max_rows);
            }
            if (std::string(mode) == "stopped") break;
        } else {
            DWORD code = 0;
            if (!GetExitCodeProcess(audio_proc, &code) || code != STILL_ACTIVE) break;
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            double seconds = static_cast<double>(now.QuadPart - t0.QuadPart) /
                             static_cast<double>(freq.QuadPart);
            index = static_cast<int>(seconds * fps);
            if (index < 0) index = 0;
            if (index >= static_cast<int>(frames.size())) break;
            if (index != last) {
                last = index;
                write_frame(frames[static_cast<size_t>(index)], max_width, max_rows);
            }
        }
        Sleep(30);
    }

    if (g_mci_open) {
        mci("stop " + g_alias, buffer512, sizeof(buffer512));
        mci("close " + g_alias, buffer512, sizeof(buffer512));
    }
    if (audio_proc) {
        TerminateProcess(audio_proc, 0);
        CloseHandle(audio_proc);
    }
    if (g_winmm) FreeLibrary(g_winmm);
    SetConsoleCursorInfo(g_out, &g_cursor);
    DWORD written = 0;
    WriteConsoleA(g_out, "\n", 1, &written, nullptr);
    DeleteFileA(frames_path.c_str());
    DeleteFileA(audio_path.c_str());
    RemoveDirectoryA(dir.c_str());
    return 0;
}
