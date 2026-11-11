/*
    SPDX-License-Identifier: MIT
*/

#include "persistence.h"

#include <QGuiApplication>

#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0) && !defined(QT_NO_SESSIONMANAGER)
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QSessionManager>
#include <QStandardPaths>
#include <QSysInfo>
#include <QWindow>
#include <private/qguiapplication_p.h>
#include <private/qsessionmanager_p.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformsessionmanager.h>
#include <QtWaylandClient/private/qwaylandsessionmanager_p.h>

#include <QSettings>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPersistence, "qt.wayland.session.persistence")

#include <memory>

namespace
{
// Follow KWindowConfig's per-resolution policy for one screen. For multiple
// screens, also include the connector names used for selecting the monitor.
QString screenConfigurationKey()
{
    const auto screens = QGuiApplication::screens();
    if (screens.size() == 1) {
        const QSize size = screens.first()->geometry().size();
        return QStringLiteral("%1x%2 screen").arg(size.width()).arg(size.height());
    }
    QStringList names;
    for (const auto *screen : screens) {
        names.append(screen->name());
    }
    // Screen enumeration order must not select a different session.
    names.sort();
    return QStringLiteral("%1 screens: %2").arg(screens.size()).arg(names.join(QLatin1Char(' ')));
}

// Keep separate persistence scopes for different machines, desktops, Wayland
// sockets, runtime directories and screen configurations, allowing reuse across logins.
QString sessionEnvironmentKey(const QString &runtimePath)
{
    QByteArray machine = QSysInfo::machineUniqueId();
    if (machine.isEmpty()) {
        machine = QSysInfo::machineHostName().toUtf8();
    }
    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");

    QString socketPath = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (socketPath.isEmpty()) {
        socketPath = QStringLiteral("wayland-0");
    }
    if (QDir::isRelativePath(socketPath)) {
        socketPath = QDir(runtimePath).absoluteFilePath(socketPath);
    }
    const QString canonicalPath = QFileInfo(socketPath).canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        socketPath = canonicalPath;
    }

    // Distinct runtime directories keep independent sessions even when they
    // share the state file and connect to the same absolute Wayland socket.
    const QString canonicalRuntimePath = QFileInfo(runtimePath).canonicalFilePath();
    const QString runtimeDirectory = canonicalRuntimePath.isEmpty() ? QDir::cleanPath(runtimePath) : canonicalRuntimePath;
    const QByteArray environment = machine + '\0' + desktop + '\0' + QDir::cleanPath(socketPath).toUtf8() + '\0' + runtimeDirectory.toUtf8() + '\0'
        + screenConfigurationKey().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(environment, QCryptographicHash::Sha256).toHex());
}

QString stateConfigPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation))
        .filePath(QStringLiteral("qt-wayland-session-persistence/sessions.ini"));
}

QString sessionRestoreLockPath(const QString &configPath, const QString &appId, const QString &environment, const QString &runtimePath)
{
    const QFileInfo configFileInfo(configPath);
    QString canonicalPath = configFileInfo.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        // The state file need not exist on the application's first run.
        const QString directory = configFileInfo.dir().canonicalPath();
        canonicalPath = directory.isEmpty() ? configFileInfo.absoluteFilePath() : QDir(directory).filePath(configFileInfo.fileName());
    }
    const QByteArray identity = (QDir::cleanPath(canonicalPath) + QLatin1Char('/') + appId + QLatin1Char('/') + environment).toUtf8();
    const QString lockName = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    return runtimePath + QLatin1String("/qt-wayland-session-") + lockName + QLatin1String(".lock");
}

