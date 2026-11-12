<?xml version='1.0' encoding='utf-8'?>
<!DOCTYPE TS>
<TS version="2.1" language="pt_BR">
<context>
    <name>backend</name>
    <message>
        <source>Could not start the PipeWire event loop.</source>
        <translation>Não foi possível iniciar o loop de eventos do PipeWire.</translation>
    </message>
    <message>
        <source>Could not connect to the PipeWire daemon.</source>
        <translation>Não foi possível conectar ao daemon do PipeWire.</translation>
    </message>
    <message>
        <source>PipeWire did not respond in time.</source>
        <translation>O PipeWire não respondeu a tempo.</translation>
    </message>
    <message>
        <source>Could not connect to the audio server</source>
        <translation>Não foi possível conectar ao servidor de áudio</translation>
    </message>
    <message>
        <source>The boost route can only be applied to devices.</source>
        <translation>O Boost DSP só pode ser aplicado a dispositivos.</translation>
    </message>
    <message>
        <source>The audio backend is not running.</source>
        <translation>O backend de áudio não está em execução.</translation>
    </message>
    <message>
        <source>Not connected to the audio server.</source>
        <translation>Sem conexão com o servidor de áudio.</translation>
    </message>
    <message>
        <source>DSP boost is already active on this device.</source>
        <translation>O Boost DSP já está ativo neste dispositivo.</translation>
    </message>
    <message>
        <source>Device not found.</source>
        <translation>Dispositivo não encontrado.</translation>
    </message>
    <message>
        <source>Could not open audio stream</source>
        <translation>Não foi possível abrir o stream de áudio</translation>
    </message>
    <message>
        <source>Could not load module-null-sink</source>
        <translation>Não foi possível carregar o module-null-sink</translation>
    </message>
    <message>
        <source>The audio server refused to create the virtual device (module-null-sink).</source>
        <translation>O servidor de áudio recusou criar o dispositivo virtual (module-null-sink).</translation>
    </message>
    <message>
        <source>Could not create audio streams.</source>
        <translation>Não foi possível criar os streams de áudio.</translation>
    </message>
    <message>
        <source>Could not open the boost source device</source>
        <translation>Não foi possível abrir o dispositivo de origem do boost</translation>
    </message>
    <message>
        <source>Could not open the output device</source>
        <translation>Não foi possível abrir o dispositivo de saída</translation>
    </message>
    <message>
        <source>The boost source device does not use a float32 mix format (not supported in v1).</source>
        <translation>O dispositivo de origem do boost não usa formato de mixagem float32 (não suportado na v1).</translation>
    </message>
    <message>
        <source>Could not access the Windows audio devices (IMMDeviceEnumerator).</source>
        <translation>Não foi possível acessar os dispositivos de áudio do Windows (IMMDeviceEnumerator).</translation>
    </message>
    <message>
        <source>DSP boost is already active on another device (v1 supports one at a time).</source>
        <translation>O Boost DSP já está ativo em outro dispositivo (a v1 suporta um por vez).</translation>
    </message>
    <message>
        <source>No virtual output device found. Install VB-Audio Virtual Cable (free), set "CABLE Input" as the Windows default output and enable DSP boost again.</source>
        <translation>Nenhum dispositivo de saída virtual encontrado. Instale o VB-Audio Virtual Cable (gratuito), defina "CABLE Input" como saída padrão do Windows e ative o Boost DSP novamente.</translation>
    </message>
    <message>
        <source>This audio backend does not support DSP processing.</source>
        <translation>Este backend de áudio não suporta processamento DSP.</translation>
    </message>
</context>
<context>
    <name>loudmixer::AudioSessionManager</name>
    <message>
        <source>No audio backend available.</source>
        <translation>Nenhum backend de áudio disponível.</translation>
    </message>
    <message>
        <source>This audio backend does not support DSP boost.</source>
        <translation>Este backend de áudio não suporta Boost DSP.</translation>
    </message>
</context>
<context>
    <name>loudmixer::AutoStartService</name>
    <message>
        <source>Could not write to the Windows registry.</source>
        <translation>Não foi possível gravar no registro do Windows.</translation>
    </message>
    <message>
        <source>Could not remove %1</source>
        <translation>Não foi possível remover %1</translation>
    </message>
    <message>
        <source>Could not create %1</source>
        <translation>Não foi possível criar %1</translation>
    </message>
</context>
<context>
    <name>loudmixer::ui::ChannelsPanel</name>
    <message>
        <source>No output devices.</source>
        <translation>Nenhum dispositivo de saída.</translation>
    </message>
    <message>
        <source>Device:</source>
        <translation>Dispositivo:</translation>
    </message>
</context>
<context>
    <name>loudmixer::ui::ClipLed</name>
    <message>
        <source>Clipping indicator: lights up red while the soft-clipper is limiting peaks</source>
        <translation>Indicador de clipping: acende em vermelho enquanto o soft-clip está limitando os picos</translation>
    </message>
