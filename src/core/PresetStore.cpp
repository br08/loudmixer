#include "PresetStore.h"

#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

namespace loudmixer {

namespace {
constexpr auto kArrayKey = "presets";

QVariantMap toVariant(const QMap<QString, double>& m)
{
    QVariantMap out;
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QMap<QString, double> fromVariant(const QVariantMap& m)
{
    QMap<QString, double> out;
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        out.insert(it.key(), it.value().toDouble());
    return out;
}
} // namespace

PresetStore::PresetStore(std::unique_ptr<QSettings> settings)
    : settings_(settings ? std::move(settings) : std::make_unique<QSettings>())
{
}

QList<Preset> PresetStore::readAll() const
{
    QList<Preset> out;
    const int n = settings_->beginReadArray(kArrayKey);
    for (int i = 0; i < n; ++i) {
        settings_->setArrayIndex(i);
        Preset p;
        p.name = settings_->value("name").toString();
        p.appGains = fromVariant(settings_->value("apps").toMap());
        p.deviceGains = fromVariant(settings_->value("devices").toMap());
        const QVariantMap ch = settings_->value("channels").toMap();
        for (auto it = ch.cbegin(); it != ch.cend(); ++it) {
            QList<double> gains;
            for (const QVariant& v : it.value().toList())
                gains.append(v.toDouble());
            p.channelGains.insert(it.key(), gains);
        }
        if (!p.name.isEmpty())
            out.append(p);
    }
    settings_->endArray();
    return out;
}

void PresetStore::writeAll(const QList<Preset>& presets)
{
    settings_->remove(kArrayKey);
    settings_->beginWriteArray(kArrayKey, static_cast<int>(presets.size()));
    for (int i = 0; i < presets.size(); ++i) {
        const Preset& p = presets.at(i);
        settings_->setArrayIndex(i);
        settings_->setValue("name", p.name);
        settings_->setValue("apps", toVariant(p.appGains));
        settings_->setValue("devices", toVariant(p.deviceGains));
        QVariantMap ch;
        for (auto it = p.channelGains.cbegin(); it != p.channelGains.cend(); ++it) {
            QVariantList gains;
            for (double g : it.value())
                gains.append(g);
            ch.insert(it.key(), gains);
        }
        settings_->setValue("channels", ch);
    }
    settings_->endArray();
    settings_->sync();
}

QStringList PresetStore::names() const
{
    QStringList out;
    for (const Preset& p : readAll())
        out.append(p.name);
    return out;
}

std::optional<Preset> PresetStore::load(const QString& name) const
{
    for (const Preset& p : readAll())
        if (p.name == name)
            return p;
    return std::nullopt;
}

void PresetStore::save(const Preset& preset)
{
    if (preset.name.trimmed().isEmpty())
        return;
    QList<Preset> all = readAll();
    auto it = std::find_if(all.begin(), all.end(), [&](const Preset& p) { return p.name == preset.name; });
    if (it != all.end())
        *it = preset;
    else
        all.append(preset);
    writeAll(all);
}

bool PresetStore::remove(const QString& name)
{
    QList<Preset> all = readAll();
    const auto removed = all.removeIf([&](const Preset& p) { return p.name == name; });
    if (removed == 0)
        return false;
    writeAll(all);
    return true;
}

} // namespace loudmixer
