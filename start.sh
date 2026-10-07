#!/usr/bin/env bash
# Ultimate Video Editor (C++ / Qt6 / OpenGL / FFmpeg) — установка, сборка и запуск одним файлом.
#   ./start.sh                  первый запуск: ставит зависимости, собирает, запускает; дальше — просто запускает
#   ./start.sh clip.mp4 a.png   открыть сразу с файлами
#   ./start.sh --rebuild        пересобрать с нуля
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

say()  { printf '\033[1;36m[UVE]\033[0m %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }
SUDO=""; [ "$(id -u)" -ne 0 ] && have sudo && SUDO="sudo"

ARGS=(); BUILD_ONLY=0
for a in "$@"; do
  case "$a" in
    --rebuild) rm -rf build ;;
    --build-only) BUILD_ONLY=1 ;;
    *) ARGS+=("$a") ;;
  esac
done

# ---------- 1. системные зависимости ----------
need_deps=0
have cmake && have g++ && have ffmpeg || need_deps=1
pkg-config --exists libavcodec libavformat libswscale libswresample libavutil 2>/dev/null || need_deps=1
pkg-config --exists Qt6Widgets Qt6OpenGLWidgets Qt6Multimedia 2>/dev/null || need_deps=1

if [ "$need_deps" -eq 1 ]; then
  say "Устанавливаю зависимости (нужен пароль sudo)…"
  if have apt-get; then
    $SUDO apt-get update
    $SUDO apt-get install -y build-essential cmake pkg-config ffmpeg \
      qt6-base-dev qt6-multimedia-dev libqt6opengl6-dev libgl-dev libxkbcommon-dev \
      libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavutil-dev \
      gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav gstreamer1.0-pulseaudio
  elif have dnf; then
    # H.264/AAC в Fedora требуют RPM Fusion (ffmpeg вместо ffmpeg-free)
    $SUDO dnf install -y gcc-c++ cmake pkgconf-pkg-config qt6-qtbase-devel qt6-qtmultimedia-devel mesa-libGL-devel \
      ffmpeg-free-devel ffmpeg-free
  elif have pacman; then
    $SUDO pacman -S --needed --noconfirm base-devel cmake pkgconf qt6-base qt6-multimedia ffmpeg mesa
  elif have zypper; then
    $SUDO zypper install -y gcc-c++ cmake pkg-config qt6-base-devel qt6-multimedia-devel qt6-opengl-devel qt6-openglwidgets-devel ffmpeg-devel ffmpeg
  else
    say "Неизвестный пакетный менеджер. Нужны: cmake, g++, Qt6 (Widgets, OpenGL, Multimedia), FFmpeg dev, ffmpeg."; exit 1
  fi
fi

# ---------- 2. сборка (инкрементальная: повторный запуск почти мгновенный) ----------
say "Сборка…"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)"

[ "$BUILD_ONLY" -eq 1 ] && { say "Сборка готова: build/uve"; exit 0; }

# ---------- 3. запуск ----------
# Если окна не двигаются мышью (бывает на Wayland):  UVE_X11=1 ./start.sh   (запуск через XWayland)
# Если звук глючит:                                  UVE_NO_AUDIO=1 ./start.sh
[ "${UVE_X11:-0}" = "1" ] && export QT_QPA_PLATFORM=xcb
say "Запуск редактора…"
exec ./build/uve "${ARGS[@]+"${ARGS[@]}"}"