// The compositor assigns session IDs, but Qt does not automatically reconnect
// a new application launch to its previously saved session. Persist the ID
// under the application identity so a later launch can request restoration.
// Keep separate IDs for each machine, desktop, Wayland display and runtime
// directory and screen configuration, since the same application can have
// independent sessions in them.
class NativeSessionPersistence : public QObject
{
public:
    explicit NativeSessionPersistence(QObject *parent)
        : QObject(parent)
    {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::PlatformSurface) {
            return false;
        }
        const auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() != QPlatformSurfaceEvent::SurfaceCreated) {
            return false;
        }
        const auto *window = qobject_cast<QWindow *>(object);
        if (!window || window->parent()) {
            return false;
        }
        if (window->flags().testFlag(Qt::ForeignWindow) || window->type() == Qt::Popup || window->type() == Qt::ToolTip) {
            return false;
        }

        // Qt 6.12 Wayland creates the wl_surface in the platform window
        // constructor, but defers the xdg_toplevel until setVisible().
        // Never retry after an eligible window, even if initialization
        // returns early: the window may acquire its xdg_toplevel next.
        qApp->removeEventFilter(this);
        if (!initialize()) {
            // QGuiApplication owns the object returned by QGenericPlugin::create
            // in generic_plugin_list. Keep this inactive object alive until its
            // teardown; self-deletion would leave a dangling pointer in that list.
            return false;
        }
        // Save only on shutdown so startup cannot overwrite a just-closed session.
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            saveSessionId();
        });
        return false;
    }

