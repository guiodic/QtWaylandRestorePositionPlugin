// SPDX-License-Identifier: MIT
#include <QGuiApplication>
#include <QWaylandCompositor>
#include <QWaylandOutput>
#include <QWaylandXdgShell>
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QWaylandCompositor compositor;
    compositor.setSocketName(QByteArrayLiteral("wayland-no-session"));
    QWaylandOutput output(&compositor, nullptr);
    QWaylandOutputMode mode(QSize(1024, 768), 60000);
    output.addMode(mode, true);
    output.setCurrentMode(mode);
    QWaylandXdgShell shell(&compositor);
    QObject::connect(&shell, &QWaylandXdgShell::toplevelCreated, &app,
        [](QWaylandXdgToplevel *top, QWaylandXdgSurface *) {
            top->sendConfigure(QSize(480, 240), QList<QWaylandXdgToplevel::State>{});
        });
    compositor.create();
    return app.exec();
}
