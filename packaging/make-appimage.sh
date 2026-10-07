#!/usr/bin/env bash
# Собирает UltimateEditor-x86_64.AppImage: один файл, внутри Qt, FFmpeg и ffmpeg-CLI.
# При первом запуске AppImage показывает красивый установщик (установить / запустить без установки).
#   ./packaging/make-appimage.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
cd "$ROOT"
say() { printf '\033[1;36m[AppImage]\033[0m %s\n' "$*"; }

say "Собираю программу…"
./start.sh --build-only

mkdir -p dist && cd dist
export APPIMAGE_EXTRACT_AND_RUN=1          # инструменты запустятся и без FUSE
fetch() { [ -f "$2" ] || { say "Скачиваю $2"; curl -L --fail -o "$2" "$1"; chmod +x "$2"; }; }
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage linuxdeploy-x86_64.AppImage
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage

rm -rf AppDir && mkdir -p AppDir/usr/bin AppDir/usr/share/uve
cp ../build/uve AppDir/usr/bin/
[ -d ../Image ] && cp -r ../Image AppDir/usr/share/uve/ || true
cp "$(command -v ffmpeg)" "$(command -v ffprobe)" AppDir/usr/bin/       # ffmpeg внутри AppImage (экспорт, волны, прокси)
QT_QPA_PLATFORM=offscreen ../build/uve --export-icon AppDir/ultimate-video-editor.png || true

export QMAKE="$(command -v qmake6 || command -v qmake-qt6 || command -v qmake)"
export EXTRA_QT_PLUGINS="multimedia;platforms;platformthemes;wayland-shell-integration;wayland-graphics-integration-client;xcbglintegration;egldeviceintegrations"
export LINUXDEPLOY_OUTPUT_VERSION="$(date +%Y.%m.%d)"
say "Упаковываю…"
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt \
  --executable AppDir/usr/bin/uve --executable AppDir/usr/bin/ffmpeg --executable AppDir/usr/bin/ffprobe \
  --desktop-file ../packaging/uve.desktop --icon-file AppDir/ultimate-video-editor.png --output appimage
OUT="$(ls -t *.AppImage | grep -vi linuxdeploy | head -1)"
mv "$OUT" "../UltimateEditor-x86_64.AppImage"
say "Готово: $ROOT/UltimateEditor-x86_64.AppImage"
