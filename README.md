# LoudMixer

Amplificador de volume para **Linux** e **Windows** (C++20 / Qt 6): leva o
áudio **acima de 100%** (até 200%, com soft-clipping) com controle por
**aplicativo**, por **dispositivo de saída** e por **canal** (L/R/surround).
Fica na bandeja do sistema. Interface em português e inglês.

O plano original está em [PLANO_LOUDMIXER.md](PLANO_LOUDMIXER.md). Este
README descreve o que foi implementado, como compilar e o que ainda falta.

---

## Estado da implementação

| Fase do plano | Estado |
|---|---|
| **0 — Fundação** | ✅ CMake multiplataforma, `IAudioBackend`, `MockBackend`, `GainProcessor` + testes, esqueleto Qt com bandeja |
| **1 — Linux MVP** | ✅ App / dispositivo / canal até 200%, rota de boost DSP com soft-clip, hot-plug, reconexão. Script de AppImage (não executado aqui) |
| **2 — Windows MVP** | ✅ WASAPI (sessões, dispositivo, canais) compilado e testado no Windows real; instalador NSIS gerado. Boost > 100% por efeito de sistema (APO) próprio, testado no Windows real; cabo virtual como alternativa |
| **3 — UX** | ✅ Presets, auto-start, LED de clipping, atalhos, i18n PT-BR + EN |
| **4 — Estabilidade** | ✅ Testes com ASan/UBSan/TSan, hot-plug no mock, encerramento limpo. ⚠️ Suspender/hibernar não testado |

### O que foi verificado de verdade

- **DSP (`GainProcessor`)**: 20 testes unitários (transparência em 100%,
  linearidade abaixo do joelho, saída nunca acima de ±1.0, curvas monotônicas
  e contínuas, rampa sem "zipper", NaN, multicanal, medição, concorrência).
  Limpo sob ASan, UBSan e TSan.
- **Núcleo Qt** (`AudioSessionManager`, `PresetStore`, `AutoStartService`):
  14 testes com o `MockBackend`.
- **Backend Pulse contra um servidor real** (PulseAudio do WSLg): ganho de
  dispositivo > 100%, por canal, mute, detecção de um app novo e boost dele a
  200%, e a rota DSP processando áudio de verdade (≈43 mil amostras,
  clipping detectado, pico de saída 0,9999). Esses testes restauram o estado
  original ao final e rodam limpos sob ASan/UBSan.
- **App completo** no Linux: compila com `-Werror`, sobe contra o Pulse real,
  instância única e comandos (`--volume-up`, `--quit`…) funcionando, UI
  renderizada e conferida nas três abas, em inglês e português.
- **Windows (MSYS2 UCRT64: GCC 16, Qt 6.12)**: o app inteiro compila com
  `-Werror`, os 33 testes passam, e o app roda no Windows real. Instância
  única e `--quit` funcionam, e `--volume-down` / `--volume-up` mudaram de
  fato o volume do dispositivo padrão (63% → 58% → 63%). O instalador NSIS e
  o ZIP são gerados, e a versão instalada roda sem o MSYS2 no PATH.
- **APO no Windows 11 real** (fone Bluetooth): o Windows carrega a DLL não
  assinada; a escolha automática de estágio caiu no MFX depois de o EFX não
  ser montado; em 200% o som fica mais alto, com o soft-clip segurando os
  picos. O `WasapiApoLiveTest` confirmou dispositivo, canal e app chegando ao
  APO, o limite de 200% e o estado salvo no registro sem administrador. O
  `uninstall` restaurou o dispositivo exatamente como estava.
- **Cabo virtual** (VB-Audio Cable): testado manualmente no Windows real.

### O que NÃO foi verificado

- Build com **MSVC** (o toolchain do Windows é o MinGW do MSYS2).
- A janela do mixer no Windows não foi conferida visualmente: o app foi
  testado minimizado na bandeja e pelos comandos.
- O instalador NSIS foi gerado, mas não executado.
- O CI (`.github/workflows/ci.yml`) ainda não rodou no GitHub.
- **APO** num alto-falante com efeitos do fabricante no estágio EFX (só foi
  testado num fone Bluetooth, onde ele roda no estágio MFX), em DAC USB e HDMI,
  e com conteúdo protegido por DRM.
- Registro do APO a partir da pasta de instalação (`Program Files`), que só o
  instalador exercita.
