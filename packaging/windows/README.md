# Empacotamento Windows

O instalador é gerado pelo CPack (NSIS), que já chama o `windeployqt` via
`qt_generate_deploy_app_script` para copiar as DLLs e plugins do Qt.

```powershell
# Prompt "x64 Native Tools for VS 2022", com Qt 6.5+ (MSVC) no PATH/CMAKE_PREFIX_PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.5.3\msvc2019_64
cmake --build build
ctest --test-dir build --output-on-failure
cd build
cpack -G NSIS   # LoudMixer-0.1.0-win64.exe
cpack -G ZIP    # versão portátil
```

Requisitos: Visual Studio 2022 (ou Build Tools), CMake 3.24+, Ninja, Qt 6.5+
(componente MSVC 2019 64-bit) e [NSIS 3](https://nsis.sourceforge.io/).

O desinstalador remove também a entrada de auto-start
(`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\LoudMixer`).

## Boost acima de 100% no Windows

O Windows não permite ganho acima de 100% pelas APIs de volume. O LoudMixer
usa a rota descrita no plano: os apps tocam num **cabo de áudio virtual** e o
LoudMixer captura esse áudio (loopback), aplica o ganho com soft-clip e o
reinjeta no dispositivo real.

1. Instale o [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (gratuito).
2. Em *Configurações > Som*, defina **CABLE Input** como saída padrão.
3. No LoudMixer, aba **Dispositivos**, marque **Boost DSP** no seu fone/caixa.

A partir daí os sliders de apps, do dispositivo e dos canais vão até 200%.
Outro cabo virtual pode ser escolhido em *Configurações > Dispositivo de
origem do boost*.
