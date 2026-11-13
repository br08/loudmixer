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
    <message>
        <source>This device already boosts above 100% through the LoudMixer system effect.</source>
        <translation>Este dispositivo já passa de 100% pelo efeito de sistema do LoudMixer.</translation>
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
    <message>
        <source> — click “Boost 200%” on a device to go above 100%</source>
        <translation> — clique em “Boost 200%” num dispositivo para passar de 100%</translation>
    </message>
    <message>
        <source>Boost 200%</source>
        <translation>Boost 200%</translation>
    </message>
    <message>
        <source>Let this device, its channels and its apps go up to 200% (asks for administrator permission)</source>
        <translation>Deixa este dispositivo, os canais e os apps dele irem até 200% (pede permissão de administrador)</translation>
    </message>
    <message>
        <source>Boost on</source>
        <translation>Boost ligado</translation>
    </message>
    <message>
        <source>Boost up to 200% is on for this device. Click to turn it off.</source>
        <translation>O boost até 200% está ligado neste dispositivo. Clique para desligar.</translation>
    </message>
    <message>
        <source>Boost paused</source>
        <translation>Boost pausado</translation>
    </message>
    <message>
        <source>Audio enhancements were turned off for this device in the Windows settings, so the boost stopped. Click to turn it back on.</source>
        <translation>Os aprimoramentos de áudio deste dispositivo foram desligados nas configurações do Windows, e o boost parou. Clique para religar.</translation>
    </message>
    <message>
        <source>Boost removed</source>
        <translation>Boost removido</translation>
    </message>
    <message>
        <source>A driver or Windows update removed the boost from this device. Click to turn it back on.</source>
        <translation>Uma atualização de driver ou do Windows removeu o boost deste dispositivo. Clique para religar.</translation>
    </message>
    <message>
        <source>Turning off…</source>
        <translation>Desligando…</translation>
    </message>
    <message>
        <source>Turning on…</source>
        <translation>Ligando…</translation>
    </message>
    <message>
        <source>LoudMixer will add its audio effect to “%1” so it can go up to 200%.</source>
        <translation>O LoudMixer vai adicionar o efeito de áudio dele a “%1” para que ele possa ir até 200%.</translation>
    </message>
    <message>
        <source>Windows will ask for administrator permission.</source>
        <translation>O Windows vai pedir permissão de administrador.</translation>
    </message>
    <message>
        <source>The audio pauses for a few seconds.</source>
        <translation>O áudio para por alguns segundos.</translation>
    </message>
    <message>
        <source>Audio enhancements are turned on for this device.</source>
        <translation>Os aprimoramentos de áudio deste dispositivo são ligados.</translation>
    </message>
    <message>
        <source>This also turns on the effects already installed for it: %1.</source>
        <translation>Isso também liga os efeitos que já estão instalados para ele: %1.</translation>
    </message>
    <message>
        <source>Turn off the boost on “%1”? The device goes back to how it was before (up to 100%).</source>
        <translation>Desligar o boost em “%1”? O dispositivo volta a ser como era antes (até 100%).</translation>
    </message>
    <message>
        <source>Boost up to 200%</source>
        <translation>Boost até 200%</translation>
    </message>
    <message>
        <source>Turn off boost</source>
        <translation>Desligar boost</translation>
    </message>
    <message>
        <source>Windows does not run audio effects on “%1”, so the boost could not be turned on. The device was left as it was.

As an alternative, the virtual cable route can be turned on in Settings &gt; Advanced.</source>
        <translation>O Windows não roda efeitos de áudio em “%1”, por isso o boost não pôde ser ligado. O dispositivo ficou como estava.

Como alternativa, a rota por cabo virtual pode ser ligada em Configurações &gt; Avançado.</translation>
    </message>
    <message>
        <source>The LoudMixer audio effect tool (loudmixer-apo-setup.exe) was not found. Reinstall LoudMixer.</source>
        <translation>A ferramenta do efeito de áudio do LoudMixer (loudmixer-apo-setup.exe) não foi encontrada. Reinstale o LoudMixer.</translation>
    </message>
    <message>
        <source>Could not turn on the boost on “%1”:
%2</source>
        <translation>Não foi possível ligar o boost em “%1”:
%2</translation>
    </message>
    <message>
        <source>Could not turn off the boost on “%1”:
%2</source>
        <translation>Não foi possível desligar o boost em “%1”:
%2</translation>
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
    <message>
        <source>Advanced</source>
        <translation>Avançado</translation>
    </message>
    <message>
        <source>Show the virtual cable route (“DSP boost”) on devices</source>
        <translation>Mostrar a rota por cabo virtual (“Boost DSP”) nos dispositivos</translation>
    </message>
    <message>
        <source>Alternative way to go above 100% for devices where “Boost 200%” does not work. Needs a virtual audio cable (e.g. VB-Audio Cable) set as the Windows default output.</source>
        <translation>Outro jeito de passar de 100% em dispositivos onde o “Boost 200%” não funciona. Precisa de um cabo de áudio virtual (ex.: VB-Audio Cable) como saída padrão do Windows.</translation>
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
    <message>
        <source>Audio enhancements were turned off for it in the Windows settings.</source>
        <translation>Os aprimoramentos de áudio dele foram desligados nas configurações do Windows.</translation>
    </message>
    <message>
        <source>A driver or Windows update removed it.</source>
        <translation>Uma atualização de driver ou do Windows o removeu.</translation>
    </message>
    <message>
        <source>Boost turned off on %1</source>
        <translation>Boost desligado em %1</translation>
    </message>
    <message>
        <source>Open LoudMixer to turn it back on.</source>
        <translation>Abra o LoudMixer para religar.</translation>
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