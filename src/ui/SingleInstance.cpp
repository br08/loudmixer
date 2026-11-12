#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

namespace loudmixer::ui {

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::serverName()
{
    // Um servidor por usuário (o nome do socket é global no Windows).
    const QByteArray user = QDir::homePath().toUtf8();
    return QStringLiteral("loudmixer-") + QString::fromLatin1(QCryptographicHash::hash(user, QCryptographicHash::Sha1).toHex().left(12));
}

bool SingleInstance::tryBecomePrimary(const QString& message)
{
    {
        QLocalSocket probe;
        probe.connectToServer(serverName());
        if (probe.waitForConnected(300)) {
            probe.write(message.toUtf8() + '\n');
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
            return false;
        }
    }

    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_->listen(serverName())) {
        // Socket órfão de uma execução que caiu: remove e tenta de novo.
        QLocalServer::removeServer(serverName());
        server_->listen(serverName());
    }
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                while (socket->canReadLine()) {
                    const QString msg = QString::fromUtf8(socket->readLine()).trimmed();
                    if (!msg.isEmpty())
                        emit messageReceived(msg);
                }
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    return true;
}

} // namespace loudmixer::ui
