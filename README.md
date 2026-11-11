# QtWaylandRestorePositionPlugin

A generic Qt 6.12 plugin that saves and restores native Wayland application
session IDs through `xdg-session-management-v1`, without replacing the platform
theme.

The plugin depends on Qt Core, Gui and WaylandClient, including private APIs
from the same Qt build. It does not depend on KDE Frameworks, Plasma or KWin.
The compositor must support the protocol to restore a session; KWin is used
only for testing. The plugin does not automatically assign window identities:
the application must provide a stable `sessionRestoreId` before creating the
first `xdg_toplevel`, as required by the original plasma-integration patch.

Build with Qt 6.12 and its matching private headers:

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

Enable the plugin for an application while preserving its platform theme and
any generic plugins already selected:

```sh
export QT_PLUGIN_PATH="$PWD/build/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QT_QPA_GENERIC_PLUGINS="${QT_QPA_GENERIC_PLUGINS:+$QT_QPA_GENERIC_PLUGINS,}waylandsessionpersistence"
my-qt-application
```

Do not change `QT_QPA_PLATFORMTHEME`. To install the module into your Qt plugin
directory, use `cmake --install build`. The default relative destination is
`lib/qt6/plugins/generic`, configurable through `QT_GENERIC_PLUGIN_INSTALL_DIR`.
Install only `libwaylandsessionpersistence.so`, not the test platform theme.

To disable the plugin, remove `waylandsessionpersistence` from the list or launch
the application with `QT_WAYLAND_SESSION_PERSISTENCE_DISABLE=1`. This variable
disables only this plugin, not an older persistence patch incorporated into
the platform theme. Setting `Qt::AA_DisableSessionManager` before constructing
the application also prevents the plugin from intervening.

State is saved on orderly shutdown to
`$XDG_STATE_HOME/qt-wayland-session-persistence/sessions.ini`, with Qt falling
back to `~/.local/state`. The original persistence policies are preserved:
scoping by machine, desktop, socket, runtime directory and screen configuration;
per-session ID locks; restoring the last instance to close; precedence for
`-session`; and conditional invalidation of replaced sessions. The Qt
`QSettings` store is independent of the old KConfig files: it does not read or
modify them and does not automatically migrate existing IDs.

Run the tests in the prepared container:

```sh
docker --host=unix:///var/run/docker.sock exec --user 1000:1000 \
  -w /workspace/QtWaylandRestorePositionPlugin arch-qt612 \
  bash -euc 'cmake -S . -B build -G Ninja -DBUILD_TESTING=ON; cmake --build build --parallel 2; python tests/runtime.py'
```

The suite requires Python 3, KWin Wayland, `dbus-run-session`, Qt Widgets and
WaylandCompositor. It starts isolated compositors and real applications and
checks protocol traffic. Each run saves reports and logs under `build/runtime-*`
and stops the compositors it started.

A separate control probe forces the private window identity API while
`AA_DisableSessionManager` is set. On the tested Qt 6.12.0 build, this crashes
with SIGSEGV even without the plugin. The result is recorded separately from
the plugin checks, with core dumps disabled. Applications should not assign
that private window identity when session management is disabled.

Licensed under the MIT License; see [LICENSE](LICENSE).
