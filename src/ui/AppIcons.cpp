#include "AppIcons.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace loudmixer::ui {

namespace {

QPixmap drawSpeaker(int size, TrayState state)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 32.0;

    // Fundo arredondado
    QColor bg = state == TrayState::Muted ? QColor(0x55, 0x5a, 0x64) : QColor(0x1f, 0x6f, 0xeb);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(1 * s, 1 * s, 30 * s, 30 * s), 7 * s, 7 * s);

    // Alto-falante
    QPainterPath speaker;
    speaker.moveTo(6 * s, 12.5 * s);
    speaker.lineTo(10.5 * s, 12.5 * s);
    speaker.lineTo(16 * s, 7.5 * s);
    speaker.lineTo(16 * s, 24.5 * s);
    speaker.lineTo(10.5 * s, 19.5 * s);
    speaker.lineTo(6 * s, 19.5 * s);
    speaker.closeSubpath();
    p.setBrush(Qt::white);
    p.drawPath(speaker);

    if (state == TrayState::Muted) {
        QPen x(Qt::white, 2.4 * s, Qt::SolidLine, Qt::RoundCap);
        p.setPen(x);
        p.drawLine(QPointF(19.5 * s, 12.5 * s), QPointF(26 * s, 19.5 * s));
        p.drawLine(QPointF(26 * s, 12.5 * s), QPointF(19.5 * s, 19.5 * s));
        return pm;
    }

    // Ondas: a externa fica âmbar quando há boost acima de 100%.
    p.setBrush(Qt::NoBrush);
    QPen wave(Qt::white, 2.2 * s, Qt::SolidLine, Qt::RoundCap);
    p.setPen(wave);
    p.drawArc(QRectF(13 * s, 11 * s, 10 * s, 10 * s), -50 * 16, 100 * 16);
    wave.setColor(state == TrayState::Boosted ? palette::boost : QColor(Qt::white));
    p.setPen(wave);
    p.drawArc(QRectF(11 * s, 6.5 * s, 18 * s, 19 * s), -50 * 16, 100 * 16);
    return pm;
}

} // namespace

QIcon appIcon(TrayState state)
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(drawSpeaker(size, state));
    return icon;
}

QIcon volumeIcon(bool muted)
{
    const QIcon themed = QIcon::fromTheme(muted ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-high"));
    if (!themed.isNull())
        return themed;
    QIcon icon;
    for (int size : {16, 24, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size / 16.0;
        const QColor fg = muted ? palette::danger : QColor(0x60, 0x66, 0x70);
        QPainterPath speaker;
        speaker.moveTo(1.5 * s, 5.5 * s);
        speaker.lineTo(4.5 * s, 5.5 * s);
        speaker.lineTo(8 * s, 2.5 * s);
        speaker.lineTo(8 * s, 13.5 * s);
        speaker.lineTo(4.5 * s, 10.5 * s);
        speaker.lineTo(1.5 * s, 10.5 * s);
        speaker.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawPath(speaker);
        QPen pen(fg, 1.4 * s, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        if (muted) {
            p.drawLine(QPointF(10 * s, 5.5 * s), QPointF(14.5 * s, 10.5 * s));
            p.drawLine(QPointF(14.5 * s, 5.5 * s), QPointF(10 * s, 10.5 * s));
        } else {
            p.drawArc(QRectF(6.5 * s, 4.5 * s, 6 * s, 7 * s), -50 * 16, 100 * 16);
            p.drawArc(QRectF(6 * s, 1.5 * s, 9.5 * s, 13 * s), -50 * 16, 100 * 16);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon sessionIcon(const AudioSession& session)
{
    const QString name = QString::fromStdString(session.iconName);
#ifdef Q_OS_WIN
    if (!name.isEmpty()) {
        static QFileIconProvider provider;
        const QIcon icon = provider.icon(QFileInfo(name));
        if (!icon.isNull())
            return icon;
    }
    return QIcon::fromTheme(QStringLiteral("application-x-executable"), appIcon());
#else
    if (!name.isEmpty() && QIcon::hasThemeIcon(name))
        return QIcon::fromTheme(name);
    const QString lowerApp = QString::fromStdString(session.appName).toLower();
    if (!lowerApp.isEmpty() && QIcon::hasThemeIcon(lowerApp))
        return QIcon::fromTheme(lowerApp);
    return QIcon::fromTheme(QStringLiteral("audio-x-generic"), appIcon());
#endif
}

QIcon deviceIcon(bool isDefault)
{
    const QIcon themed = QIcon::fromTheme(isDefault ? QStringLiteral("audio-speakers") : QStringLiteral("audio-card"));
    return themed.isNull() ? appIcon() : themed;
}

} // namespace loudmixer::ui
