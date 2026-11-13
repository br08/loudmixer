#!/usr/bin/env python3
"""Preenche translations/loudmixer_pt_BR.ts com as traduções abaixo.

Fluxo para novas strings:
  1. cmake --build build --target loudmixer_lupdate   (extrai as strings)
  2. adicione a tradução no dicionário PT_BR
  3. python3 tools/fill_pt_BR.py
Strings sem tradução ficam marcadas como "unfinished" e aparecem em inglês.
"""
import os
import sys
import xml.etree.ElementTree as ET

TS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "translations", "loudmixer_pt_BR.ts")

PT_BR = {
    # ChannelsPanel / MixerWindow
    "No output devices.": "Nenhum dispositivo de saída.",
    "Device:": "Dispositivo:",
    "Clipping indicator: lights up red while the soft-clipper is limiting peaks":
        "Indicador de clipping: acende em vermelho enquanto o soft-clip está limitando os picos",
    "Reset to 100%": "Voltar para 100%",
    "Boost above 100% is not available for this item right now":
        "O boost acima de 100% não está disponível para este item no momento",
    "Unmute": "Reativar som",
    "Mute": "Silenciar",
    "No application is playing audio right now.": "Nenhum aplicativo está tocando áudio no momento.",
    "Applications": "Aplicativos",
    "Devices": "Dispositivos",
    "Channels": "Canais",
    "Apply a saved preset": "Aplicar um preset salvo",
    "Save…": "Salvar…",
    "Save the current gains as a preset": "Salvar os ganhos atuais como preset",
    "Delete": "Excluir",
    "Delete the selected preset": "Excluir o preset selecionado",
    "Mute all": "Silenciar tudo",
    "Reset all": "Resetar tudo",
    "Set every app, device and channel back to 100%": "Voltar todos os apps, dispositivos e canais para 100%",
    "Settings": "Configurações",
    "\nPID %1": "\nPID %1",
    "\nOutput: %1": "\nSaída: %1",
    "DSP boost": "Boost DSP",
    "Route this device through LoudMixer's DSP: real boost up to 200% with soft-clipping and a clipping indicator (adds a few milliseconds of latency)":
        "Passa o áudio deste dispositivo pelo DSP do LoudMixer: boost real até 200% com soft-clip e indicador de clipping (adiciona alguns milissegundos de latência)",
    "%1\nDefault output device": "%1\nDispositivo de saída padrão",
    "Audio backend: %1": "Backend de áudio: %1",
    " — enable “DSP boost” on a device to go above 100%": " — ative o “Boost DSP” em um dispositivo para passar de 100%",
    "Could not change DSP boost:\n%1": "Não foi possível alterar o Boost DSP:\n%1",
    "Presets…": "Presets…",
    "Save preset": "Salvar preset",
    "Preset name (e.g. “Movie night”):": "Nome do preset (ex.: “Filme à noite”):",
    "Replace the existing preset “%1”?": "Substituir o preset “%1” existente?",
    "Delete preset": "Excluir preset",
    "Delete the preset “%1”?": "Excluir o preset “%1”?",
    # SettingsDialog
    "Start LoudMixer when I log in": "Iniciar o LoudMixer ao entrar no sistema",
    "Start minimized to the system tray": "Iniciar minimizado na bandeja do sistema",
    "Enable global hotkeys (Ctrl+Alt+↑ / ↓ / M)": "Ativar atalhos globais (Ctrl+Alt+↑ / ↓ / M)",
    "LoudMixer Settings": "Configurações do LoudMixer",
    "Audio": "Áudio",
    "Soft (tanh) — recommended": "Suave (tanh) — recomendado",
    "Soft (cubic) — lighter on CPU": "Suave (cúbica) — mais leve para a CPU",
    "Hard clip": "Corte seco (hard clip)",
    "How peaks above 0 dBFS are limited when DSP boost is active":
        "Como os picos acima de 0 dBFS são limitados quando o Boost DSP está ativo",
    "Clipping curve:": "Curva de clipping:",
    "auto (CABLE Input, VoiceMeeter, ...)": "automático (CABLE Input, VoiceMeeter, ...)",
    "Virtual output device whose audio is captured and boosted":
        "Dispositivo de saída virtual cujo áudio é capturado e amplificado",
    "Boost source device:": "Dispositivo de origem do boost:",
    "Startup": "Inicialização",
    "Hotkeys": "Atalhos",
    "Volume step:": "Passo de volume:",
    "Bind these commands to keys in your desktop's keyboard settings:\n  loudmixer --volume-up\n  loudmixer --volume-down\n  loudmixer --mute\n  loudmixer --toggle":
        "Associe estes comandos a teclas nas configurações de teclado do seu ambiente gráfico:\n  loudmixer --volume-up\n  loudmixer --volume-down\n  loudmixer --mute\n  loudmixer --toggle",
    "Language": "Idioma",
    "System default": "Padrão do sistema",
    "Interface language:": "Idioma da interface:",
    "The new language will be used after restarting LoudMixer.": "O novo idioma será usado depois de reiniciar o LoudMixer.",
    "Could not change the startup setting:\n%1": "Não foi possível alterar a inicialização automática:\n%1",
    # TrayController
    "Open mixer": "Abrir mixer",
    "Presets": "Presets",
    "Reset all gains": "Resetar todos os ganhos",
    "Start with the system": "Iniciar com o sistema",
    "Settings…": "Configurações…",
    "Quit": "Sair",
    " (muted)": " (mudo)",
    "(no presets saved)": "(nenhum preset salvo)",
    "Save current as preset…": "Salvar estado atual como preset…",
    # main
    "Volume booster per app, device and channel": "Amplificador de volume por aplicativo, dispositivo e canal",
    "Start hidden in the system tray.": "Iniciar oculto na bandeja do sistema.",
    "Audio backend: auto, %1.": "Backend de áudio: auto, %1.",
    "Raise the default device volume (running instance).": "Aumenta o volume do dispositivo padrão (instância em execução).",
    "Lower the default device volume (running instance).": "Diminui o volume do dispositivo padrão (instância em execução).",
    "Toggle mute on the default device (running instance).": "Silencia/reativa o dispositivo padrão (instância em execução).",
    "Toggle “mute all” (running instance).": "Alterna o “silenciar tudo” (instância em execução).",
    "Show or hide the mixer window (running instance).": "Mostra ou esconde a janela do mixer (instância em execução).",
    "Quit the running instance.": "Encerra a instância em execução.",
    "%1: not available in this build": "%1: não disponível nesta compilação",
    "Could not connect to any audio system.\n\n%1": "Não foi possível conectar a nenhum sistema de áudio.\n\n%1",
    "LoudMixer is still running in the system tray.": "O LoudMixer continua rodando na bandeja do sistema.",
    # AudioSessionManager / AutoStartService
    "No audio backend available.": "Nenhum backend de áudio disponível.",
    "This audio backend does not support DSP boost.": "Este backend de áudio não suporta Boost DSP.",
    "Could not write to the Windows registry.": "Não foi possível gravar no registro do Windows.",
    "Could not remove %1": "Não foi possível remover %1",
    "Could not create %1": "Não foi possível criar %1",
    # backend (mensagens de erro dos backends)
    "Could not start the PipeWire event loop.": "Não foi possível iniciar o loop de eventos do PipeWire.",
    "Could not connect to the PipeWire daemon.": "Não foi possível conectar ao daemon do PipeWire.",
    "PipeWire did not respond in time.": "O PipeWire não respondeu a tempo.",
    "Could not connect to the audio server": "Não foi possível conectar ao servidor de áudio",
    "The boost route can only be applied to devices.": "O Boost DSP só pode ser aplicado a dispositivos.",
    "The audio backend is not running.": "O backend de áudio não está em execução.",
    "Not connected to the audio server.": "Sem conexão com o servidor de áudio.",
    "DSP boost is already active on this device.": "O Boost DSP já está ativo neste dispositivo.",
    "Device not found.": "Dispositivo não encontrado.",
    "Could not open audio stream": "Não foi possível abrir o stream de áudio",
    "Could not load module-null-sink": "Não foi possível carregar o module-null-sink",
    "The audio server refused to create the virtual device (module-null-sink).":
        "O servidor de áudio recusou criar o dispositivo virtual (module-null-sink).",
    "Could not create audio streams.": "Não foi possível criar os streams de áudio.",
    "Could not open the boost source device": "Não foi possível abrir o dispositivo de origem do boost",
    "Could not open the output device": "Não foi possível abrir o dispositivo de saída",
    "The boost source device does not use a float32 mix format (not supported in v1).":
        "O dispositivo de origem do boost não usa formato de mixagem float32 (não suportado na v1).",
    "Could not access the Windows audio devices (IMMDeviceEnumerator).":
        "Não foi possível acessar os dispositivos de áudio do Windows (IMMDeviceEnumerator).",
    "DSP boost is already active on another device (v1 supports one at a time).":
        "O Boost DSP já está ativo em outro dispositivo (a v1 suporta um por vez).",
    "This device already boosts above 100% through the LoudMixer system effect.":
        "Este dispositivo já passa de 100% pelo efeito de sistema do LoudMixer.",
    'No virtual output device found. Install VB-Audio Virtual Cable (free), set "CABLE Input" as the Windows default output and enable DSP boost again.':
        'Nenhum dispositivo de saída virtual encontrado. Instale o VB-Audio Virtual Cable (gratuito), defina "CABLE Input" como saída padrão do Windows e ative o Boost DSP novamente.',
    "This audio backend does not support DSP processing.": "Este backend de áudio não suporta processamento DSP.",
    ' — click “Turn on boost” on a device to go above 100%':
        ' — clique em “Ligar boost” num dispositivo para passar de 100%',
    'Turn on boost':
        'Ligar boost',
    'Let this device, its channels and its apps go up to 200% (asks for administrator permission)':
        'Deixa este dispositivo, os canais e os apps dele irem até 200% (pede permissão de administrador)',
    'Boost on':
        'Boost ligado',
    'Boost up to 200% is on for this device. Click to turn it off.':
        'O boost até 200% está ligado neste dispositivo. Clique para desligar.',
    'Boost paused':
        'Boost pausado',
    'Audio enhancements were turned off for this device in the Windows settings, so the boost stopped. Click to turn it back on.':
        'Os aprimoramentos de áudio deste dispositivo foram desligados nas configurações do Windows, e o boost parou. Clique para religar.',
    'Boost removed':
        'Boost removido',
    'A driver or Windows update removed the boost from this device. Click to turn it back on.':
        'Uma atualização de driver ou do Windows removeu o boost deste dispositivo. Clique para religar.',
    'Turning off…':
        'Desligando…',
    'Turning on…':
        'Ligando…',
    'LoudMixer will add its audio effect to “%1” so it can go up to 200%.':
        'O LoudMixer vai adicionar o efeito de áudio dele a “%1” para que ele possa ir até 200%.',
    'Windows will ask for administrator permission.':
        'O Windows vai pedir permissão de administrador.',
    'The audio pauses for a few seconds.':
        'O áudio para por alguns segundos.',
    'Audio enhancements are turned on for this device.':
        'Os aprimoramentos de áudio deste dispositivo são ligados.',
    'This also turns on the effects already installed for it: %1.':
        'Isso também liga os efeitos que já estão instalados para ele: %1.',
    'Turn off the boost on “%1”? The device goes back to how it was before (up to 100%).':
        'Desligar o boost em “%1”? O dispositivo volta a ser como era antes (até 100%).',
    'Boost up to 200%':
        'Boost até 200%',
    'Turn off boost':
        'Desligar boost',
    'Windows does not run audio effects on “%1”, so the boost could not be turned on. The device was left as it was.\n\nAs an alternative, the virtual cable route can be turned on in Settings > Advanced.':
        'O Windows não roda efeitos de áudio em “%1”, por isso o boost não pôde ser ligado. O dispositivo ficou como estava.\n\nComo alternativa, a rota por cabo virtual pode ser ligada em Configurações > Avançado.',
    'The LoudMixer audio effect tool (loudmixer-apo-setup.exe) was not found. Reinstall LoudMixer.':
        'A ferramenta do efeito de áudio do LoudMixer (loudmixer-apo-setup.exe) não foi encontrada. Reinstale o LoudMixer.',
    'Could not turn on the boost on “%1”:\n%2':
        'Não foi possível ligar o boost em “%1”:\n%2',
    'Could not turn off the boost on “%1”:\n%2':
        'Não foi possível desligar o boost em “%1”:\n%2',
    'Advanced':
        'Avançado',
    'Show the virtual cable route (“DSP boost”) on devices':
        'Mostrar a rota por cabo virtual (“Boost DSP”) nos dispositivos',
    'Alternative way to go above 100% for devices where the boost button does not work. Needs a virtual audio cable (e.g. VB-Audio Cable) set as the Windows default output.':
        'Outro jeito de passar de 100% em dispositivos onde o botão de boost não funciona. Precisa de um cabo de áudio virtual (ex.: VB-Audio Cable) como saída padrão do Windows.',
    'Audio enhancements were turned off for it in the Windows settings.':
        'Os aprimoramentos de áudio dele foram desligados nas configurações do Windows.',
    'A driver or Windows update removed it.':
        'Uma atualização de driver ou do Windows o removeu.',
    'Boost turned off on %1':
        'Boost desligado em %1',
    'Open LoudMixer to turn it back on.':
        'Abra o LoudMixer para religar.',
}


def main():
    tree = ET.parse(TS)
    missing = []
    for msg in tree.getroot().iter("message"):
        src = msg.find("source").text or ""
        tr = msg.find("translation")
        if src in PT_BR:
            tr.text = PT_BR[src]
            tr.attrib.pop("type", None)
        else:
            missing.append(src)
    tree.write(TS, encoding="utf-8", xml_declaration=True)
    # O lupdate espera <!DOCTYPE TS>; o ElementTree o descarta.
    with open(TS, encoding="utf-8") as f:
        content = f.read()
    if "<!DOCTYPE TS>" not in content:
        content = content.replace("?>\n", "?>\n<!DOCTYPE TS>\n", 1)
    with open(TS, "w", encoding="utf-8") as f:
        f.write(content)
    for m in missing:
        print("SEM TRADUÇÃO:", repr(m))
    print(f"{len(PT_BR)} traduções aplicadas; {len(missing)} pendentes")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
