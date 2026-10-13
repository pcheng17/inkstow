#pragma once

#include <QString>

// Remembers what Inkstow should restore on the next launch.
class SettingsStore
{
public:
    SettingsStore() = default;
    SettingsStore(const SettingsStore &) = delete;
    SettingsStore &operator=(const SettingsStore &) = delete;
    SettingsStore(SettingsStore &&) = delete;
    SettingsStore &operator=(SettingsStore &&) = delete;
    virtual ~SettingsStore() = default;

    // The folder of the last opened vault, or empty if none was opened.
    virtual QString lastVault() const = 0;
    // Returns false if the setting could not be saved.
    virtual bool setLastVault(const QString &path) = 0;
};

// Keeps settings in Qt's per-user settings for the application.
class AppSettingsStore final : public SettingsStore
{
public:
    QString lastVault() const override;
    bool setLastVault(const QString &path) override;
};
