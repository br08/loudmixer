#pragma once

// Liga/desliga a inicialização automática junto com o SO.
//  - Linux:   arquivo .desktop em ~/.config/autostart (padrão XDG)
//  - Windows: valor em HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// Em ambos os casos o app é iniciado com `--minimized` (direto para a bandeja).

#include <QString>

namespace loudmixer {

class AutoStartService {
public:
    // `executablePath` vazio = executável atual (ou $APPIMAGE, se rodando de um AppImage).
    // `autostartDir` vazio = diretório XDG padrão. Os dois existem para testes.
    explicit AutoStartService(QString executablePath = {}, QString autostartDir = {});

    bool isSupported() const;
    bool isEnabled() const;
    bool setEnabled(bool enabled, QString* error = nullptr);

    QString executablePath() const { return executable_; }
    QString desktopEntryPath() const; // somente Linux

private:
    QString executable_;
    QString autostartDir_;
};

} // namespace loudmixer
