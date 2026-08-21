// Bad Apple native player for Windows (x64 and ARM32 / Windows RT).
//
// Audio strategy, in order of preference:
//   1. WAV + waveOut (winmm PCM): needs no codec at all, works on Windows RT,
//      and waveOutGetPosition gives an exact audio clock for A/V sync.
//   2. MP3 + MCI mpegvideo: desktop Windows with an MP3 decoder installed.
//   3. mpv / ffplay launched hidden.
//
// winmm is loaded at runtime instead of linked, because the Windows SDK ARM32
// import libraries are not available on all toolchains.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const char* kFramesUrl =
    "https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1";
const char* kWavUrl =
    "https://github.com/LANqed/bad-apple/releases/latest/download/bad_apple.wav";
const char* kMp3Url =
    "https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3";
const double kAudioSeconds = 219.09;

typedef UINT(WINAPI* MciSendStringAFn)(LPCSTR, LPSTR, UINT, HWND);
typedef MMRESULT(WINAPI* WaveOutOpenFn)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT(WINAPI* WaveOutPrepareHeaderFn)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT(WINAPI* WaveOutWriteFn)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT(WINAPI* WaveOutGetPositionFn)(HWAVEOUT, LPMMTIME, UINT);
typedef MMRESULT(WINAPI* WaveOutResetFn)(HWAVEOUT);
typedef MMRESULT(WINAPI* WaveOutCloseFn)(HWAVEOUT);

HANDLE g_out = nullptr;
CONSOLE_CURSOR_INFO g_cursor{};
std::string g_alias;
bool g_mci_open = false;
HMODULE g_winmm = nullptr;

MciSendStringAFn p_mciSendStringA = nullptr;
WaveOutOpenFn p_waveOutOpen = nullptr;
WaveOutPrepareHeaderFn p_waveOutPrepareHeader = nullptr;
WaveOutWriteFn p_waveOutWrite = nullptr;
WaveOutGetPositionFn p_waveOutGetPosition = nullptr;
WaveOutResetFn p_waveOutReset = nullptr;
WaveOutCloseFn p_waveOutClose = nullptr;

HWAVEOUT g_wave = nullptr;
WAVEHDR g_wave_header{};
std::vector<char> g_wave_data;
DWORD g_bytes_per_second = 0;

template <typename T>
void bind(T* target, const char* name) {
    *target = reinterpret_cast<T>(GetProcAddress(g_winmm, name));
}

bool load_winmm() {
    g_winmm = LoadLibraryA("winmm.dll");
    if (!g_winmm) return false;
    bind(&p_mciSendStringA, "mciSendStringA");
    bind(&p_waveOutOpen, "waveOutOpen");
    bind(&p_waveOutPrepareHeader, "waveOutPrepareHeader");
    bind(&p_waveOutWrite, "waveOutWrite");
    bind(&p_waveOutGetPosition, "waveOutGetPosition");
    bind(&p_waveOutReset, "waveOutReset");
    bind(&p_waveOutClose, "waveOutClose");
    return true;
}

