# Valutazione: plugin generico Qt per sessioni native Wayland

Il meccanismo è separabile dal platform theme e il prototipo funziona con
Qt 6.12.0 e KWin 6.8.2. Si tratta di un componente legato alle API private Qt,
non di un plugin con ABI pubblica stabile. Mantiene il `QSessionManager`
dell'applicazione e sostituisce soltanto il suo backend QPA, prima che la prima
finestra idonea acquisisca un `xdg_toplevel`.

## Riferimenti e ordine di caricamento: conclusioni dai sorgenti

Patch di riferimento: ramo `qt612-last-closed-session`, commit
`ac8aa97151e7a96ce5bd963a431056971a0c389c` di
[plasma-integration](https://github.com/guiodic/plasma-integration/blob/ac8aa97151e7a96ce5bd963a431056971a0c389c/qt6/src/platformtheme/kwaylandsessionmanagement.cpp).
Il checkout di quel repository non è stato modificato; il ramo è stato letto
tramite fetch. Il prototipo è nel repository QtWaylandRestorePositionPlugin.

Sorgenti Qt esaminati: tag upstream `v6.12.0` di `qt/qtbase`. In questa versione
il client Wayland è dentro qtbase, non nei vecchi percorsi `qtwayland/src/client`.

1. In [qguiapplication.cpp](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/kernel/qguiapplication.cpp#L1308),
   `init_platform()` seleziona integrazione QPA e platform theme, rispettando
   il tema richiesto e i fallback normali. `QGuiApplicationPrivate::init()`
   inizializza poi palette, font e theme hints.
2. Lo stesso [init()](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/kernel/qguiapplication.cpp#L1752)
   legge `QT_QPA_GENERIC_PLUGINS` come lista separata da virgole; chiama
   `init_plugins(pluginList)` alla riga 1777, esegue
   `flushWindowSystemEvents()`, poi crea `QSessionManager` alla riga 1783.
   Il factory generico cerca i moduli nella sottodirectory
   [`generic`](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/kernel/qgenericpluginfactory.cpp#L17).
   Non può quindi ripristinare immediatamente il backend in `create()`;
   può installare il filtro che attende la prima superficie.
3. In [QWindowPrivate::create()](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/kernel/qwindow.cpp#L575)
   viene costruita e inizializzata la platform window e poi inviato
   `PlatformSurface/SurfaceCreated` alla `QWindow` (riga 607). Durante la normale
   `show()`, questo precede `platformWindow->setVisible(true)` (riga 438).
4. Il costruttore di
   [QWaylandWindow](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/qwaylandwindow.cpp#L56)
   crea il `wl_surface`. `setVisible(true)` chiama invece `initWindow()`
   (riga 653), che crea la shell surface (riga 141).
5. Il costruttore di
   [QWaylandXdgSurface::Toplevel](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp#L38)
   acquisisce `xdg_toplevel` e usa `sessionRestoreId()` per chiamare
   `restore_toplevel`. È questo il limite temporale da rispettare.
6. [QWaylandSessionManager](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/qwaylandsessionmanager.cpp#L21)
   richiede la sessione e forza un round trip nel costruttore: l'ID assegnato
   o ripristinato è già disponibile al ritorno. `replaced()` svuota l'ID.

Il plugin non chiama `QPlatformThemeFactory`, non modifica `platform_theme`,
non imposta un tema sostitutivo e non cambia font, palette, icone o dialog helper.
Il filtro è one-shot: ignora finestre con parent, foreign, popup e tooltip;
si rimuove alla prima superficie idonea anche se deve rinunciare.

Una differenza di ownership rispetto alla patch è necessaria: Qt conserva
l'oggetto restituito dal factory in `generic_plugin_list` e lo elimina nel
distruttore privato di QGuiApplication. Il filtro non deve autodistruggersi
con `deleteLater()` quando rinuncia, altrimenti Qt conserva un puntatore scaduto.

## Risultati runtime

Eseguiti nel contenitore Arch `arch-qt612`, Qt pacchetti `6.12.0`, KWin
`6.8.2-2`, utente non privilegiato, output virtuale e rendering software.
Le versioni dei pacchetti della distribuzione non sono una verifica binaria
di identità con tutti i sorgenti upstream: le conclusioni sull'ordine sono
anche corroborate dalle tracce runtime.

| Prova | Esito osservato |
| --- | --- |
| Primo e secondo processo | PID diversi, stesso ID dopo la chiusura ordinata del primo |
| Identità finestra | `main-window` inviata nel protocollo; risposta `xdg_toplevel_session_v1.restored()` |
| Istanza concorrente | Non sottrae l'ID occupato; mantiene una sessione nuova e distinta |
| Ultima istanza chiusa | Avvio ordinario successivo riprende l'ID dell'ultima chiusa |
| `-session ID` dopo la chiusura | Qt usa l'ID esplicito; il successivo avvio ordinario lo riprende |
| `-session ID` con proprietario ancora vivo | Evento `replaced()`, ID del proprietario svuotato; invalidazione condizionale alla sua chiusura |
| Tema sentinella distinto dal plugin | Oggetto theme invariato e hint sentinella presente; font, icone, stile e policy dialoghi invariati rispetto al controllo |
| Tema reale `xdgdesktopportal` | Libreria originale caricata con e senza plugin; font, icone, stile e policy dialoghi nativi identici |
| `AA_DisableSessionManager` | Nessun recupero né salvataggio, ID vuoto, file di stato byte per byte invariato |
| Protocollo non associato su KWin | `QT_WAYLAND_DISABLED_INTERFACES=xdg_session_manager_v1`, anche con ID precedentemente salvato nello stesso scope: nessun intervento |
| Compositor senza protocollo | Piccolo compositor Qt WaylandCompositor con xdg-shell, nessuna global session; app funzionante, ID vuoto, stato invariato |
| Disattivazione esplicita del plugin | Sessione Qt nuova, stato persistito invariato |
| Modulo appena compilato | `QT_DEBUG_PLUGINS` indica il percorso assoluto del `.so` di build; hash registrato; log del filtro prima della creazione del session manager |
| Dipendenze del modulo | `ldd` senza librerie KF, Plasma o KWin; Qt Core/Gui/WaylandClient e relative dipendenze di sistema |

Il controllo usa sia l'uguaglianza dell'ID sia il protocollo, quindi non scambia
un ID soltanto conservato localmente per un ripristino riconosciuto dal server.
`QGuiApplication::isSessionRestored()` rimane **false** nel recupero automatico:
Qt imposta quel flag per `-session`, non quando questo plugin sostituisce il
backend. Il prototipo conserva questa semantica della patch.

Le evidenze della verifica finale sono in `runtime-results.json` e
`runtime-evidence.txt`. I log completi restano nella directory di build
indicata dalle evidenze. La suite è ripetibile con `python tests/runtime.py`.

### Problema Qt riprodotto separatamente

Forzare `QNativeInterface::Private::QWaylandWindow::setSessionRestoreId()` con
`AA_DisableSessionManager` provoca SIGSEGV anche con il plugin **non caricato**.
Il controllo dedicato registra il codice `-11`. Dai sorgenti: il flag fa creare
un semplice `QPlatformSessionManager` in `qsessionmanager.cpp:88`; il percorso
xdg-shell, quando ha un'identità finestra non vuota, usa invece
`QWaylandSessionManager::instance()`, che effettua un cast statico al backend
Wayland. Questa è la spiegazione dai sorgenti del crash osservato, non una
correzione di Qt implementata qui.

La prova valida del flag non assegna quell'identità alla finestra; il plugin
rispetta il flag e non sostituisce il backend. Un'app che usa autonomamente
quell'API privata deve rispettare la stessa condizione.

## Politiche conservate e storage Qt

Porting della logica della patch: primo top-level idoneo, app ID da
`desktopFileName()` o `applicationName()`, scope SHA-256 per macchina, desktop,
socket, runtime directory e schermi; ordine dei nomi monitor normalizzato;
configurazione schermi fissata all'avvio. `WAYLAND_SOCKET` causa rinuncia perché
il descrittore può bypassare il socket dichiarato. Si mantengono lock non
bloccanti per ID, protezione anche delle sessioni nuove ed esplicite, salvataggio
solo in `aboutToQuit`, lock condiviso per l'aggiornamento e invalidazione solo
se il valore salvato è ancora quello della sessione sostituita. Un crash non
equivale a chiusura ordinata e non salva l'ultimo ID.

`KConfig`/`KSharedConfig` sono sostituiti da `QSettings::IniFormat` in
`GenericStateLocation/qt-wayland-session-persistence/sessions.ini`.
La struttura logica è `XdgSessionManagement/<environment hash>/<app ID>`.
Questo preserva le politiche ma **non** il formato o il percorso di KConfig:
nessuna migrazione automatica dei vecchi ID, né accesso ai vecchi file.
Un aggiornamento da plasma-integration deve considerare questo confine di
storage; il primo avvio del plugin può creare una nuova sessione.

Il controllo esplicito `QWaylandSessionManager::session()` è aggiunto per non
ripristinare un vecchio ID quando il protocollo manca: il costruttore Qt può
altrimenti conservare la stringa richiesta senza aver creato una sessione nativa.

La proprietà applicativa `org.kde.plasma.integration.nativeSessionPersistence`
rimane come guardia comune alla patch originale: il primo hook installato
possiede l'integrazione. Il nome non introduce una dipendenza KDE. In presenza
del platform theme già patchato, viene lasciato operare il suo hook; il plugin
non lo sostituisce. Questa coesistenza è una conclusione dal codice, non un
test runtime con il tema Plasma patchato compilato.

## API private e requisiti

| API | Uso |
| --- | --- |
| `QGuiApplicationPrivate::instance()->session_manager` | Individuazione e conservazione del QSessionManager originale |
| `QObjectPrivate::get`, `QSessionManagerPrivate::platformSessionManager` | Accesso e sostituzione del backend |
| `QPlatformIntegration::createPlatformSessionManager` | Richiesta dell'ID salvato prima del primo toplevel |
| `QPlatformSessionManager` | Distruzione del vecchio backend tramite interfaccia QPA |
| `QtWaylandClient::QWaylandSessionManager::session()` | Verifica effettiva della disponibilità del protocollo |
| `QGenericPlugin`, factory IID | Caricamento generico Qt; interfaccia QPA, da trattare come non stabile |
| `QNativeInterface::Private::QWaylandWindow::setSessionRestoreId` | Solo lato applicazione/test, identità finestra stabile prima della shell surface |
| `QGuiApplicationPrivate::platformTheme`, `QPlatformTheme` | Solo nelle sonde, per verificare il tema; il plugin di produzione non li modifica |

Il modulo deve essere ricompilato per la build Qt di destinazione. CMake accetta
la serie 6.12 e rifiuta 6.13+, ma ciò non garantisce compatibilità fra diverse
build/patchlevel di 6.12. QtWaylandClientPrivate e GuiPrivate sono requisiti
espliciti; Widgets e WaylandCompositor servono solo alle prove.

## Limiti e ipotesi

- La finestra deve avere un'identità stabile fornita dall'applicazione. Non viene
  inventata dal plugin, e il caricamento del modulo da solo non risolve questo
  requisito per qualunque applicazione Qt esistente.
- L'ordine è valido per il percorso Qt 6.12 esaminato e per finestre create dopo
  la normale inizializzazione dell'applicazione. Finestre create precocemente
  da un platform theme o da un altro plugin prima dell'installazione del filtro
  non sono recuperabili retroattivamente. Se un altro plugin crea una superficie
  durante l'inizializzazione, prima del session manager, il filtro rinuncia senza
  riprovare; l'ordine dei plugin generici è quindi rilevante.
- Nessuna prova di logout/login completo, riavvio del compositor, ripristino dopo
  reboot, hotplug monitor, crash recovery, sandbox Flatpak o altri compositor
  con supporto del protocollo. La persistenza del server rimane responsabilità
  del compositor. Queste proprietà non sono dimostrate dai test fra processi.
- Font, icone, stile e policy dialoghi sono stati confrontati a runtime; non è
  stata svolta un'ispezione interattiva di tutti i dialoghi nativi o dei portal.
  Il mantenimento del routing verso il tema originale deriva anche dall'assenza
  di sostituzioni del theme e dalla verifica del suo oggetto.
- Nessuna modifica inviata ai remoti, nessuna pubblicazione dell'ambiente.
