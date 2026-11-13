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

### A partir do WSL

Com o código no WSL, a versão Windows também pode ser compilada e aberta
direto do terminal do WSL (ou da janela WSL do VS Code), sem janela local:

```bash
tools/windows-dev.sh            # compila (Debug) e abre o app no Windows
tools/windows-dev.sh test       # compila e roda os testes no Windows
tools/windows-dev.sh run release
```

No VS Code, as mesmas ações estão em *Terminal > Run Task*: **Windows: rodar
(dev)**, **Windows: compilar (dev)** e **Windows: testes**. Esse caminho não
tem depurador; para depurar com o gdb, use a janela local descrita acima.

## 3. Instalador

```powershell
cd $env:LOCALAPPDATA\loudmixer-build\windows-release
cpack -G NSIS   # loudmixer-0.1.0-win64.exe (instalador)
cpack -G ZIP    # versão portátil
```

A instalação chama o `windeployqt` (DLLs e plugins do Qt) e depois copia as
demais DLLs de que o Qt do MSYS2 depende (ICU, zstd, harfbuzz, runtime do GCC
etc.). O resultado roda numa máquina sem o MSYS2. O instalador leva também o
APO (`loudmixer_apo.dll`) e o `loudmixer-apo-setup.exe`, em `bin`, de onde o
APO é registrado.

- **Desinstalar** tira o APO de todos os dispositivos e os restaura como eram
  antes de apagar os arquivos, e remove a entrada de auto-start
  (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\LoudMixer`).
- **Atualizar** (instalar uma versão nova por cima) mantém o boost ligado nos
  dispositivos. O DLL do APO em uso é renomeado e o novo vai para o mesmo
  caminho; o antigo é apagado no próximo boot e o novo passa a valer quando o
  áudio do Windows reiniciar.

### MSVC (alternativa, não testada)

O código também foi escrito para o MSVC (`/W4 /permissive-`), mas esse caminho
nunca foi compilado. Com Build Tools for Visual Studio 2022 + Qt para MSVC:
`cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\<versão>\msvc2022_64`.

## Boost acima de 100% no Windows

As APIs de volume do Windows param em 100%. Para passar disso, o LoudMixer tem
um **efeito de sistema (APO)** próprio: uma DLL que o Windows carrega na cadeia
de efeitos do dispositivo e que aplica o ganho, com soft-clip, à mistura dos
apps. Com o APO num dispositivo, os sliders do dispositivo, dos canais e dos
apps que tocam nele vão até 200%, sem cabo virtual e sem trocar a saída padrão.

No LoudMixer, aba **Dispositivos**, o botão **Boost 200%** de cada dispositivo
ativa o APO (o Windows pede permissão de administrador). O mesmo botão mostra o
estado e desativa o boost:

| Botão | Significado |
|---|---|
| **Boost 200%** | disponível; clique para ativar |
| **Boost ligado** | ativo; clique para desativar |
| **Boost pausado** | os aprimoramentos de áudio do dispositivo foram desligados nas configurações do Windows; clique para religar |
| **Boost removido** | uma atualização de driver ou do Windows tirou o APO do dispositivo; clique para religar |

Quando o boost cai por fora, o LoudMixer também avisa com uma notificação.

O botão usa a ferramenta `loudmixer-apo-setup`, que fica ao lado do
`loudmixer.exe` e também pode ser usada direto, como administrador:

```powershell
loudmixer-apo-setup list              # dispositivos e estado do APO
loudmixer-apo-setup install default   # ativa no dispositivo padrão (pede UAC)
loudmixer-apo-setup status default    # o áudio está passando pelo APO?
loudmixer-apo-setup uninstall all     # remove e restaura os dispositivos
```

- O `install` liga os **aprimoramentos de áudio** do dispositivo (o Windows só
  roda APOs assim). Isso também ativa os efeitos do fabricante, se houver.
- Ele escolhe sozinho o estágio da cadeia (EFX, MFX ou SFX) e confere se o
  áudio passa pelo APO. Se nenhum estágio funcionar, restaura o dispositivo e
  sai com o código 3.
- Durante o `install` e o `uninstall`, o áudio para por alguns segundos
  (os serviços de áudio são reiniciados).
- O `uninstall` restaura os valores originais do dispositivo, guardados no
  `install`.

Se o Windows não rodar efeitos de áudio no dispositivo, o LoudMixer avisa e
deixa o dispositivo como estava. Os riscos conhecidos dessa abordagem estão nas
issues com a label `risco`.

### Alternativa: cabo virtual

Para dispositivos em que o APO não funciona, o LoudMixer também pode passar de
100% por um cabo de áudio virtual: os apps tocam no cabo, o LoudMixer captura
esse áudio (loopback), aplica o ganho com soft-clip e o reinjeta no dispositivo
real.

1. Instale o [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (gratuito).
2. Em *Configurações > Som* do Windows, defina **CABLE Input** como saída padrão.
3. Nas configurações do LoudMixer, em **Avançado**, ligue **Mostrar a rota por
   cabo virtual**.
4. Na aba **Dispositivos**, marque **Boost DSP** no seu fone/caixa.

Outro cabo virtual pode ser escolhido no campo *Dispositivo de origem do boost*,
também em **Avançado**. Em dispositivos com o APO ativo, a opção **Boost DSP**
não aparece.
