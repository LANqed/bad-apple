#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
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

int main(int argc, char** argv) {
    const std::string frames_url =
        "https://cdn.jsdelivr.net/gh/lxcnju/bad_apple_ascii@master/html/bad1.html";
    const std::string audio_url =
        "https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3";
    const fs::path temp = fs::temp_directory_path() / ("bad-apple-" + std::to_string(getpid()));
    const fs::path frames = temp / "frames.html";
    const fs::path audio = temp / "bad-apple.mp3";
    const double fps = argc > 1 ? std::stod(argv[1]) : 10.0;

    if (fps <= 0 || !command_exists("curl")) {
        std::cerr << "用法: bad-apple [fps]\n需要 curl\n";
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
    std::string html((std::istreambuf_iterator<char>(input)), {});
    std::regex frame_re(R"(<pre>([\s\S]*?)</pre>)", std::regex::icase | std::regex::optimize);
    std::sregex_iterator it(html.begin(), html.end(), frame_re), end;
    std::vector<std::string> all_frames;
    for (; it != end; ++it) all_frames.push_back((*it)[1].str());
    if (all_frames.empty()) {
        std::cerr << "没有解析到 ASCII 帧\n";
        fs::remove_all(temp);
        return 1;
    }

    audio_pid = start_audio(audio);
    std::thread bell;
    if (audio_pid < 0) {
        std::cerr << "找不到 mpv/ffplay，降级为 PC Speaker/终端响铃（非原曲）\n";
        bell = std::thread(bell_loop);
    }

    std::atexit(cleanup);
    std::cout << "\033[2J\033[?25l" << std::flush;
    const auto start = std::chrono::steady_clock::now();
    for (size_t index = 0; index < all_frames.size() && !stopping; ++index) {
        const auto target = start + std::chrono::duration<double>(index / fps);
        std::this_thread::sleep_until(target);
        std::cout << "\033[H" << all_frames[index] << std::flush;
    }
    stopping = 1;
    if (bell.joinable()) bell.join();
    fs::remove_all(temp);
    return 0;
}
