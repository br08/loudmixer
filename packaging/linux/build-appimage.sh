#!/usr/bin/env bash
# Gera LoudMixer-<versão>-x86_64.AppImage usando linuxdeploy + plugin Qt.
#
# Requisitos: as dependências de build (ver README) e acesso à internet para
# baixar o linuxdeploy na primeira execução.
#
# Uso: packaging/linux/build-appimage.sh [diretório-de-build]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:-$ROOT/build-appimage}"
APPDIR="$BUILD/AppDir"
TOOLS="$BUILD/tools"
ARCH="$(uname -m)"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLOUDMIXER_BUILD_TESTS=OFF
cmake --build "$BUILD"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"

mkdir -p "$TOOLS"
fetch() {
    local url="$1" out="$TOOLS/$(basename "$1")"
    if [[ ! -x "$out" ]]; then
        curl -fL --retry 3 -o "$out" "$url"
        chmod +x "$out"
    fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage"

# O plugin Qt precisa achar o qmake do Qt 6.
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || echo /usr/lib/qt6/bin/qmake)}"
# Wayland além de X11 (o plugin xcb é incluído por padrão).
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
export VERSION="$(grep -m1 -oP '^\s+VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt")"
# Em contêineres sem FUSE, os AppImages das ferramentas rodam extraídos.
export APPIMAGE_EXTRACT_AND_RUN=1

cd "$BUILD"
"$TOOLS/linuxdeploy-${ARCH}.AppImage" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/loudmixer.desktop" \
    --icon-file "$ROOT/resources/icons/loudmixer.svg" \
    --plugin qt \
    --output appimage

echo "AppImage gerado em: $BUILD"
ls -1 "$BUILD"/*.AppImage