- **Backend PipeWire nativo**: compila, mas o ambiente de desenvolvimento não
  tinha daemon PipeWire. Só o caminho de erro foi testado.
- Reconexão automática do Pulse após o servidor reiniciar.
- Geração de AppImage, Flatpak e instalador NSIS (scripts prontos, não executados).
- Suspender/hibernar.

---

## Como funciona

```
UI (Qt Widgets) ── TrayController · MixerWindow · ChannelsPanel · GainSlider
        │  sinais/slots
Núcleo  ── AudioSessionManager · GainProcessor (DSP) · PresetStore · AutoStartService
        │  IAudioBackend
Backends ── PulseBackend (+ PulseBoostRoute) │ PipeWireBackend │ WasapiBackend (+ APO · LoopbackCapture) │ MockBackend
```

### Linux

O backend padrão usa a **libpulse**, que funciona tanto com o PulseAudio quanto
com o **PipeWire** (via `pipewire-pulse`, presente em praticamente toda distro
com PipeWire). Ganho acima de 100% é nativo:

- **App** → volume do sink-input
- **Dispositivo** → volume do sink (escala os canais preservando o balanço)
- **Canal** → valor individual do volume do sink

**Boost DSP** (checkbox por dispositivo): cria um sink virtual, captura o que
toca nele, passa pelo `GainProcessor` (soft-clip + LED de clipping) e reproduz
no dispositivo real. É a forma de ter boost **sem distorção dura**, já que o
ganho nativo do servidor só faz clip seco. Adiciona ~40 ms de latência.

### Windows

As APIs de volume do Windows param em 100% (sessão via `ISimpleAudioVolume`,
dispositivo e canais via `IAudioEndpointVolume`). Para passar disso, o
LoudMixer tem um **efeito de sistema (APO)** próprio, que o Windows carrega na
cadeia de efeitos do dispositivo e que aplica o `GainProcessor` (soft-clip +
medição) à mistura dos apps:

- **Dispositivo e canais:** até 100% no volume do Windows, acima disso no APO.
- **Apps:** o APO sobe pelo maior ganho pedido (B) e cada app é atenuado no
  volume nativo para pedido/B.
- O ganho fica salvo e o APO o reaplica quando o Windows o recria (reboot,
  reconexão), mesmo com o app fechado.

Por enquanto o APO é ativado com a ferramenta `loudmixer-apo-setup`. Onde ele
não funciona, há a alternativa do **cabo virtual** (VB-Audio Cable): os apps
tocam no cabo, o LoudMixer captura em loopback, aplica o DSP e reinjeta no
dispositivo físico. Passo a passo em
[packaging/windows/README.md](packaging/windows/README.md).

### Escala de ganho

O ganho é **amplitude linear**: 100% = 0 dB, 200% = **+6 dB**. O pavucontrol
usa escala cúbica, então o nosso "200%" aparece lá como ~126%.

---

## Compilar

> **WSL ≠ Windows.** Compilado dentro do WSL, o LoudMixer é a versão
> **Linux**: ele controla o servidor de áudio do WSLg (dispositivo
> `RDPSink`), por onde passa só o som de programas Linux do WSL. O volume do
> Windows e dos apps do Windows só é controlado pela versão **Windows**
> (abaixo).

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

Pacotes: `cd build && cpack -G DEB` · AppImage: `packaging/linux/build-appimage.sh`
· Flatpak: `packaging/linux/flatpak/org.loudmixer.LoudMixer.yml`.

### Windows

Usa o **MSYS2 UCRT64** (GCC + Qt 6 + CMake + Ninja). Resumo:

1. Instale o MSYS2 em `C:\msys64` e, no terminal UCRT64:
   `pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,gdb,cmake,ninja,qt6-base,qt6-tools,qt6-translations,gtest,nsis}`
2. Ponha `C:\msys64\ucrt64\bin` no PATH do usuário e reabra o VS Code.
3. Abra a pasta do projeto numa janela **local** do VS Code (se o código
   estiver no WSL: `\\wsl.localhost\<distro>\...`), escolha o preset
   **Windows (MinGW) · Debug** e aperte **F7**. **F5** depura.

Passo a passo, instalador NSIS e detalhes em
[packaging/windows/README.md](packaging/windows/README.md).

### Opções do CMake

