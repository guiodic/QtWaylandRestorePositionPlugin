# QtWaylandRestorePositionPlugin

Plugin generico Qt 6.12 per conservare e recuperare l'ID della sessione nativa
Wayland tramite `xdg-session-management-v1`, senza sostituire il platform theme.
La valutazione e le prove sono in [docs/assessment.md](docs/assessment.md).

Il plugin dipende da Qt Core, Gui e WaylandClient, incluse le API private della
stessa build Qt. Non dipende da KDE Frameworks, Plasma o KWin. Il compositor
deve supportare il protocollo per poter ripristinare una sessione; KWin è usato
soltanto dalle prove. Il componente non assegna automaticamente identità alle
finestre: l'app deve fornire una `sessionRestoreId` stabile prima del primo
`xdg_toplevel`, come nella patch originale.

Compilazione con Qt 6.12 e i relativi header privati:

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

Attivazione per una singola applicazione, conservando il platform theme e gli
altri eventuali plugin generici già selezionati:

```sh
export QT_PLUGIN_PATH="$PWD/build/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QT_QPA_GENERIC_PLUGINS="${QT_QPA_GENERIC_PLUGINS:+$QT_QPA_GENERIC_PLUGINS,}waylandsessionpersistence"
my-qt-application
```

Non modificare `QT_QPA_PLATFORMTHEME`. Per installare il modulo nella directory
plugin del proprio Qt è disponibile `cmake --install build`; la destinazione
relativa predefinita è `lib/qt6/plugins/generic` ed è configurabile tramite
`QT_GENERIC_PLUGIN_INSTALL_DIR`. Installare soltanto `libwaylandsessionpersistence.so`,
non il platform theme di test.

Disattivazione: rimuovere `waylandsessionpersistence` dalla lista oppure avviare
l'app con `QT_WAYLAND_SESSION_PERSISTENCE_DISABLE=1`. Questa variabile disabilita
solo questo plugin, non un'eventuale vecchia patch già incorporata nel tema.
`Qt::AA_DisableSessionManager`, impostato prima di costruire l'applicazione,
impedisce comunque l'intervento del plugin.

Lo stato è salvato alla chiusura ordinata in
`$XDG_STATE_HOME/qt-wayland-session-persistence/sessions.ini`, con fallback Qt a
`~/.local/state`. Si conservano le politiche della patch: scope per macchina,
desktop, socket, runtime directory e schermi; lock per ID; ultima istanza chiusa;
precedenza a `-session`; invalidazione condizionale delle sessioni sostituite.
Lo storage Qt `QSettings` è indipendente dai vecchi file KConfig: non li legge,
non li modifica e non migra automaticamente gli ID preesistenti.

Prove nel contenitore già predisposto:

```sh
docker --host=unix:///var/run/docker.sock exec --user 1000:1000 \
  -w /workspace/QtWaylandRestorePositionPlugin arch-qt612 \
  bash -euc 'cmake -S . -B build -G Ninja -DBUILD_TESTING=ON; cmake --build build --parallel 2; python tests/runtime.py'
```

La suite richiede Python 3, KWin Wayland, `dbus-run-session`, Qt Widgets e
WaylandCompositor. Avvia compositor isolati, applicazioni reali e controlli del
protocollo. Ogni esecuzione conserva report e log in `build/runtime-*` e termina
i compositor che ha avviato. La sonda di controllo del problema Qt descritto
nella valutazione può terminare con SIGSEGV; viene registrata separatamente dai
controlli del plugin e i core dump sono disabilitati.

`src/persistence.cpp` e il relativo header sono derivati dalla patch
plasma-integration e mantengono le licenze LGPL indicate nei rispettivi SPDX;
i testi sono in `LICENSES/`. Il codice originale di scaffolding e test usa MIT.
