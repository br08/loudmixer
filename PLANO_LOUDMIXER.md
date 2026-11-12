# Plano do Projeto — Volume Booster Cross-Platform

## 1. Visão geral

App desktop **C++/Qt** para Windows e Linux que permite amplificar o volume de áudio **acima de 100%** (até 200% com soft-clipping), com controle granular em três níveis:

- **Por aplicativo** (sessão de áudio: Chrome, Spotify, etc.)
- **Por dispositivo de saída** (cada caixa/fone físico)
- **Por canal físico** (L/R/surround dentro de um dispositivo)

Funciona minimizado na bandeja do sistema (tray icon) com acesso rápido aos controles.

**Nome sugerido:** `LoudMixer` (pode trocar).

---

## 2. Desafio técnico central

Amplificar acima de 100% **não é apenas mexer no slider do sistema** — o SO limita o ganho em 1.0 (0 dBFS). Para ir além, é preciso:

1. **Interceptar/capturar** o stream PCM de cada fonte.
2. **Multiplicar as amostras** por um ganho > 1.0 em ponto flutuante.
3. **Aplicar soft-clipping** (ex.: `tanh` ou curva cúbica) para evitar distorção agressiva quando o resultado ultrapassa ±1.0.
4. **Devolver o áudio processado** ao dispositivo de saída.

O ponto onde essa interceptação acontece é **completamente diferente em cada SO** — o núcleo do projeto é isolar essa diferença atrás de uma abstração limpa.

---

## 3. Arquitetura

```
┌─────────────────────────────────────────────────────┐
│           UI Layer (Qt Widgets + QML)              │
│   - Tray icon + menu                                │
│   - Janela principal (mixer)                        │
│   - Sliders por app / dispositivo / canal           │
└──────────────────────┬──────────────────────────────┘
                       │  Qt signals/slots
┌──────────────────────▼──────────────────────────────┐
│            Core (audio-agnóstico)                   │
│   - AudioSessionManager                             │
│   - GainProcessor (soft-clipping DSP)               │
│   - PresetStore (QSettings)                         │
│   - AutoStartService                                │
└──────────────────────┬──────────────────────────────┘
                       │  interface IAudioBackend
       ┌───────────────┴────────────────┐
       │                                │
┌──────▼──────────┐              ┌──────▼──────────┐
│ Windows Backend │              │  Linux Backend  │
│  (WASAPI)       │              │  (PipeWire)     │
└─────────────────┘              └─────────────────┘
```

### 3.1. Interface `IAudioBackend`
Contrato comum expondo:
- `listDevices()`, `listSessions()`, `listChannels(deviceId)`
- `setGain(target, gainMultiplier)` — target é app | device | channel
- `subscribeToChanges(callback)` — hot-plug, apps abrindo/fechando
- `attachProcessor(target, processor)` — insere o DSP na cadeia

### 3.2. `GainProcessor` (DSP compartilhado)
Módulo puro C++ (sem dependência de Qt) que:
- Recebe buffer PCM float32
- Aplica ganho linear (0.0 – 2.0)
- Aplica soft-clip: `y = tanh(x * gain) / tanh(gain)` (normalizado) ou variante cúbica para menor custo
- Processa em blocos alinhados para SIMD (opcional em v2)

---

## 4. Backend Windows

**Tecnologia:** WASAPI (Windows Audio Session API) + Core Audio APIs.

**Como conseguir cada escopo:**

| Escopo | Método |
|---|---|
| **Por aplicativo** | `IAudioSessionManager2` → enumerar sessões → `ISimpleAudioVolume` só chega a 1.0. Para ir além, usar **captura em modo loopback** da sessão específica + reinjeção. Alternativa mais limpa: **APO (Audio Processing Object)** registrado no endpoint, mas exige assinatura de driver (complicado). Solução prática **v1**: usar o slider por-sessão como pré-atenuador e aplicar o boost real no endpoint. |
| **Por dispositivo** | Captura loopback WASAPI do endpoint → processa → toca em modo exclusivo/compartilhado no mesmo endpoint via loop virtual (usar biblioteca **VB-Cable** ou driver próprio). Alternativa v1: usar **AudioEndpointVolume** para atenuar + boost por software num "virtual device" intermediário. |
| **Por canal** | WASAPI expõe `IChannelAudioVolume` (0.0–1.0). Boost >1.0 requer processar o buffer multicanal manualmente antes de mandar ao endpoint. |

