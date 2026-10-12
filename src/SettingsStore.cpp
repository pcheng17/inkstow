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

void AppSettingsStore::setLastVault(const QString &path)
{
    QSettings().setValue(kLastVaultKey, path);
}
