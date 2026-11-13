# LoudMixer

Amplificador de volume para **Linux** e **Windows**: leva o áudio **acima de
100%** (até 200%, com soft-clipping) com controle por **aplicativo**, por
**dispositivo de saída** e por **canal** (L/R/surround). Fica na bandeja do
sistema. Interface em português e inglês.

## Instalação

Baixe a versão mais recente na página de
[Releases](https://github.com/br08/loudmixer/releases/latest).

### Windows (64 bits)

- **Instalador:** `loudmixer-<versão>-win64.exe`. Instalar uma versão nova
  por cima da antiga mantém as configurações e o boost ligado.
- **Portátil:** `loudmixer-<versão>-win64.zip`. Descompacte e rode o
  `loudmixer.exe` da pasta `bin`.

O instalador não é assinado digitalmente. Se o Windows SmartScreen avisar na
primeira execução, clique em *Mais informações → Executar assim mesmo*.

Para desinstalar, use *Configurações > Aplicativos* do Windows. A
desinstalação tira o boost de todos os dispositivos e restaura a configuração
de áudio original.

### Linux (Debian 12 / Ubuntu 22.04 ou mais novos)

```bash
sudo apt install ./loudmixer_<versão>_amd64.deb
```

Funciona com o **PulseAudio** e com o **PipeWire** (via `pipewire-pulse`,
presente em praticamente toda distro com PipeWire). Para outras distros,
[compile a partir do código](docs/DESENVOLVIMENTO.md#compilar).

## Uso

- **Clique esquerdo** no ícone da bandeja abre/esconde o mixer; **clique
  direito** dá acesso a presets, *Silenciar tudo*, *Resetar ganhos*, *Iniciar
  com o sistema*, configurações e sair. Fechar a janela só a esconde.
- O mixer tem uma aba para os **aplicativos** que estão tocando, uma para os
  **dispositivos** de saída e uma para os **canais** de cada dispositivo.
- Duplo clique num slider (ou ⟲) volta a 100%.
- O **LED** acende quando o soft-clip está segurando picos (sinal de que o
  volume está no limite).
- **Presets** salvam os ganhos de apps (pelo nome do app), dispositivos e
  canais.

### Boost acima de 100% no Windows

O volume do Windows para em 100%. Para passar disso, ative o boost no
dispositivo: aba **Dispositivos**, botão **Ligar boost**. O Windows pede
permissão de administrador e o áudio para por alguns segundos enquanto é
reiniciado. Depois disso, os sliders do dispositivo, dos canais e dos apps
que tocam nele vão até 200%, e o ganho continua valendo mesmo com o LoudMixer
fechado.

| Botão | Significado |
|---|---|
| **Ligar boost** | disponível; clique para ativar |
| **Boost ligado** | ativo; clique para desativar |
| **Boost pausado** | os aprimoramentos de áudio do dispositivo foram desligados nas configurações do Windows; clique para religar |
| **Boost removido** | uma atualização de driver ou do Windows tirou o boost do dispositivo; clique para religar |

Se o boost não funcionar no seu dispositivo, há a alternativa do **cabo
virtual** (VB-Audio Cable), em *Configurações > Avançado*. Passo a passo e
detalhes em [packaging/windows/README.md](packaging/windows/README.md#boost-acima-de-100-no-windows).

### Boost acima de 100% no Linux

Os sliders vão até 200% direto, pelo próprio servidor de áudio. Nesse modo,
picos acima do limite são cortados secos e podem distorcer. Para ter o
soft-clip (e o LED), marque **Boost DSP** no dispositivo: o áudio passa a ser
processado pelo LoudMixer, com ~40 ms de latência a mais.

### Escala de ganho

O ganho é **amplitude linear**: 100% = 0 dB, 200% = **+6 dB**. O pavucontrol
usa escala cúbica, então o "200%" do LoudMixer aparece lá como ~126%.

### Linha de comando e atalhos

```
loudmixer [--minimized] [--backend auto|pulse|pipewire|wasapi]
loudmixer --volume-up | --volume-down | --mute | --mute-all | --toggle | --quit
```

Os comandos da segunda linha são enviados ao LoudMixer que já está rodando.

- **Windows**: atalhos globais **Ctrl+Alt+↑ / ↓ / M** (podem ser desligados
  nas configurações).
- **Linux**: não há atalho global portátil (o Wayland não permite). Associe
  os comandos acima a teclas nas configurações de teclado do seu ambiente
  gráfico.

## Limitações conhecidas

- **Windows**: ligar e desligar o boost pede permissão de administrador e
  reinicia o áudio do sistema por alguns segundos. O total por dispositivo é
  limitado a 200% (dispositivo × app). Apps que tocam em modo RAW não passam
  pelo boost.
- **Windows (cabo virtual)**: uma rota por vez, e a saída padrão do Windows
  precisa ser trocada manualmente para o cabo.
- **Linux (Boost DSP)**: pode haver um estalo ocasional.
- **Linux (backend PipeWire nativo, experimental)**: sem Boost DSP. O backend
  padrão (libpulse) já funciona com PipeWire.
- **Flatpak**: *Iniciar com o sistema* não funciona dentro do sandbox.
- Suspender/hibernar ainda não foi testado.

## Desenvolvimento

Como compilar, testar, arquitetura, estado da implementação e como publicar
uma release: [docs/DESENVOLVIMENTO.md](docs/DESENVOLVIMENTO.md).

## Licença

O projeto ainda não tem uma licença definida.