</context>
<context>
    <name>loudmixer::ui::GainSlider</name>
    <message>
        <source>Reset to 100%</source>
        <translation>Voltar para 100%</translation>
    </message>
    <message>
        <source>Boost above 100% is not available for this item right now</source>
        <translation>O boost acima de 100% não está disponível para este item no momento</translation>
    </message>
    <message>
        <source>Unmute</source>
        <translation>Reativar som</translation>
    </message>
    <message>
        <source>Mute</source>
        <translation>Silenciar</translation>
    </message>
</context>
<context>
    <name>loudmixer::ui::MixerWindow</name>
    <message>
        <source>No application is playing audio right now.</source>
        <translation>Nenhum aplicativo está tocando áudio no momento.</translation>
    </message>
    <message>
        <source>Applications</source>
        <translation>Aplicativos</translation>
    </message>
    <message>
        <source>No output devices.</source>
        <translation>Nenhum dispositivo de saída.</translation>
    </message>
    <message>
        <source>Devices</source>
        <translation>Dispositivos</translation>
    </message>
    <message>
        <source>Channels</source>
        <translation>Canais</translation>
    </message>
    <message>
        <source>Apply a saved preset</source>
        <translation>Aplicar um preset salvo</translation>
    </message>
    <message>
        <source>Save…</source>
        <translation>Salvar…</translation>
    </message>
    <message>
        <source>Save the current gains as a preset</source>
        <translation>Salvar os ganhos atuais como preset</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>Excluir</translation>
    </message>
    <message>
        <source>Delete the selected preset</source>
        <translation>Excluir o preset selecionado</translation>
    </message>
    <message>
        <source>Mute all</source>
        <translation>Silenciar tudo</translation>
    </message>
    <message>
        <source>Reset all</source>
        <translation>Resetar tudo</translation>
    </message>
    <message>
        <source>Set every app, device and channel back to 100%</source>
        <translation>Voltar todos os apps, dispositivos e canais para 100%</translation>
    </message>
    <message>
        <source>Settings</source>
        <translation>Configurações</translation>
    </message>
    <message>
        <source>
PID %1</source>
        <translation>
PID %1</translation>
    </message>
    <message>
        <source>
Output: %1</source>
        <translation>
Saída: %1</translation>
    </message>
    <message>
        <source>DSP boost</source>
        <translation>Boost DSP</translation>
    </message>
    <message>
        <source>Route this device through LoudMixer's DSP: real boost up to 200% with soft-clipping and a clipping indicator (adds a few milliseconds of latency)</source>
        <translation>Passa o áudio deste dispositivo pelo DSP do LoudMixer: boost real até 200% com soft-clip e indicador de clipping (adiciona alguns milissegundos de latência)</translation>
    </message>
    <message>
        <source>%1
Default output device</source>
        <translation>%1
Dispositivo de saída padrão</translation>
    </message>
    <message>
        <source>Audio backend: %1</source>
        <translation>Backend de áudio: %1</translation>
    </message>
    <message>
        <source> — enable “DSP boost” on a device to go above 100%</source>
        <translation> — ative o “Boost DSP” em um dispositivo para passar de 100%</translation>
    </message>
    <message>
        <source>Could not change DSP boost:
%1</source>
        <translation>Não foi possível alterar o Boost DSP:
%1</translation>
    </message>
    <message>
        <source>Presets…</source>
        <translation>Presets…</translation>
    </message>
    <message>
        <source>Save preset</source>
        <translation>Salvar preset</translation>
    </message>
    <message>
        <source>Preset name (e.g. “Movie night”):</source>
        <translation>Nome do preset (ex.: “Filme à noite”):</translation>
    </message>
    <message>
        <source>Replace the existing preset “%1”?</source>
        <translation>Substituir o preset “%1” existente?</translation>
    </message>
    <message>
        <source>Delete preset</source>
        <translation>Excluir preset</translation>
    </message>
    <message>
        <source>Delete the preset “%1”?</source>
        <translation>Excluir o preset “%1”?</translation>
    </message>
