# Windows: compilar no VS Code e empacotar

O toolchain do Windows é o **MSYS2 UCRT64** (GCC/MinGW-w64 + Qt 6 + CMake +
Ninja), o mesmo usado no CI.

## 1. Instalar as ferramentas (uma vez)

1. Instale o [MSYS2](https://www.msys2.org/) em `C:\msys64` (o padrão).
2. No terminal **MSYS2 UCRT64**:

   ```bash
   pacman -Syu   # se ele fechar o terminal, abra de novo e rode outra vez
   pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,gdb,cmake,ninja,qt6-base,qt6-tools,qt6-translations,gtest,nsis}
   ```

3. Coloque `C:\msys64\ucrt64\bin` no PATH do usuário (PowerShell):

   ```powershell
   [Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path","User") + ";C:\msys64\ucrt64\bin", "User")
   ```

   Feche e reabra o VS Code depois. O PATH é o que permite ao VS Code achar o
   `cmake.exe` e ao app achar as DLLs do Qt quando roda a partir do editor.

4. No VS Code, instale as extensões recomendadas pelo projeto (**CMake Tools**
   e **C/C++**). Ele oferece isso sozinho ao abrir a pasta.

## 2. Compilar, testar e rodar

1. Abra a pasta do projeto numa janela **local** do VS Code (não na janela
   "WSL"). Se o código estiver no WSL, abra pelo caminho
   `\\wsl.localhost\<distro>\caminho\do\loudmixer`. Se o VS Code avisar sobre
   host UNC, permita `wsl.localhost`. Se sugerir "Reabrir no WSL", ignore:
   essa opção compila a versão Linux.
2. Na barra de status, escolha o preset **Windows (MinGW) · Debug** (ou
   Ctrl+Shift+P → *CMake: Select Configure Preset*).
3. **F7** compila. *CMake: Run Tests* roda os testes. **F5** depura o app
   com o gdb (configuração "LoudMixer (gdb)" em `.vscode/launch.json`).

A pasta de build do Windows fica em `%LOCALAPPDATA%\loudmixer-build\<preset>`,
fora do repositório. Isso é proposital: o compartilhamento de arquivos do WSL
não suporta *file locking*, que o `rcc` do Qt usa, e o `cmd.exe` não aceita
caminho UNC como pasta atual. Com o código no WSL e o build num disco do
Windows, as duas coisas funcionam. As janelas WSL e Windows podem ficar
abertas ao mesmo tempo, cada uma com o seu build.

Pelo terminal (PowerShell ou MSYS2), na pasta do projeto:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Se o MSYS2 estiver em outro lugar, crie um `CMakeUserPresets.json` (ignorado
pelo git) com um preset que herde de `windows-debug` e redefina
`MSYS2_UCRT64` no `environment`.

## 3. Instalador

```powershell
cd $env:LOCALAPPDATA\loudmixer-build\windows-release
cpack -G NSIS   # loudmixer-0.1.0-win64.exe (instalador)
cpack -G ZIP    # versão portátil
```

A instalação chama o `windeployqt` (DLLs e plugins do Qt) e depois copia as
demais DLLs de que o Qt do MSYS2 depende (ICU, zstd, harfbuzz, runtime do GCC
etc.). O resultado roda numa máquina sem o MSYS2. O desinstalador remove também
a entrada de auto-start (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\LoudMixer`).

### MSVC (alternativa, não testada)

O código também foi escrito para o MSVC (`/W4 /permissive-`), mas esse caminho
nunca foi compilado. Com Build Tools for Visual Studio 2022 + Qt para MSVC:
`cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\<versão>\msvc2022_64`.

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
