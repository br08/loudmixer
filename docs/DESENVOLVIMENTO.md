# Desenvolvimento

Documento para quem trabalha no código do LoudMixer: arquitetura, como
compilar e testar, estado da implementação, o que foi e o que não foi
verificado, desvios em relação ao plano e como publicar uma release.

O plano original está em [PLANO_LOUDMIXER.md](../PLANO_LOUDMIXER.md).

---

## Arquitetura

```
UI (Qt Widgets) ── TrayController · MixerWindow · ChannelsPanel · GainSlider
        │  sinais/slots
Núcleo  ── AudioSessionManager · GainProcessor (DSP) · PresetStore · AutoStartService
        │  IAudioBackend
Backends ── PulseBackend (+ PulseBoostRoute) │ PipeWireBackend │ WasapiBackend (+ APO · LoopbackCapture) │ MockBackend
```

### Linux

O backend padrão usa a **libpulse**, que funciona tanto com o PulseAudio quanto
com o **PipeWire** (via `pipewire-pulse`). Ganho acima de 100% é nativo:

- **App** → volume do sink-input
- **Dispositivo** → volume do sink (escala os canais preservando o balanço)
- **Canal** → valor individual do volume do sink

**Boost DSP** (`PulseBoostRoute`): cria um sink virtual, captura o que toca
nele, passa pelo `GainProcessor` (soft-clip + medição para o LED de clipping) e
reproduz no dispositivo real.

### Windows

As APIs de volume do Windows param em 100% (sessão via `ISimpleAudioVolume`,
dispositivo e canais via `IAudioEndpointVolume`). Acima disso entra o **APO**
próprio (`src/apo/`), que aplica o `GainProcessor` à mistura dos apps:

- **Dispositivo e canais:** até 100% no volume do Windows, acima disso no APO.
- **Apps:** o APO sobe pelo maior ganho pedido (B) e cada app é atenuado no
  volume nativo para pedido/B (`ApoRouteMath`).
- O ganho fica salvo no registro e o APO o reaplica quando o Windows o recria
  (reboot, reconexão), mesmo com o app fechado.

A rota alternativa por **cabo virtual** usa `LoopbackCapture`: captura o cabo
em loopback, aplica o DSP e reinjeta no dispositivo físico. Detalhes do APO,
da ferramenta `loudmixer-apo-setup` e do instalador em
[packaging/windows/README.md](../packaging/windows/README.md).

### Estrutura

```
src/core/            IAudioBackend, GainProcessor (DSP puro), AudioSessionManager, PresetStore, AutoStartService
src/backend/linux/   PulseBackend, PulseBoostRoute, PipeWireBackend
src/backend/windows/ WasapiBackend, ApoRouteMath.h, SessionEnumerator, LoopbackCapture, ComUtil.h
src/apo/             APO (loudmixer_apo.dll), loudmixer-apo-setup, interfaces e estado compartilhado
src/backend/mock/    MockBackend
src/ui/              TrayController, MixerWindow, ChannelsPanel, GainSlider, SettingsDialog, SingleInstance, GlobalHotkeys, AppIcons, BuildInfo.h
tests/               GainProcessorTest, MockBackendTest, ApoRouteMathTest, PulseBackendLiveTest; no Windows: ApoTest, WasapiApoLiveTest
translations/        loudmixer_pt_BR.ts  (tools/fill_pt_BR.py aplica as traduções)
resources/           ícones (tools/make_icons.py gera .ico/.png), .qrc, .rc
tools/               windows-dev.sh (versão Windows a partir do WSL), fill_pt_BR.py, make_icons.py
packaging/           linux/ (AppImage, .desktop, Flatpak) · windows/ (NSIS via CPack)
.github/workflows/   ci.yml (compila, testa e empacota em todo push e PR) · release.yml (publica releases)
```

---

## Compilar

> **WSL ≠ Windows.** Compilado dentro do WSL, o LoudMixer é a versão
> **Linux**: ele controla o servidor de áudio do WSLg (dispositivo
> `RDPSink`), por onde passa só o som de programas Linux do WSL. O volume do
> Windows e dos apps do Windows só é controlado pela versão **Windows**.

