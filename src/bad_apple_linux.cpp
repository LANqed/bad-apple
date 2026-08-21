#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

namespace fs = std::filesystem;
static volatile std::sig_atomic_t stopping = 0;
static pid_t audio_pid = -1;

void stop_handler(int) { stopping = 1; }

bool command_exists(const std::string& command) {
    std::string check = "command -v " + command + " >/dev/null 2>&1";
    return std::system(check.c_str()) == 0;
}

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (char c : value) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    return result + "'";
}

bool download(const std::string& url, const fs::path& output) {
    std::string command = "curl -L --fail --silent --show-error " + shell_quote(url) +
                          " -o " + shell_quote(output.string());
    return std::system(command.c_str()) == 0;
}

void cleanup() {
    if (audio_pid > 0) {
        kill(audio_pid, SIGTERM);
        waitpid(audio_pid, nullptr, 0);
    }
    std::cout << "\033[?25h\033[0m\n" << std::flush;
}

pid_t start_audio(const fs::path& audio) {
    const char* player = command_exists("mpv") ? "mpv" :
                         command_exists("ffplay") ? "ffplay" : nullptr;
    if (!player) return -1;

    pid_t pid = fork();
    if (pid != 0) return pid;
    if (std::string(player) == "mpv") {
        execlp("mpv", "mpv", "--no-video", "--really-quiet", audio.c_str(), nullptr);
    } else {
        execlp("ffplay", "ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", audio.c_str(), nullptr);
    }
    _exit(127);
}

void bell_loop() {
    while (!stopping) {
        std::cout << '\a' << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
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
    std::cout << "\033[H" << os.str() << std::flush;
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

int main(int argc, char** argv) {
    const std::string frames_url =
        "https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1";
    const std::string audio_url =
        "https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3";
    const double audio_seconds = 219.09;
    const fs::path temp = fs::temp_directory_path() / ("bad-apple-" + std::to_string(getpid()));
    const fs::path frames = temp / "frames.ps1";
    const fs::path audio = temp / "bad-apple.mp3";

    if (!command_exists("curl")) {
        std::cerr << "需要 curl\n";
        return 1;
    }
    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);
    fs::create_directories(temp);
    if (!download(frames_url, frames) || !download(audio_url, audio)) {
        std::cerr << "素材下载失败，请检查网络或更换 CDN\n";
        fs::remove_all(temp);
        return 1;
    }

    std::ifstream input(frames);
    std::vector<std::string> all_frames;
    std::vector<std::string> buffer;
    bool capturing = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (capturing) {
            if (line == "\"@") {
                std::string frame;
                for (const auto& part : buffer) {
                    frame += part;
                    frame += '\n';
                }
                if (!frame.empty()) frame.pop_back();
                all_frames.push_back(frame);
                buffer.clear();
                capturing = false;
            } else {
                buffer.push_back(line);
            }
        } else if (line == "@\"") {
            capturing = true;
        }
    }
    if (all_frames.empty()) {
        std::cerr << "没有解析到 ASCII 帧\n";
        fs::remove_all(temp);
        return 1;
    }
    const double fps = argc > 1
        ? std::stod(argv[1])
        : static_cast<double>(all_frames.size()) / audio_seconds;
    if (fps <= 0) {
        std::cerr << "无效帧率\n";
        fs::remove_all(temp);
        return 1;
    }
    size_t max_width = 0, max_rows = 0;
    compute_dimensions(all_frames, &max_width, &max_rows);

    audio_pid = start_audio(audio);
    std::thread bell;
    if (audio_pid < 0) {
        std::cerr << "找不到 mpv/ffplay，降级为 PC Speaker/终端响铃（非原曲）\n";
        bell = std::thread(bell_loop);
    } else {
        // Let the audio player open the device and start output before the
        // frame clock begins, so video does not run ahead of the music.
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    std::atexit(cleanup);
    std::cout << "\033[2J\033[?25l" << std::flush;
    const auto start = std::chrono::steady_clock::now();
    for (size_t index = 0; index < all_frames.size() && !stopping; ++index) {
        const auto target = start + std::chrono::duration<double>(index / fps);
        std::this_thread::sleep_until(target);
        write_frame(all_frames[index], max_width, max_rows);
    }
    stopping = 1;
    if (bell.joinable()) bell.join();
    fs::remove_all(temp);
    return 0;
}
