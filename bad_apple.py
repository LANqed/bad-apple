#!/usr/bin/env python3
"""Play a video as monochrome terminal art with synchronized audio."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


CSI = "\x1b["


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="在终端中播放带声音的 Bad Apple 视频",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("video", type=Path, help="视频文件路径，例如 bad_apple.mp4")
    parser.add_argument("--fps", type=float, default=30.0, help="终端画面帧率")
    parser.add_argument("--width", type=int, help="输出字符宽度；默认使用终端宽度")
    parser.add_argument("--height", type=int, help="输出字符高度；默认使用终端高度")
    parser.add_argument("--threshold", type=int, default=128, help="黑白分界值 (0-255)")
    parser.add_argument("--invert", action="store_true", help="反转黑白画面")
    parser.add_argument("--mute", action="store_true", help="静音播放")
    return parser.parse_args()


def enable_ansi_on_windows() -> None:
    if os.name != "nt":
        return

    import ctypes

    kernel32 = ctypes.windll.kernel32
    handle = kernel32.GetStdHandle(-11)
    mode = ctypes.c_ulong()
    if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
        kernel32.SetConsoleMode(handle, mode.value | 0x0004)


def output_size(width: int | None, height: int | None) -> tuple[int, int]:
    terminal = shutil.get_terminal_size((80, 24))
    columns = width or max(20, terminal.columns - 1)
    rows = height or max(8, terminal.lines - 2)
    if columns < 2 or rows < 1:
        raise ValueError("输出尺寸太小")
    return columns, rows


def find_audio_player(video: Path) -> list[str] | None:
    ffplay = shutil.which("ffplay")
    if ffplay:
        return [
            ffplay,
            "-nodisp",
            "-autoexit",
            "-loglevel",
            "quiet",
            str(video),
        ]

    mpv = shutil.which("mpv")
    if mpv:
        return [mpv, "--no-video", "--really-quiet", str(video)]

    return None


def decoder_command(video: Path, width: int, pixel_height: int, fps: float) -> list[str]:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("找不到 ffmpeg，请先安装并将其加入 PATH")

    video_filter = (
        f"scale={width}:{pixel_height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{pixel_height}:(ow-iw)/2:(oh-ih)/2:black,"
        f"fps={fps},format=gray"
    )
    return [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(video),
        "-an",
        "-vf",
        video_filter,
        "-f",
        "rawvideo",
        "-pix_fmt",
        "gray",
        "-",
    ]


def render_frame(frame: bytes, width: int, rows: int, threshold: int, invert: bool) -> str:
    lines: list[str] = []
    for row in range(rows):
        top_start = row * 2 * width
        bottom_start = top_start + width
        chars: list[str] = []
        for column in range(width):
            top = frame[top_start + column] >= threshold
            bottom = frame[bottom_start + column] >= threshold
            if invert:
                top, bottom = not top, not bottom
            chars.append((" ", "▄", "▀", "█")[(top << 1) | bottom])
        lines.append("".join(chars))
    return CSI + "H" + "\n".join(lines)


def read_frame(stream: object, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = stream.read(remaining)  # type: ignore[attr-defined]
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def terminate(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        process.kill()


def play(args: argparse.Namespace) -> int:
    if not args.video.is_file():
        raise RuntimeError(f"视频文件不存在：{args.video}")
    if args.fps <= 0:
        raise RuntimeError("--fps 必须大于 0")
    if not 0 <= args.threshold <= 255:
        raise RuntimeError("--threshold 必须在 0 到 255 之间")

    width, rows = output_size(args.width, args.height)
    pixel_height = rows * 2
    frame_size = width * pixel_height
    decoder = subprocess.Popen(
        decoder_command(args.video, width, pixel_height, args.fps),
        stdout=subprocess.PIPE,
    )
    audio: subprocess.Popen[bytes] | None = None

    try:
        assert decoder.stdout is not None
        first_frame = read_frame(decoder.stdout, frame_size)
        if len(first_frame) != frame_size:
            decoder.wait()
            raise RuntimeError("ffmpeg 未能解码视频，请检查文件格式或上方错误信息")

        if not args.mute:
            audio_command = find_audio_player(args.video)
            if audio_command is None:
                raise RuntimeError("找不到 ffplay 或 mpv；安装其中一个，或使用 --mute")
            audio = subprocess.Popen(
                audio_command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

        sys.stdout.write(CSI + "2J" + CSI + "?25l")
        start = time.monotonic()
        frame_number = 0
        frame = first_frame
        while len(frame) == frame_size:
            target = start + frame_number / args.fps
            delay = target - time.monotonic()
            if delay > 0:
                time.sleep(delay)

            # Reading continues even when rendering is late, allowing video to catch up to audio.
            if time.monotonic() - target < 1 / args.fps:
                sys.stdout.write(render_frame(frame, width, rows, args.threshold, args.invert))
                sys.stdout.flush()

            frame_number += 1
            frame = read_frame(decoder.stdout, frame_size)

        decoder.wait()
        if decoder.returncode:
            raise RuntimeError(f"ffmpeg 解码失败，退出码 {decoder.returncode}")
        if audio is not None:
            audio.wait()
        return 0
    finally:
        terminate(decoder)
        terminate(audio)
        sys.stdout.write(CSI + "?25h" + CSI + "0m\n")
        sys.stdout.flush()


def main() -> int:
    enable_ansi_on_windows()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    args = parse_args()
    try:
        return play(args)
    except (RuntimeError, ValueError, OSError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
