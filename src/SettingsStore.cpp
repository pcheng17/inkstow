#include "SettingsStore.h"

#include <QSettings>

namespace
{
const QString kLastVaultKey = QStringLiteral("lastVault");
} // namespace

QString AppSettingsStore::lastVault() const
{
    return QSettings().value(kLastVaultKey).toString();
}

void AppSettingsStore::setLastVault(const QString &path)
{
    QSettings().setValue(kLastVaultKey, path);
}
