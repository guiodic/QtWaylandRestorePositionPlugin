# QtWaylandRestorePositionPlugin

A generic Qt 6.12 plugin that saves and restores native Wayland application
session IDs through `xdg-session-management-v1`, without replacing the platform
theme.

The plugin depends on Qt Core, Gui and WaylandClient, including private APIs
from the same Qt build. It does not depend on KDE Frameworks, Plasma or KWin.
The compositor must support the protocol to restore a session (Kwin does). 
The plugin does not automatically assign window identities:
the application must provide a stable `sessionRestoreId` before creating the
first `xdg_toplevel`.

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

Installation also places the executable `waylandsessionpersistence.sh` in
`/etc/profile.d` to enable the plugin for subsequent login shells. It preserves
other generic plugins and avoids duplicate entries when sourced again. The
destination is configurable through `WAYLAND_SESSION_PROFILE_INSTALL_DIR`.
The default installation prefix is `/usr`; override it with
`-DCMAKE_INSTALL_PREFIX=/your/prefix` if needed. Install the module into a plugin
directory searched by your Qt build, or add its parent plugin directory to
`QT_PLUGIN_PATH` for a custom installation.

To disable the plugin, remove `waylandsessionpersistence` from the list or launch
the application with `QT_WAYLAND_SESSION_PERSISTENCE_DISABLE=1`. This variable
disables only this plugin, not an older persistence patch incorporated into
the platform theme. Setting `Qt::AA_DisableSessionManager` before constructing
the application also prevents the plugin from intervening.

State is saved on orderly shutdown to
`$XDG_STATE_HOME/qt-wayland-session-persistence/sessions.ini`, with Qt falling
back to `~/.local/state`. 

The persistence policies:
scoping by machine, desktop, socket, runtime directory and screen configuration;
per-session ID locks; restoring the last instance to close; precedence for
`-session`; and conditional invalidation of replaced sessions. 

Licensed under the MIT License; see [LICENSE](LICENSE).
