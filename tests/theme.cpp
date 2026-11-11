// SPDX-License-Identifier: MIT
#include <qpa/qplatformthemeplugin.h>
#include <qpa/qplatformtheme.h>
class SentinelTheme final : public QPlatformTheme
{
public:
    QVariant themeHint(ThemeHint hint) const override
    {
        if (hint == QPlatformTheme::SystemIconThemeName)
            return QStringLiteral("session-test-original-theme");
        return QPlatformTheme::themeHint(hint);
    }
};
class SentinelThemePlugin final : public QPlatformThemePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformThemeFactoryInterface_iid FILE "theme.json")
public:
    QPlatformTheme *create(const QString &key, const QStringList &) override
    {
        return key == QStringLiteral("sessiontest") ? new SentinelTheme : nullptr;
    }
};
#include "theme.moc"
