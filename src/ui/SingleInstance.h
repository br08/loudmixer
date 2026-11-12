#pragma once

// Garante uma única instância por usuário. Uma segunda execução encaminha
// seu comando (ex.: `loudmixer --volume-up`) para a instância principal e
// sai. É assim que os "atalhos globais" funcionam no Linux/Wayland: o
// usuário associa esses comandos a teclas nas configurações do desktop.

#include <QObject>
#include <QString>

class QLocalServer;

namespace loudmixer::ui {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    // true = esta é a instância principal (passa a escutar comandos).
    // false = já existe outra; `message` foi entregue a ela.
    bool tryBecomePrimary(const QString& message);

    static QString serverName();

signals:
    void messageReceived(const QString& message);

private:
    QLocalServer* server_ = nullptr;
};

} // namespace loudmixer::ui
