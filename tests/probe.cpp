// SPDX-License-Identifier: MIT
#include <QApplication>
#include <QLabel>
#include <QWindow>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QIcon>
#include <QStyle>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformtheme.h>
#include <qpa/qplatformwindow_p.h>

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager,
                                  qEnvironmentVariableIntValue("PROBE_DISABLE_SM") != 0);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("session-persistence-probe"));
    app.setDesktopFileName(QStringLiteral("org.example.SessionPersistenceProbe"));
    const auto *theme = QGuiApplicationPrivate::platformTheme();
    const auto beforeTheme = theme->themeHint(QPlatformTheme::SystemIconThemeName).toString();
    QLabel label(QStringLiteral("Wayland session probe"));
    label.setObjectName(QStringLiteral("main-window"));
    label.resize(480, 240);
    label.winId();
    auto *native = label.windowHandle()->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    const bool hasIdentity = native && (!app.testAttribute(Qt::AA_DisableSessionManager)
                   || qEnvironmentVariableIntValue("PROBE_FORCE_IDENTITY"));
    if (hasIdentity)
        native->setSessionRestoreId(QStringLiteral("main-window"));
    label.show();
    const int delay = qEnvironmentVariableIntValue("PROBE_DELAY");
    QTimer::singleShot(delay > 0 ? delay : 500, &app, [&] {
        QJsonObject report{
            {"pid", app.applicationPid()}, {"session", app.sessionId()},
            {"restored", app.isSessionRestored()}, {"platform", app.platformName()},
            {"windowIdentity", hasIdentity ? QStringLiteral("main-window") : QString()},
            {"theme", beforeTheme},
            {"font", app.font().toString()},
            {"iconTheme", QIcon::themeName()},
            {"style", app.style()->objectName()},
            {"nativeFileDialogPolicy", theme->usePlatformNativeDialog(QPlatformTheme::FileDialog)},
            {"themeUnchanged", theme == QGuiApplicationPrivate::platformTheme()
                 && beforeTheme == theme->themeHint(QPlatformTheme::SystemIconThemeName).toString()},
            {"qt", QString::fromLatin1(qVersion())}
        };
        const QByteArray data = QJsonDocument(report).toJson(QJsonDocument::Compact);
        QFile out(qEnvironmentVariable("PROBE_REPORT"));
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
            app.exit(2);
        else
            app.quit();
    });
    return app.exec();
}
