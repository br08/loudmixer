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
| **2 — Windows MVP** | ✅ código completo (WASAPI, sessões, canais, loopback + reinjeção). ⚠️ Verificado só parcialmente (ver abaixo) |
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
- **Backend Windows**: compilado com MinGW-w64 (`-Wall -Wextra -Werror`) e
  **executado no Windows real** em modo leitura: lista os dispositivos, o
  padrão, os volumes e as sessões dos apps.

### O que NÃO foi verificado

- Build com **MSVC** e a **UI no Windows** (só o backend foi compilado, com
  MinGW). O CI (`.github/workflows/ci.yml`) cobre isso, mas ainda não rodou.
- **`LoopbackCapture`** (boost > 100% no Windows): precisa do VB-Cable
  instalado; não foi executado.
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
Backends ── PulseBackend (+ PulseBoostRoute) │ PipeWireBackend │ WasapiBackend (+ LoopbackCapture) │ MockBackend
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

As APIs de volume do Windows param em 100%. Sem rota DSP, os sliders vão até
100% (sessão via `ISimpleAudioVolume`, dispositivo e canais via
`IAudioEndpointVolume`). Para passar disso, o LoudMixer usa a solução v1 do
plano: os apps tocam num **cabo virtual** (VB-Audio Cable), o LoudMixer
captura em loopback, aplica o DSP e reinjeta no dispositivo físico. Com a rota
ativa, o volume das sessões vira pré-atenuador e o ganho real é aplicado no
DSP. Passo a passo em [packaging/windows/README.md](packaging/windows/README.md).

### Escala de ganho

O ganho é **amplitude linear**: 100% = 0 dB, 200% = **+6 dB**. O pavucontrol
usa escala cúbica, então o nosso "200%" aparece lá como ~126%.

---

## Compilar

### Linux (Debian 12 / Ubuntu 22.04+)

```bash
sudo apt install cmake ninja-build pkg-config g++ \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
    libpulse-dev libpipewire-0.3-dev libspa-0.2-dev libgtest-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/loudmixer
```

Pacotes: `cd build && cpack -G DEB` · AppImage: `packaging/linux/build-appimage.sh`
· Flatpak: `packaging/linux/flatpak/org.loudmixer.LoudMixer.yml`.

### Windows

Visual Studio 2022 + Qt 6.5 (MSVC) + CMake + Ninja. Instruções e instalador
NSIS em [packaging/windows/README.md](packaging/windows/README.md).

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
| WIL / WRL | `ComUtil.h` próprio | Compila também com MinGW (CI e cross-compile a partir do Linux) |
| Qt 6.5+ | Qt **6.4+** | Debian 12 traz 6.4.2; nenhuma API usada exige 6.5 |
| `MixerWindow.ui` / QML | Qt Widgets em código | As linhas são dinâmicas (apps aparecem/somem); o slider é desenhado à mão |
| Soft-clip em todo boost | Soft-clip só na **rota DSP** | O ganho nativo do Pulse/PipeWire > 100% é aplicado no servidor, fora do nosso alcance |

## Limitações conhecidas / próximos passos

- **Windows**: uma rota de boost por vez; exige um cabo virtual e trocar a
  saída padrão manualmente (trocar por código exige a API não documentada
  `IPolicyConfig`). Volumes e sessões são lidos por varredura a cada 750 ms
  (mais notificações de hot-plug). APO assinado fica para a v2, como previsto.
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
src/backend/windows/ WasapiBackend, SessionEnumerator, LoopbackCapture, ComUtil.h
src/backend/mock/    MockBackend
src/ui/              TrayController, MixerWindow, ChannelsPanel, GainSlider, SettingsDialog, SingleInstance, GlobalHotkeys, AppIcons
tests/               GainProcessorTest, MockBackendTest, PulseBackendLiveTest
translations/        loudmixer_pt_BR.ts  (tools/fill_pt_BR.py aplica as traduções)
resources/           ícones (tools/make_icons.py gera .ico/.png), .qrc, .rc
packaging/           linux/ (AppImage, .desktop, Flatpak) · windows/ (NSIS via CPack)
```
