#pragma once

// Presets nomeados ("Filme à noite", "Reunião", ...), persistidos com QSettings.
//
// Apps são identificados pelo nome (ids de sessão são efêmeros); dispositivos
// pelo id estável que o backend fornece.

#include <QList>
#include <QMap>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace loudmixer {

struct Preset {
    QString name;
    QMap<QString, double> appGains;              // appName  -> ganho
    QMap<QString, double> deviceGains;           // deviceId -> ganho mestre
    QMap<QString, QList<double>> channelGains;   // deviceId -> ganho por canal

    bool operator==(const Preset&) const = default;
};

class PresetStore {
public:
    // Sem argumento usa o QSettings padrão da aplicação.
    explicit PresetStore(std::unique_ptr<QSettings> settings = nullptr);

    QStringList names() const;
    std::optional<Preset> load(const QString& name) const;
    void save(const Preset& preset); // substitui se o nome já existir
    bool remove(const QString& name);

private:
    QList<Preset> readAll() const;
    void writeAll(const QList<Preset>& presets);

    std::unique_ptr<QSettings> settings_;
};

} // namespace loudmixer