**Ponto de atenção:** para boost real **acima de 100% no nível do sistema**, o caminho tecnicamente correto é um **APO** ou driver virtual. Para v1, aceitar a limitação de rotear o áudio por um endpoint virtual controlado pelo app.

**Bibliotecas:** WIL (Windows Implementation Library), WRL para COM.

---

## 5. Backend Linux

**Tecnologia:** **PipeWire** (primário) com fallback para **PulseAudio** via `libpulse` (compatibilidade com distros antigas).

**Como conseguir cada escopo:**

| Escopo | Método |
|---|---|
| **Por aplicativo** | PipeWire trata cada stream como um nó. Criar um **filter-chain node** por sink-input, com plugin de ganho customizado. `pw_stream` + `spa_node` com processamento in-place. Ganho > 1.0 é nativamente suportado (PulseAudio também: `pactl set-sink-input-volume ... 200%`). |
| **Por dispositivo** | Criar sink virtual (`module-null-sink` no PA / null-sink node no PW) → roteia todo áudio para lá → processa → envia para sink físico. Ou usar `filter-chain` direto no sink. |
| **Por canal** | PipeWire suporta ganho por canal nativamente via `channelmix`. Aplicar via propriedades do nó ou filter-chain com matriz de mix. |

**Bibliotecas:** `libpipewire-0.3`, `libpulse` (fallback), `libspa`.

**Vantagem no Linux:** ganho >100% é *primeira-classe* no PulseAudio/PipeWire — a implementação é muito mais simples que no Windows.

---

## 6. Interface do Usuário (Qt)

### 6.1. Tray icon
- Ícone permanente na bandeja (Qt `QSystemTrayIcon`)
- **Clique esquerdo:** abre/traz o mixer flutuante
- **Clique direito:** menu com presets, "Silenciar tudo", "Resetar ganhos", "Abrir configurações", "Sair"
- Ao fechar a janela: **minimiza para tray, não fecha o processo**
- Iniciar minimizado ao subir com o SO

### 6.2. Janela principal (mixer)
Três abas ou seções colapsáveis:
1. **Apps** — lista de sessões ativas com slider 0–200%, mute, ícone do app
2. **Dispositivos** — cada saída com slider mestre 0–200%
3. **Canais** — dispositivo selecionado → sliders L/R/etc.

Cada slider mostra:
- Valor numérico (%)
- Faixa 0–100% em azul, 100–200% em amarelo/vermelho (feedback visual de "zona de boost")
- Botão de reset (volta a 100%)
- Indicador visual quando soft-clipping está ativo (LED que acende quando amostras estão saturando)

### 6.3. Presets
Salvar/carregar combinações nomeadas (ex.: "Filme à noite", "Reunião").

---

## 7. Estrutura de diretórios

```
loudmixer/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── IAudioBackend.h
│   │   ├── GainProcessor.{h,cpp}
│   │   ├── AudioSessionManager.{h,cpp}
│   │   ├── PresetStore.{h,cpp}
│   │   └── AutoStartService.{h,cpp}
│   ├── backend/
│   │   ├── windows/
│   │   │   ├── WasapiBackend.{h,cpp}
│   │   │   ├── SessionEnumerator.{h,cpp}
│   │   │   └── LoopbackCapture.{h,cpp}
│   │   └── linux/
│   │       ├── PipeWireBackend.{h,cpp}
│   │       ├── PulseFallback.{h,cpp}
│   │       └── FilterChainNode.{h,cpp}
│   └── ui/
│       ├── TrayController.{h,cpp}
│       ├── MixerWindow.{h,cpp,ui}
│       └── qml/  (se optarmos por QML para os sliders)
├── tests/
│   ├── GainProcessorTest.cpp     # unit tests do DSP
│   └── MockBackendTest.cpp
├── resources/
│   ├── icons/
│   └── loudmixer.qrc
└── packaging/
    ├── windows/  (NSIS/MSIX)
    └── linux/    (AppImage, .deb, Flatpak)
```