</context>
<context>
    <name>loudmixer::ui::SettingsDialog</name>
    <message>
        <source>Start LoudMixer when I log in</source>
        <translation>Iniciar o LoudMixer ao entrar no sistema</translation>
    </message>
    <message>
        <source>Start minimized to the system tray</source>
        <translation>Iniciar minimizado na bandeja do sistema</translation>
    </message>
    <message>
        <source>Enable global hotkeys (Ctrl+Alt+↑ / ↓ / M)</source>
        <translation>Ativar atalhos globais (Ctrl+Alt+↑ / ↓ / M)</translation>
    </message>
    <message>
        <source>LoudMixer Settings</source>
        <translation>Configurações do LoudMixer</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation>Áudio</translation>
    </message>
    <message>
        <source>Soft (tanh) — recommended</source>
        <translation>Suave (tanh) — recomendado</translation>
    </message>
    <message>
        <source>Soft (cubic) — lighter on CPU</source>
        <translation>Suave (cúbica) — mais leve para a CPU</translation>
    </message>
    <message>
        <source>Hard clip</source>
        <translation>Corte seco (hard clip)</translation>
    </message>
    <message>
        <source>How peaks above 0 dBFS are limited when DSP boost is active</source>
        <translation>Como os picos acima de 0 dBFS são limitados quando o Boost DSP está ativo</translation>
    </message>
    <message>
        <source>Clipping curve:</source>
        <translation>Curva de clipping:</translation>
    </message>
    <message>
        <source>auto (CABLE Input, VoiceMeeter, ...)</source>
        <translation>automático (CABLE Input, VoiceMeeter, ...)</translation>
    </message>
    <message>
        <source>Virtual output device whose audio is captured and boosted</source>
        <translation>Dispositivo de saída virtual cujo áudio é capturado e amplificado</translation>
    </message>
    <message>
        <source>Boost source device:</source>
        <translation>Dispositivo de origem do boost:</translation>
    </message>
    <message>
        <source>Startup</source>
        <translation>Inicialização</translation>
    </message>
    <message>
        <source>Hotkeys</source>
        <translation>Atalhos</translation>
    </message>
    <message>
        <source>Volume step:</source>
        <translation>Passo de volume:</translation>
    </message>
    <message>
        <source>Bind these commands to keys in your desktop's keyboard settings:
  loudmixer --volume-up
  loudmixer --volume-down
  loudmixer --mute
  loudmixer --toggle</source>
        <translation>Associe estes comandos a teclas nas configurações de teclado do seu ambiente gráfico:
  loudmixer --volume-up
  loudmixer --volume-down
  loudmixer --mute
  loudmixer --toggle</translation>
    </message>
    <message>
        <source>Language</source>
        <translation>Idioma</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>Padrão do sistema</translation>
    </message>
    <message>
        <source>Interface language:</source>
        <translation>Idioma da interface:</translation>
    </message>
    <message>
        <source>The new language will be used after restarting LoudMixer.</source>
        <translation>O novo idioma será usado depois de reiniciar o LoudMixer.</translation>
    </message>
    <message>
        <source>Could not change the startup setting:
%1</source>
        <translation>Não foi possível alterar a inicialização automática:
%1</translation>
    </message>
</context>
<context>
    <name>loudmixer::ui::TrayController</name>
    <message>
        <source>Open mixer</source>
        <translation>Abrir mixer</translation>
    </message>
    <message>
        <source>Presets</source>
        <translation>Presets</translation>
    </message>
    <message>
        <source>Mute all</source>
        <translation>Silenciar tudo</translation>
    </message>
    <message>
        <source>Reset all gains</source>
        <translation>Resetar todos os ganhos</translation>
    </message>
    <message>
        <source>Start with the system</source>
        <translation>Iniciar com o sistema</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>Configurações…</translation>
    </message>
    <message>
        <source>Quit</source>
        <translation>Sair</translation>
    </message>
    <message>
        <source> (muted)</source>
        <translation> (mudo)</translation>
    </message>
    <message>
        <source>(no presets saved)</source>
        <translation>(nenhum preset salvo)</translation>
    </message>
    <message>
        <source>Save current as preset…</source>
        <translation>Salvar estado atual como preset…</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Volume booster per app, device and channel</source>
        <translation>Amplificador de volume por aplicativo, dispositivo e canal</translation>
    </message>
    <message>
        <source>Start hidden in the system tray.</source>
        <translation>Iniciar oculto na bandeja do sistema.</translation>
    </message>
    <message>
        <source>Audio backend: auto, %1.</source>
        <translation>Backend de áudio: auto, %1.</translation>
    </message>
    <message>
        <source>Raise the default device volume (running instance).</source>
        <translation>Aumenta o volume do dispositivo padrão (instância em execução).</translation>
    </message>
    <message>
        <source>Lower the default device volume (running instance).</source>
        <translation>Diminui o volume do dispositivo padrão (instância em execução).</translation>
    </message>
    <message>
        <source>Toggle mute on the default device (running instance).</source>
        <translation>Silencia/reativa o dispositivo padrão (instância em execução).</translation>
    </message>
    <message>
        <source>Toggle “mute all” (running instance).</source>
        <translation>Alterna o “silenciar tudo” (instância em execução).</translation>
    </message>
    <message>
        <source>Show or hide the mixer window (running instance).</source>
        <translation>Mostra ou esconde a janela do mixer (instância em execução).</translation>
    </message>
    <message>
        <source>Quit the running instance.</source>
        <translation>Encerra a instância em execução.</translation>
    </message>
    <message>
        <source>%1: not available in this build</source>
        <translation>%1: não disponível nesta compilação</translation>
    </message>
    <message>
        <source>Could not connect to any audio system.

%1</source>
        <translation>Não foi possível conectar a nenhum sistema de áudio.

%1</translation>
    </message>
    <message>
        <source>LoudMixer is still running in the system tray.</source>
        <translation>O LoudMixer continua rodando na bandeja do sistema.</translation>
    </message>
</context>
</TS>