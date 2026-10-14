#include "SettingsStore.h"

#include <QSettings>

namespace
{
constexpr auto kLastVaultKey = QLatin1StringView("lastVault");
} // namespace

QString AppSettingsStore::lastVault() const
{
    return QSettings().value(kLastVaultKey).toString();
}

bool AppSettingsStore::setLastVault(const QString& path)
{
    QSettings settings;
    settings.setValue(kLastVaultKey, path);
    // QSettings writes lazily; sync() writes now so a failure can be reported.
    settings.sync();
    return settings.status() == QSettings::NoError;
}