O projeto traz `CMakePresets.json` para o VS Code (extensão CMake Tools):
`linux-debug` / `linux-release` e `windows-debug` / `windows-release`.

### Linux (Debian 12 / Ubuntu 22.04+)

```bash
sudo apt install cmake ninja-build pkg-config g++ \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
    libpulse-dev libpipewire-0.3-dev libspa-0.2-dev libgtest-dev

cmake --preset linux-release        # ou: cmake -S . -B build -G Ninja
cmake --build --preset linux-release
ctest --preset linux-release
./build-linux-release/src/loudmixer
```

Pacotes: `cd build-linux-release && cpack -G DEB` · AppImage: `packaging/linux/build-appimage.sh`
· Flatpak: `packaging/linux/flatpak/org.loudmixer.LoudMixer.yml`.

### Windows

Usa o **MSYS2 UCRT64** (GCC + Qt 6 + CMake + Ninja). Resumo:

1. Instale o MSYS2 em `C:\msys64` e, no terminal UCRT64:
   `pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,gdb,cmake,ninja,qt6-base,qt6-tools,qt6-translations,gtest,nsis}`
2. Ponha `C:\msys64\ucrt64\bin` no PATH do usuário e reabra o VS Code.
3. Abra a pasta do projeto numa janela **local** do VS Code (se o código
   estiver no WSL: `\\wsl.localhost\<distro>\...`), escolha o preset
   **Windows (MinGW) · Debug** e aperte **F7**. **F5** depura.
4. Com o código no WSL, `tools/windows-dev.sh` compila a versão Windows e a
   abre a partir do terminal do WSL (sem depurador).

Passo a passo, instalador NSIS e detalhes em
[packaging/windows/README.md](../packaging/windows/README.md).

### Opções do CMake

