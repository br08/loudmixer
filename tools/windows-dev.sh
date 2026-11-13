#!/usr/bin/env bash
# Compila a versão Windows (MSYS2 UCRT64) a partir do WSL e a executa no
# Windows, sem precisar de uma janela local do VS Code.
#
# Uso: tools/windows-dev.sh [run|build|test] [debug|release]   (padrão: run debug)
#
# Requer o MSYS2 em C:\msys64 com o toolchain (packaging/windows/README.md).
# O build fica em %LOCALAPPDATA%\loudmixer-build\windows-<config>.
set -euo pipefail

ACTION="${1:-run}"
CONFIG="${2:-debug}"
PRESET="windows-$CONFIG"
UCRT=/mnt/c/msys64/ucrt64/bin
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v powershell.exe >/dev/null; then
    echo "Este script roda no WSL, com a integração com o Windows ativa." >&2
    exit 1
fi
if [[ ! -x "$UCRT/cmake.exe" ]]; then
    echo "MSYS2 não encontrado em C:\\msys64 (veja packaging/windows/README.md)." >&2
    exit 1
fi
case "$ACTION" in run | build | test) ;; *)
    echo "uso: $0 [run|build|test] [debug|release]" >&2
    exit 2
    ;;
esac

# O CMake do Windows lê o CMakePresets.json da pasta atual (\\wsl.localhost\...).
cd "$ROOT"
"$UCRT/cmake.exe" --preset "$PRESET" >/dev/null
"$UCRT/cmake.exe" --build --preset "$PRESET"

case "$ACTION" in
test)
    "$UCRT/ctest.exe" --preset "$PRESET"
    ;;
run)
    # O PATH do MSYS2 é para o app achar as DLLs do Qt.
    powershell.exe -NoProfile -Command "\$env:PATH = 'C:\\msys64\\ucrt64\\bin;' + \$env:PATH; Start-Process (Join-Path \$env:LOCALAPPDATA 'loudmixer-build\\$PRESET\\loudmixer.exe')"
    echo "LoudMixer ($PRESET) aberto no Windows."
    ;;
esac