private:
    bool initialize()
    {
        auto *application = QGuiApplicationPrivate::instance();
        if (!application->session_manager) {
            return false;
        }
        if (qApp->testAttribute(Qt::AA_DisableSessionManager)) {
            return false;
        }
        const auto *qtManager = static_cast<QSessionManagerPrivate *>(QObjectPrivate::get(application->session_manager));
        const auto *waylandManager = dynamic_cast<const QtWaylandClient::QWaylandSessionManager *>(qtManager->platformSessionManager);
        if (!waylandManager || !waylandManager->session()) {
            qCDebug(lcPersistence) << "session protocol unavailable; persistence skipped";
            return false;
        }
        const QString appId = QGuiApplication::desktopFileName().isEmpty() ? QCoreApplication::applicationName() : QGuiApplication::desktopFileName();
        if (appId.isEmpty()) {
            return false;
        }

        // A passed connection FD can bypass WAYLAND_DISPLAY. Skip persistence
        // rather than assign it to an unrelated/default socket path.
        if (qEnvironmentVariableIsSet("WAYLAND_SOCKET")) {
            return false;
        }
        const QString runtimePath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (runtimePath.isEmpty()) {
            return false;
        }
        // Select the scope at launch. Output changes while running do not
        // reassign existing toplevels to a different session.
        m_environment = sessionEnvironmentKey(runtimePath);

        m_stateConfigPath = stateConfigPath();
        m_appId = appId;
        const QString lockPath = sessionRestoreLockPath(m_stateConfigPath, appId, m_environment, runtimePath);
        m_updateLockPath = lockPath + QLatin1String(".update");
        const auto acquireSessionLock = [&](const QString &id) {
            if (id.isEmpty()) {
                return false;
            }
            const QString key = QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
            const QString path = lockPath + QLatin1Char('.') + key + QLatin1String(".session");
            if (m_restoreLock && m_restoreLock->fileName() == path) {
                return true;
            }
            auto lock = std::make_unique<QLockFile>(path);
            // Disable age-based expiry; dead owners can still be recovered.
            lock->setStaleLockTime(0);
            if (!lock->tryLock(0)) {
                return false;
            }
            m_restoreLock = std::move(lock);
            return true;
        };

        if (qApp->isSessionRestored()) {
            // Qt has already selected the explicit -session ID. Protect and
            // persist it too, so an ordinary launch cannot take it over and
            // later launches can restore it after this process exits.
            m_sessionId = qApp->sessionId();
            return acquireSessionLock(m_sessionId);
        }

        // The shared config may have been cached before another process saved.
        // Read independently without reparsing the application's shared object.
        QSettings config(m_stateConfigPath, QSettings::IniFormat);
        config.beginGroup(QStringLiteral("XdgSessionManagement"));
        config.beginGroup(m_environment);
        const QString savedId = config.value(appId).toString();
        qCDebug(lcPersistence) << "eligible surface" << m_appId << "saved" << savedId
                               << "current" << qApp->sessionId();
        // Protect the saved ID, not the whole application: another process may
        // still be running with a different session. Keep Qt's fresh session if
        // the saved one is occupied.
        if (!savedId.isEmpty() && acquireSessionLock(savedId) && savedId != qApp->sessionId()) {
            auto *manager = static_cast<QSessionManagerPrivate *>(QObjectPrivate::get(application->session_manager));
            auto *previous = manager->platformSessionManager;
            // Qt has already requested a new session from the compositor and
            // exposes its ID through qApp->sessionId(). Qt 6.12 has no public
            // setter, so replace the backend to request the saved session before
            // any xdg_toplevel is associated with it. On exit, save the current ID.
            manager->platformSessionManager = application->platformIntegration()->createPlatformSessionManager(savedId, qApp->sessionKey());
            delete previous;
        }
        m_sessionId = qApp->sessionId();
        // Also protect fresh sessions, including a different ID returned by
        // the compositor when the requested session cannot be restored.
        return acquireSessionLock(m_sessionId);
    }

    void saveSessionId()
    {
        if (!QGuiApplicationPrivate::instance()->session_manager) {
            return;
        }
        if (m_sessionId.isEmpty()) {
            return;
        }
        // Writers of all session IDs share the same update lock.
        // Hold this lock from the fresh read through sync() so invalidation
        // cannot delete an ID written between the comparison and the update.
        QLockFile updateLock(m_updateLockPath);
        updateLock.setStaleLockTime(0);
        if (!updateLock.tryLock(1000)) {
            qWarning() << "Failed to lock native Wayland session state in" << m_stateConfigPath;
            return;
        }
        QSettings config(m_stateConfigPath, QSettings::IniFormat);
        config.beginGroup(QStringLiteral("XdgSessionManagement"));
        config.beginGroup(m_environment);
        const QString savedSessionId = config.value(m_appId).toString();
        if (qApp->sessionId().isEmpty()) {
            // Qt clears a previously assigned ID when the compositor replaces
            // our session, e.g. after an explicit -session launch bypasses the
            // restore lock. Do not let a later launch take that session back.
            // Only invalidate our own ID; another process may have saved a
            // different session since we started.
            if (savedSessionId != m_sessionId) {
                return;
            }
            config.remove(m_appId);
        } else {
            if (savedSessionId == m_sessionId) {
                return;
            }
            config.setValue(m_appId, m_sessionId);
        }
        config.sync();
        qCDebug(lcPersistence) << "saved session" << m_sessionId << "live" << qApp->sessionId();
        if (config.status() != QSettings::NoError) {
            qWarning() << "Failed to persist native Wayland session ID in" << m_stateConfigPath;
        }
    }

    QString m_updateLockPath;
    QString m_stateConfigPath;
    QString m_appId;
    QString m_sessionId;
    QString m_environment;
    std::unique_ptr<QLockFile> m_restoreLock;
};
}
#endif

QObject *createWaylandSessionPersistence()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0) && !defined(QT_NO_SESSIONMANAGER)
    if (!qApp || !QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
        || qEnvironmentVariableIntValue("QT_WAYLAND_SESSION_PERSISTENCE_DISABLE") != 0) {
        return new QObject;
    }
    // Coordinate with the original patch: whichever hook is loaded first owns it.
    constexpr auto property = "org.kde.plasma.integration.nativeSessionPersistence";
    if (!qApp->property(property).toBool()) {
        qApp->setProperty(property, true);
        auto *hook = new NativeSessionPersistence(nullptr);
        qCDebug(lcPersistence) << "generic hook installed; session manager exists:"
                               << bool(QGuiApplicationPrivate::instance()->session_manager);
        return hook;
    }
#endif
    return new QObject;
}