| Opção | Padrão | |
|---|---|---|
| `LOUDMIXER_BUILD_APP` | ON | OFF compila só núcleo + testes (sem Qt Widgets) |
| `LOUDMIXER_BUILD_TESTS` | ON | GoogleTest do sistema ou baixado via FetchContent |
| `LOUDMIXER_WITH_PULSE` | ON | backend libpulse |
| `LOUDMIXER_WITH_PIPEWIRE` | ON | backend PipeWire nativo (se a lib existir) |
| `LOUDMIXER_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |

## Testes

```bash
ctest --preset linux-release                                            # unitários (Linux)
LOUDMIXER_LIVE_TESTS=1 ./build-linux-release/tests/PulseBackendLiveTest   # contra o servidor real*
tools/windows-dev.sh test                                               # unitários (Windows, a partir do WSL)
```

No Windows, o `WasapiApoLiveTest` (também com `LOUDMIXER_LIVE_TESTS=1`) testa a
rota APO contra o áudio real; precisa do APO ligado num dispositivo.

\*Os testes contra o áudio real alteram volumes por alguns segundos e os
restauram no final. O do Pulse silencia o dispositivo durante o teste de boost
para não tocar um bipe alto.

`loudmixer --backend mock` abre a UI com dados fictícios, útil para
desenvolver sem áudio.

---

## Estado da implementação

| Fase do plano | Estado |
|---|---|
| **0 — Fundação** | ✅ CMake multiplataforma, `IAudioBackend`, `MockBackend`, `GainProcessor` + testes, esqueleto Qt com bandeja |
| **1 — Linux MVP** | ✅ App / dispositivo / canal até 200%, rota de boost DSP com soft-clip, hot-plug, reconexão. Script de AppImage (não executado) |
| **2 — Windows MVP** | ✅ WASAPI (sessões, dispositivo, canais) compilado e testado no Windows real; instalador NSIS testado (instalação, atualização e desinstalação). Boost > 100% por efeito de sistema (APO) próprio, testado no Windows real; cabo virtual como alternativa |
| **3 — UX** | ✅ Presets, auto-start, LED de clipping, atalhos, i18n PT-BR + EN |
| **4 — Estabilidade** | ✅ Testes com ASan/UBSan/TSan, hot-plug no mock, encerramento limpo. ⚠️ Suspender/hibernar não testado |

### O que foi verificado

- **DSP (`GainProcessor`)**: testes unitários de transparência em 100%,
  linearidade abaixo do joelho, saída nunca acima de ±1.0, curvas monotônicas
  e contínuas, rampa sem "zipper", NaN, multicanal, medição e concorrência.
  Limpo sob ASan, UBSan e TSan.
- **Núcleo Qt** (`AudioSessionManager`, `PresetStore`, `AutoStartService`):
  testes com o `MockBackend`, incluindo o boost do sistema simulado (medição
  do LED, pedido assíncrono e aviso de boost perdido). O teste do auto-start
  roda só no Linux.
- **Divisão de ganho da rota APO** (`ApoRouteMath`): testes nas duas
  plataformas.
- **Backend Pulse contra um servidor real** (PulseAudio do WSLg): ganho de
  dispositivo > 100%, por canal, mute, detecção de um app novo e boost dele a
  200%, e a rota DSP processando áudio de verdade (≈43 mil amostras,
  clipping detectado, pico de saída 0,9999). Esses testes restauram o estado
  original ao final e rodam limpos sob ASan/UBSan.
- **App completo** no Linux: compila com `-Werror`, sobe contra o Pulse real,
  instância única e comandos (`--volume-up`, `--quit`…) funcionando, UI
  renderizada e conferida nas três abas, em inglês e português.
- **Windows (MSYS2 UCRT64: GCC 16, Qt 6.12)**: o app inteiro compila com
  `-Werror`, todos os testes passam (incluindo os do APO, que carregam a DLL
  real), e o app roda no Windows real. Instância
  única e `--quit` funcionam, e `--volume-down` / `--volume-up` mudaram de
  fato o volume do dispositivo padrão (63% → 58% → 63%). O instalador NSIS e
  o ZIP são gerados, e a versão instalada roda sem o MSYS2 no PATH.
- **APO no Windows 11 real** (fone Bluetooth): o Windows carrega a DLL não
  assinada; a escolha automática de estágio caiu no MFX depois de o EFX não
  ser montado; em 200% o som fica mais alto, com o soft-clip segurando os
  picos. O `WasapiApoLiveTest` confirmou dispositivo, canal e app chegando ao
  APO, o limite de 200% e o estado salvo no registro sem administrador. O
  `uninstall` restaurou o dispositivo exatamente como estava.
- **Boost pelo app no Windows real**: os botões ligam e desligam o APO (com
  UAC) e indicam quando os aprimoramentos de áudio são desligados nas
  configurações do Windows, com notificação; o app continua funcionando
  depois de cada reinício dos serviços de áudio. Conferido visualmente: slider
  até 200%, LED ativo e opção do cabo virtual escondida.
- **Instalador NSIS no Windows real**: instalação por cima de uma versão
  anterior (pelo assistente e em modo silencioso), APO registrado a partir de
  `Program Files`, atualização com o boost ligado (o DLL em uso é trocado e o
  boost continua ativo) e desinstalação restaurando os dispositivos, sem
  sobras em arquivos ou no registro.
- **Cabo virtual** (VB-Audio Cable): testado manualmente no Windows real.
- **CI no GitHub**: os jobs Linux, Linux com sanitizers e Windows passam e
  geram o `.deb`, o instalador NSIS e o ZIP.

### O que NÃO foi verificado

- Build com **MSVC** (o toolchain do Windows é o MinGW do MSYS2).
- **APO** num alto-falante com efeitos do fabricante no estágio EFX (só foi
  testado num fone Bluetooth, onde ele roda no estágio MFX), em DAC USB e HDMI,
  e com conteúdo protegido por DRM.
- **Backend PipeWire nativo**: compila, mas o ambiente de desenvolvimento não
  tinha daemon PipeWire. Só o caminho de erro foi testado.
- Reconexão automática do Pulse após o servidor reiniciar.
- Geração de AppImage e Flatpak (scripts prontos, não executados).
- Suspender/hibernar.

## Desvios em relação ao plano

| Plano | Implementado | Motivo |
|---|---|---|
| PipeWire como backend primário, Pulse como fallback | **libpulse primeiro**, PipeWire nativo como fallback experimental | A libpulse cobre PulseAudio **e** PipeWire (via pipewire-pulse), trata corretamente o volume de hardware (Route) e pôde ser testada contra um servidor real |
| `FilterChainNode` | `PulseBoostRoute` (sink virtual + captura + DSP + reprodução) | Mesmo efeito, funciona em PulseAudio e PipeWire, e permite usar o nosso `GainProcessor` (soft-clip + medição) |
| `PulseFallback.{h,cpp}` | `PulseBackend.{h,cpp}` | Deixou de ser fallback |
| WIL / WRL | `ComUtil.h` próprio | Compila também com MinGW |
| MSVC + Visual Studio | **MSYS2 UCRT64** (GCC/MinGW + Qt 6), no VS Code | Não exige o Visual Studio; gratuito e sem conta. É o toolchain testado e o usado no CI |
| Cabo virtual na v1, APO assinado na v2 | **APO próprio** (não assinado) já agora; cabo virtual como alternativa | O Windows carrega o APO sem assinatura; boost sem programa de terceiros nem troca da saída padrão |
| Qt 6.5+ | Qt **6.4+** | Debian 12 traz 6.4.2; nenhuma API usada exige 6.5 |
| `MixerWindow.ui` / QML | Qt Widgets em código | As linhas são dinâmicas (apps aparecem/somem); o slider é desenhado à mão |
| Soft-clip em todo boost | Windows: sempre (no APO ou na rota do cabo). Linux: só na **rota DSP** | O ganho nativo do Pulse/PipeWire > 100% é aplicado no servidor, fora do nosso alcance |

## Pendências técnicas

- **Windows**: volumes e sessões são lidos por varredura a cada 750 ms (mais
  notificações de hot-plug).
- **Windows (cabo virtual)**: trocar a saída padrão por código exigiria a API
  não documentada `IPolicyConfig`.
- **Rota DSP no Linux**: quando os relógios do sink virtual e do físico
  divergem, o excesso é descartado para não acumular latência. Um resampler
  adaptativo eliminaria os estalos ocasionais.
- **PipeWire nativo**: ajusta o volume do nó, não o Route do dispositivo, e
  não tem rota DSP nem reconexão.
- **Flatpak**: *Iniciar com o sistema* precisa usar o portal Background.
- **APO**: riscos acompanhados nas issues com a label `risco`.
- **Licença**: falta escolher uma (o Qt é usado sob LGPL, com link dinâmico).

---

## Publicar uma release

O workflow [release.yml](../.github/workflows/release.yml) roda só quando uma
tag `v*` é enviada ao GitHub; push de commits não publica nada. Ele roda os
mesmos jobs da CI (compilação, testes e pacotes) e, se todos passarem, cria a
release com o instalador NSIS, o ZIP, o `.deb` e um `SHA256SUMS.txt`.

1. Atualize a versão em `project(LoudMixer VERSION x.y.z)` no
   [CMakeLists.txt](../CMakeLists.txt) e faça o merge na `main`.
2. Crie e envie a tag a partir da `main`:

   ```bash
   git checkout main && git pull
   git tag v0.2.0
   git push origin v0.2.0
   ```

- A tag precisa bater com a versão do CMake; se não bater, o workflow falha
  sem publicar.
- Tag com sufixo (`v0.2.0-rc1`) vira **pré-release**. O sufixo não entra no
  nome dos pacotes, que usam a versão do CMake (`0.2.0`).
- As notas da release juntam o texto fixo de
  [.github/release-notes.md](../.github/release-notes.md) (tabela de downloads
  e aviso sobre a falta de assinatura) com as notas geradas pelo GitHub a
  partir dos PRs e commits desde a release anterior.
- Se o workflow falhar, apague a tag (`git push --delete origin v0.2.0` e
  `git tag -d v0.2.0`), corrija e envie de novo.