int mci(const std::string& command, char* buffer, int capacity) {
    if (!p_mciSendStringA) return -1;
    if (buffer && capacity > 0) buffer[0] = '\0';
    return static_cast<int>(
        p_mciSendStringA(command.c_str(), buffer, static_cast<UINT>(capacity), nullptr));
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
    std::string cmd = "curl.exe -fL --silent --show-error --ssl-no-revoke --max-time 300 \"" +
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

DWORD read_le32(const char* p) {
    return static_cast<DWORD>(static_cast<unsigned char>(p[0])) |
           (static_cast<DWORD>(static_cast<unsigned char>(p[1])) << 8) |
           (static_cast<DWORD>(static_cast<unsigned char>(p[2])) << 16) |
           (static_cast<DWORD>(static_cast<unsigned char>(p[3])) << 24);
}

// Plays a PCM WAV through waveOut. No codec is involved, so this also works on
// Windows RT where the MCI MP3 decoder is unavailable.
bool start_wav(const std::string& path) {
    if (!p_waveOutOpen || !p_waveOutWrite || !p_waveOutPrepareHeader) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<char> file((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (file.size() < 44) return false;
    if (std::memcmp(file.data(), "RIFF", 4) != 0 ||
        std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    WAVEFORMATEX format{};
    bool have_format = false;
    size_t offset = 12;
    while (offset + 8 <= file.size()) {
        const char* id = file.data() + offset;
        DWORD size = read_le32(file.data() + offset + 4);
        size_t body = offset + 8;
        if (body + size > file.size()) size = static_cast<DWORD>(file.size() - body);

        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            const char* f = file.data() + body;
            format.wFormatTag = static_cast<WORD>(static_cast<unsigned char>(f[0]) |
                                                  (static_cast<unsigned char>(f[1]) << 8));
            format.nChannels = static_cast<WORD>(static_cast<unsigned char>(f[2]) |
                                                 (static_cast<unsigned char>(f[3]) << 8));
            format.nSamplesPerSec = read_le32(f + 4);
            format.nAvgBytesPerSec = read_le32(f + 8);
            format.nBlockAlign = static_cast<WORD>(static_cast<unsigned char>(f[12]) |
                                                   (static_cast<unsigned char>(f[13]) << 8));
            format.wBitsPerSample = static_cast<WORD>(static_cast<unsigned char>(f[14]) |
                                                      (static_cast<unsigned char>(f[15]) << 8));
            format.cbSize = 0;
            have_format = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            g_wave_data.assign(file.begin() + static_cast<long>(body),
                               file.begin() + static_cast<long>(body + size));
        }
        offset = body + size + (size & 1);
    }

    if (!have_format || g_wave_data.empty()) return false;
    if (format.wFormatTag != WAVE_FORMAT_PCM) return false;

    g_bytes_per_second = format.nAvgBytesPerSec
                             ? format.nAvgBytesPerSec
                             : format.nSamplesPerSec * format.nBlockAlign;
    if (g_bytes_per_second == 0) return false;

    if (p_waveOutOpen(&g_wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_wave = nullptr;
        return false;
    }
    g_wave_header = WAVEHDR{};
    g_wave_header.lpData = g_wave_data.data();
    g_wave_header.dwBufferLength = static_cast<DWORD>(g_wave_data.size());
    if (p_waveOutPrepareHeader(g_wave, &g_wave_header, sizeof(g_wave_header)) != MMSYSERR_NOERROR ||
        p_waveOutWrite(g_wave, &g_wave_header, sizeof(g_wave_header)) != MMSYSERR_NOERROR) {
        if (p_waveOutClose) p_waveOutClose(g_wave);
        g_wave = nullptr;
        return false;
    }
    return true;
}

// Exact audio position in seconds, straight from the audio device.
double wav_position_seconds() {
    if (!g_wave || !p_waveOutGetPosition || g_bytes_per_second == 0) return -1.0;
    MMTIME time{};
    time.wType = TIME_BYTES;
    if (p_waveOutGetPosition(g_wave, &time, sizeof(time)) != MMSYSERR_NOERROR) return -1.0;
    if (time.wType != TIME_BYTES) return -1.0;
    return static_cast<double>(time.u.cb) / static_cast<double>(g_bytes_per_second);
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

std::vector<std::string> parse_frames(const std::string& path) {
    std::ifstream in(path);
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
    return frames;
}
}  // namespace

int main() {
    char temp_dir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp_dir);
    std::string dir = std::string(temp_dir) + "bad-apple-" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string frames_path = dir + "\\frames.ps1";
    std::string wav_path = dir + "\\audio.wav";
    std::string mp3_path = dir + "\\audio.mp3";

    printf("Downloading ASCII frames...\n");
    if (!download(kFramesUrl, frames_path)) {
        fprintf(stderr, "frame download failed\n");
        return 1;
    }
    std::vector<std::string> frames = parse_frames(frames_path);
    if (frames.empty()) {
        fprintf(stderr, "no frames found\n");
        return 1;
    }

    load_winmm();

    // WAV first: codec-free and gives an exact audio clock.
    printf("Downloading audio (WAV)...\n");
    bool have_wav = download(kWavUrl, wav_path) && start_wav(wav_path);
    bool use_mci = false;
    HANDLE audio_proc = nullptr;
    LARGE_INTEGER freq{}, t0{};
    char buffer512[512] = {};

    if (!have_wav) {
        printf("WAV unavailable, trying MP3...\n");
        if (download(kMp3Url, mp3_path) && p_mciSendStringA) {
            std::string short_audio = short_path(mp3_path);
            g_alias = "ba" + std::to_string(GetCurrentProcessId());
            std::string open_command =
                "open \"" + short_audio + "\" type mpegvideo alias " + g_alias;
            use_mci = mci(open_command, buffer512, sizeof(buffer512)) == 0;
            if (use_mci) {
                g_mci_open = true;
                mci("play " + g_alias, buffer512, sizeof(buffer512));
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
        }
        if (!use_mci) {
            const char* candidates[] = {
                "mpv --no-video --really-quiet",
                "ffplay -nodisp -autoexit -loglevel quiet",
            };
            for (const char* candidate : candidates) {
                std::string cmd_line = std::string(candidate) + " \"" + mp3_path + "\"";
                if (run_hidden(cmd_line, &audio_proc)) break;
            }
            if (!audio_proc) {
                fprintf(stderr,
                        "No audio backend available.\n"
                        "Tried: WAV via waveOut, MP3 via MCI, mpv, ffplay.\n"
                        "Check network access to the WAV asset, or install mpv/ffplay.\n");
                if (g_winmm) FreeLibrary(g_winmm);
                return 1;
            }
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&t0);
            Sleep(600);
        }
    }

    size_t max_width = 0, max_rows = 0;
    compute_dimensions(frames, &max_width, &max_rows);

    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleCursorInfo(g_out, &g_cursor);
    CONSOLE_CURSOR_INFO hidden = {1, FALSE};
    SetConsoleCursorInfo(g_out, &hidden);

    double fps = static_cast<double>(frames.size()) / kAudioSeconds;
    if (have_wav) {
        double total = static_cast<double>(g_wave_data.size()) /
                       static_cast<double>(g_bytes_per_second);
        if (total > 1.0) fps = static_cast<double>(frames.size()) / total;
    } else if (use_mci) {
        mci("status " + g_alias + " length", buffer512, sizeof(buffer512));
        long duration_ms = atol(buffer512);
        if (duration_ms > 0) {
            double computed = static_cast<double>(frames.size()) /
                              (static_cast<double>(duration_ms) / 1000.0);
            if (computed >= 1.0 && computed <= 60.0) fps = computed;
        }
    }

    int last = -1;
    while (true) {
        double seconds = 0.0;
        if (have_wav) {
            seconds = wav_position_seconds();
            if (seconds < 0.0) break;
        } else if (use_mci) {
            char pos[64] = {};
            char mode[64] = {};
            mci("status " + g_alias + " position", pos, sizeof(pos));
            mci("status " + g_alias + " mode", mode, sizeof(mode));
            seconds = static_cast<double>(atol(pos)) / 1000.0;
            if (std::string(mode) == "stopped" && seconds <= 0.0) break;
        } else {
            DWORD code = 0;
            if (!GetExitCodeProcess(audio_proc, &code) || code != STILL_ACTIVE) break;
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            seconds = static_cast<double>(now.QuadPart - t0.QuadPart) /
                      static_cast<double>(freq.QuadPart);
        }

        int index = static_cast<int>(seconds * fps);
        if (index < 0) index = 0;
        if (index >= static_cast<int>(frames.size())) break;
        if (index != last) {
            last = index;
            write_frame(frames[static_cast<size_t>(index)], max_width, max_rows);
        }
        Sleep(30);
    }

    if (g_wave) {
        if (p_waveOutReset) p_waveOutReset(g_wave);
        if (p_waveOutClose) p_waveOutClose(g_wave);
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
    DeleteFileA(wav_path.c_str());
    DeleteFileA(mp3_path.c_str());
    RemoveDirectoryA(dir.c_str());
    return 0;
}