---

## 8. Dependências

**Comum:**
- Qt 6.5+ (Core, Widgets, opcionalmente QML)
- CMake 3.24+
- Compilador C++20 (MSVC 2022 / GCC 11+ / Clang 14+)

**Windows:**
- Windows SDK 10.0.22621+ (para WASAPI e APO headers)
- WIL (header-only)

**Linux:**
- `libpipewire-0.3-dev`
- `libpulse-dev` (fallback)
- `libspa-0.2-dev`

**Testes:**
- Google Test ou Catch2

---

## 9. Fases de implementação

### **Fase 0 — Fundação** (~1 semana)
- Configurar CMake multiplataforma
- Esqueleto Qt com tray icon + janela vazia
- Definir `IAudioBackend` e stub `MockBackend`
- Escrever `GainProcessor` puro + testes unitários (crítico: cobrir bem, é o coração DSP)

### **Fase 1 — Linux MVP** (~2 semanas)
Priorizado por ser tecnicamente mais simples.
- `PipeWireBackend` — listar sinks/sink-inputs
- Ganho por app (usar API nativa PW até 200%)
- Ganho por dispositivo (via filter-chain)
- Ganho por canal
- Empacotamento AppImage para teste rápido

### **Fase 2 — Windows MVP** (~3-4 semanas)
- `WasapiBackend` — enumerar dispositivos e sessões
- Ganho por canal via `IChannelAudioVolume` + processamento manual acima de 1.0
- Loopback capture + reinjeção para ganho por dispositivo/app > 100%
- **Decisão importante nessa fase:** avaliar se APO/driver virtual é viável ou se aceitamos a solução via dispositivo virtual
- Instalador NSIS

### **Fase 3 — UX e polimento** (~2 semanas)
- Presets
- Auto-start com o SO
- Indicador de clipping em tempo real
- Atalhos globais (hotkeys) para mudar volume
- Traduções (i18n Qt) — PT-BR + EN

### **Fase 4 — Estabilidade** (~1 semana)
- Testes de hot-plug (fone conectando/desconectando)
- Testes de estabilidade prolongada (memory leaks, CPU)
- Comportamento em suspender/hibernar

---

## 10. Riscos técnicos e mitigações

| Risco | Impacto | Mitigação |
|---|---|---|
| **Windows: boost real >100% por app exige APO/driver** | Alto — o feature-chave depende disso | Aceitar limitação inicial usando dispositivo virtual; avaliar APO como v2 |
| **Latência adicional pela cadeia de processamento** | Médio — usuário percebe delay em jogos/vídeo | Usar buffers pequenos (~10ms), processamento em thread de alta prioridade |
| **Distorção mesmo com soft-clipping** | Baixo | Documentar; permitir usuário escolher curva (tanh vs. cúbica) |
| **PipeWire vs. PulseAudio: fragmentação de distros** | Médio | Detectar em runtime; fallback automático |
| **Qt licenciamento (LGPL requer link dinâmico ou compra)** | Baixo | Distribuir com Qt dinâmico + attribution |
| **Permissões: Windows exige elevação para APO / Linux exige grupo audio** | Médio | Documentar; oferecer wizard de primeira execução |

---

## 11. Fora de escopo (v1)

- macOS (Core Audio taps têm restrições recentes de segurança)
- Equalizador
- Efeitos além de ganho (reverb, compressor visível ao usuário)
- Remote control (network/mobile)
- Plugin VST/LV2

---

## 12. Próximos passos sugeridos

1. Aprovar/ajustar este plano.
2. Configurar o esqueleto do projeto (CMake + Qt + estrutura de pastas + CI básico).
3. Implementar Fase 0 (fundação + `GainProcessor` com testes) antes de mexer em qualquer backend real.