| Opção | Padrão | |
|---|---|---|
| `LOUDMIXER_BUILD_APP` | ON | OFF compila só núcleo + testes (sem Qt Widgets) |
| `LOUDMIXER_BUILD_TESTS` | ON | GoogleTest do sistema ou baixado via FetchContent |
| `LOUDMIXER_WITH_PULSE` | ON | backend libpulse |
| `LOUDMIXER_WITH_PIPEWIRE` | ON | backend PipeWire nativo (se a lib existir) |
| `LOUDMIXER_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |

---

## Uso

- **Clique esquerdo** na bandeja abre/esconde o mixer; **clique direito** dá
  acesso a presets, *Silenciar tudo*, *Resetar ganhos*, *Iniciar com o
  sistema*, configurações e sair. Fechar a janela só a esconde.
- Duplo clique num slider (ou ⟲) volta a 100%.
- **Presets** salvam ganhos de apps (pelo nome do app), dispositivos e canais.

### Linha de comando e atalhos

```
loudmixer [--minimized] [--backend auto|pulse|pipewire|mock]
loudmixer --volume-up | --volume-down | --mute | --mute-all | --toggle | --quit
```

Os comandos da segunda linha são enviados à instância que já está rodando.
- **Windows**: atalhos globais **Ctrl+Alt+↑ / ↓ / M** (configuráveis on/off).
- **Linux**: não há API de atalho global portátil (o Wayland proíbe por
  design). Associe os comandos acima a teclas nas configurações de teclado do
  seu ambiente gráfico.

`--backend mock` abre a UI com dados fictícios, útil para desenvolver sem áudio.

### Testes

```bash
ctest --test-dir build --output-on-failure            # unitários
LOUDMIXER_LIVE_TESTS=1 ./build/tests/PulseBackendLiveTest   # contra o servidor real*
```
\*Altera volumes temporariamente e os restaura no final. Silencia o
dispositivo durante o teste de boost para não tocar um bipe alto.

---

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
| Soft-clip em todo boost | Soft-clip só na **rota DSP** | O ganho nativo do Pulse/PipeWire > 100% é aplicado no servidor, fora do nosso alcance |

## Limitações conhecidas / próximos passos

- **Windows (APO)**: ainda é ativado pela linha de comando, como
  administrador. O total por dispositivo é limitado a 200% (dispositivo × app).
  Apps em modo RAW não passam pelo APO. Riscos acompanhados nas issues com a
  label `risco`.
- **Windows (cabo virtual)**: uma rota por vez; exige trocar a saída padrão
  manualmente (trocar por código exige a API não documentada `IPolicyConfig`).
- **Windows**: volumes e sessões são lidos por varredura a cada 750 ms (mais
  notificações de hot-plug).
- **Rota DSP no Linux**: se os relógios do sink virtual e do físico
  divergirem, o excesso é descartado para não acumular latência, o que pode
  gerar um estalo ocasional. Um resampler adaptativo resolveria.
- **PipeWire nativo**: ajusta o volume do nó, não o Route do dispositivo, e
  não tem rota DSP nem reconexão.
- **Flatpak**: *Iniciar com o sistema* não funciona dentro do sandbox (falta
  usar o portal Background).
- Projeto ainda sem arquivo de licença: falta escolher uma (o Qt é usado sob
  LGPL, com link dinâmico).

## Estrutura

```
src/core/            IAudioBackend, GainProcessor (DSP puro), AudioSessionManager, PresetStore, AutoStartService
src/backend/linux/   PulseBackend, PulseBoostRoute, PipeWireBackend
src/backend/windows/ WasapiBackend, ApoRouteMath.h, SessionEnumerator, LoopbackCapture, ComUtil.h
src/apo/             APO (loudmixer_apo.dll), loudmixer-apo-setup, interfaces e estado compartilhado
src/backend/mock/    MockBackend
src/ui/              TrayController, MixerWindow, ChannelsPanel, GainSlider, SettingsDialog, SingleInstance, GlobalHotkeys, AppIcons
tests/               GainProcessorTest, MockBackendTest, ApoRouteMathTest, PulseBackendLiveTest; no Windows: ApoTest, WasapiApoLiveTest
translations/        loudmixer_pt_BR.ts  (tools/fill_pt_BR.py aplica as traduções)
resources/           ícones (tools/make_icons.py gera .ico/.png), .qrc, .rc
packaging/           linux/ (AppImage, .desktop, Flatpak) · windows/ (NSIS via CPack)
```
