// Bad Apple native player for Windows (x64).
// Downloads ASCII frames and MP3 via curl.exe, plays MP3 through the MCI layer
// (winmm) and renders frames locked to the audio clock, so video never drifts.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#pragma comment(lib, "winmm.lib")

namespace {
const char* kFramesUrl =
    "https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1";
const char* kAudioUrl =
    "https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3";
const double kAudioSeconds = 219.09;

HANDLE g_out = nullptr;
CONSOLE_CURSOR_INFO g_cursor{};
std::string g_alias;
bool g_mci_open = false;

bool download(const std::string& url, const std::string& out) {
    std::string cmd = "curl.exe -fL --silent --show-error \"" + url + "\" -o \"" + out + "\"";
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

std::string short_path(const std::string& path) {
    int len = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(len, 0);
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wide[0], len);
    std::wstring short_name(MAX_PATH, 0);
    GetShortPathNameW(wide.c_str(), &short_name[0], (DWORD)short_name.size());
    int out_len = WideCharToMultiByte(CP_ACP, 0, short_name.c_str(), -1,
                                      nullptr, 0, nullptr, nullptr);
    std::string result(out_len, 0);
    WideCharToMultiByte(CP_ACP, 0, short_name.c_str(), -1, &result[0], out_len,
                        nullptr, nullptr);
    return result;
}

int mci(const std::string& command, char* buffer, int capacity) {
    return mciSendStringA(command.c_str(), buffer, capacity, nullptr);
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
    SetConsoleCursorPosition(g_out, {0, 0});
    DWORD written = 0;
    WriteConsoleA(g_out, frame.c_str(), (DWORD)frame.size(), &written, nullptr);
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

    if (!download(kFramesUrl, frames_path) || !download(kAudioUrl, audio_path)) {
        fprintf(stderr, "download failed\n");
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

    std::string short_audio = short_path(audio_path);
    g_alias = "ba" + std::to_string(GetCurrentProcessId());
    char buffer512[512];
    char command[1024];
    snprintf(command, sizeof(command), "open \"%s\" type mpegvideo alias %s",
             short_audio.c_str(), g_alias.c_str());

    bool use_mci = mci(command, buffer512, sizeof(buffer512)) == 0;
    double fps = static_cast<double>(frames.size()) / kAudioSeconds;
    HANDLE audio_proc = nullptr;
    LARGE_INTEGER freq{}, t0{};

    if (use_mci) {
        g_mci_open = true;
        mci(("play " + g_alias), buffer512, sizeof(buffer512));
        mci(("status " + g_alias + " length"), buffer512, sizeof(buffer512));
        long duration_ms = atol(buffer512);
        if (duration_ms > 0) {
            double computed = static_cast<double>(frames.size()) /
                              (static_cast<double>(duration_ms) / 1000.0);
            if (computed >= 1.0 && computed <= 60.0) fps = computed;
        }
    } else {
        for (const char* candidate : {"mpv", "ffplay"}) {
            std::string cmd_line = std::string(candidate) +
                                   (std::string(candidate) == "mpv"
                                        ? " --no-video --really-quiet"
                                        : " -nodisp -autoexit -loglevel quiet");
            cmd_line += " \"" + audio_path + "\"";
            STARTUPINFOA si{};
            PROCESS_INFORMATION pi{};
            si.cb = sizeof(si);
            if (CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                audio_proc = pi.hProcess;
                CloseHandle(pi.hThread);
                break;
            }
        }
        if (!audio_proc) {
            fprintf(stderr, "MCI and mpv/ffplay unavailable\n");
            return 1;
        }
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        Sleep(600);
    }

    int last = -1;
    while (true) {
        if (use_mci) {
            char pos[64] = {};
            char mode[64] = {};
            mci(("status " + g_alias + " position"), pos, sizeof(pos));
            mci(("status " + g_alias + " mode"), mode, sizeof(mode));
            int position = atoi(pos);
            int index = static_cast<int>(position * fps / 1000.0);
            if (index < 0) index = 0;
            if (index != last) {
                if (index >= static_cast<int>(frames.size())) break;
                last = index;
                write_frame(frames[static_cast<size_t>(index)], max_width, max_rows);
            }
            if (std::string(mode) == "stopped" || index >= static_cast<int>(frames.size()) - 1) break;
        } else {
            DWORD code = 0;
            if (!GetExitCodeProcess(audio_proc, &code) || code != STILL_ACTIVE) break;
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            double seconds = static_cast<double>(now.QuadPart - t0.QuadPart) /
                             static_cast<double>(freq.QuadPart);
            int index = static_cast<int>(seconds * fps);
            if (index < 0) index = 0;
            if (index != last) {
                if (index >= static_cast<int>(frames.size())) break;
                last = index;
                write_frame(frames[static_cast<size_t>(index)], max_width, max_rows);
            }
        }
        Sleep(30);
    }

    if (g_mci_open) {
        mci(("stop " + g_alias), buffer512, sizeof(buffer512));
        mci(("close " + g_alias), buffer512, sizeof(buffer512));
    }
    SetConsoleCursorInfo(g_out, &g_cursor);
    DWORD written = 0;
    WriteConsoleA(g_out, "\n", 1, &written, nullptr);
    DeleteFileA(frames_path.c_str());
    DeleteFileA(audio_path.c_str());
    RemoveDirectoryA(dir.c_str());
    return 0;
}
