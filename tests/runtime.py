#!/usr/bin/env python3
"""Run inside the Arch container as its ordinary development user."""
import json
import os
from pathlib import Path
import signal
import re
import resource
import hashlib
import subprocess
import sys
import tempfile
import time

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
result = Path(tempfile.mkdtemp(prefix="runtime-", dir=build))
runtime = result / "run"
runtime.mkdir(mode=0o700)
env = os.environ.copy()
for key in ("QT_QPA_GENERIC_PLUGINS", "QT_QPA_PLATFORMTHEME", "QT_PLUGIN_PATH",
            "WAYLAND_SOCKET", "QT_QPA_PLATFORM", "QT_WAYLAND_DISABLED_INTERFACES"):
    env.pop(key, None)
env.update(XDG_RUNTIME_DIR=str(runtime), HOME=str(result / "home"),
           XDG_STATE_HOME=str(result / "state"), XDG_CONFIG_HOME=str(result / "config"),
           XDG_DATA_HOME=str(result / "data"), WAYLAND_DISPLAY="wayland-test",
           XDG_CURRENT_DESKTOP="IndependentTest", KWIN_COMPOSE="Q", QT_QUICK_BACKEND="software")
Path(env["HOME"]).mkdir()
compositor_log = (result / "kwin.log").open("w")
compositor = subprocess.Popen(["dbus-run-session", "--", "kwin_wayland", "--virtual",
    "--socket", "wayland-test", "--no-lockscreen", "--no-global-shortcuts", "--no-kactivities"],
    env=env, stdout=compositor_log, stderr=subprocess.STDOUT, start_new_session=True)
reports = {}

def start(name, extra=None, args=()):
    e = env.copy()
    e.update(QT_QPA_PLATFORM="wayland", QT_PLUGIN_PATH=str(build / "plugins"),
             QT_QPA_PLATFORMTHEME="sessiontest", QT_QPA_GENERIC_PLUGINS="waylandsessionpersistence",
             QT_LOGGING_RULES="qt.wayland.session.persistence.debug=true", QT_DEBUG_PLUGINS="1",
             QT_FORCE_STDERR_LOGGING="1", QT_LOGGING_TO_CONSOLE="1",
             WAYLAND_DEBUG="client", PROBE_REPORT=str(result / (name + ".json")))
    e.update(extra or {})
    log = (result / (name + ".log")).open("w")
    p = subprocess.Popen([str(build / "session-probe"), *args], env=e, stdout=log, stderr=subprocess.STDOUT)
    return p, log

def finish(name, job):
    p, log = job
    try:
        code = p.wait(timeout=15)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        raise
    finally:
        log.close()
    assert code == 0, (name, code, str(result / (name + ".log")))
    d = json.loads((result / (name + ".json")).read_text())
    reports[name] = d
    assert d["themeUnchanged"], (name, d)
    if not name.startswith("portal-"):
        assert d["theme"] == "session-test-original-theme", (name, d)
    if "baseline" in reports and not name.startswith("portal-"):
        for field in ("font", "iconTheme", "style", "nativeFileDialogPolicy"):
            assert d[field] == reports["baseline"][field], (name, field, d)
    assert d["platform"] == "wayland", (name, d)
    assert d["windowIdentity"] == ("" if name == "disable-session-manager" else "main-window"), (name, d)
    print(name, d, flush=True)
    return d

def run(name, extra=None, args=()):
    return finish(name, start(name, extra, args))

try:
    for _ in range(100):
        if (runtime / "wayland-test").exists():
            break
        assert compositor.poll() is None, "KWin exited: " + str(result)
        time.sleep(.05)
    else:
        raise RuntimeError("KWin did not open socket: " + str(result))
    run("baseline", {"QT_QPA_GENERIC_PLUGINS": ""})
    first = run("first")
    assert first["session"], "No session protocol support"
    second = run("second")
    assert second["session"] == first["session"] and first["pid"] != second["pid"]
    active = start("active", {"PROBE_DELAY": "1000"})
    time.sleep(.3)
    concurrent = start("concurrent", {"PROBE_DELAY": "1800"})
    a = finish("active", active)
    c = finish("concurrent", concurrent)
    assert a["session"] == first["session"] and c["session"] != a["session"] and c["session"]
    last = run("last-closed")
    assert last["session"] == c["session"]
    explicit = run("explicit-after-close", args=("-session", a["session"]))
    assert explicit["session"] == a["session"] and explicit["restored"]
    after = run("after-explicit")
    assert after["session"] == explicit["session"]
    owner = start("replaced-owner", {"PROBE_DELAY": "1600"})
    time.sleep(.3)
    takeover = run("explicit-takeover", args=("-session", after["session"]))
    replaced = finish("replaced-owner", owner)
    assert takeover["session"] == after["session"] and not replaced["session"]
    assert ".replaced()" in (result / "replaced-owner.log").read_text()
    next_session = run("after-invalidation")
    assert next_session["session"] and next_session["session"] != takeover["session"]
    before = (result / "state/qt-wayland-session-persistence/sessions.ini").read_bytes()
    disabled = run("disable-session-manager", {"PROBE_DISABLE_SM": "1"})
    assert not disabled["session"]
    assert before == (result / "state/qt-wayland-session-persistence/sessions.ini").read_bytes()
    portal_baseline = run("portal-baseline", {
        "QT_QPA_PLATFORMTHEME": "xdgdesktopportal", "QT_QPA_GENERIC_PLUGINS": ""})
    portal_enabled = run("portal-enabled", {"QT_QPA_PLATFORMTHEME": "xdgdesktopportal"})
    for field in ("theme", "font", "iconTheme", "style", "nativeFileDialogPolicy"):
        assert portal_baseline[field] == portal_enabled[field], field
    assert 'libqxdgdesktopportal.so" loaded library' in (result / "portal-enabled.log").read_text()
    no_protocol = run("unbound-protocol", {"QT_WAYLAND_DISABLED_INTERFACES": "xdg_session_manager_v1"})
    assert not no_protocol["session"]
    assert before == (result / "state/qt-wayland-session-persistence/sessions.ini").read_bytes()
    run("plugin-disabled", {"QT_WAYLAND_SESSION_PERSISTENCE_DISABLE": "1"})
    assert before == (result / "state/qt-wayland-session-persistence/sessions.ini").read_bytes()
    log = (result / "second.log").read_text()
    assert str(build / "plugins/generic/libwaylandsessionpersistence.so") in log
    assert "generic hook installed; session manager exists: false" in log
    assert re.search(r"xdg_toplevel_session_v1#\d+\.restored\(\)", log)
    assert '"main-window"' in log
    # An actual compositor that never advertises the session extension.
    no_log = (result / "no-session-compositor.log").open("w")
    no_env = env | {"QT_QPA_PLATFORM": "offscreen"}
    no_server = subprocess.Popen([str(build / "no-session-compositor")], env=no_env,
                                 stdout=no_log, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            if (runtime / "wayland-no-session").exists():
                break
            assert no_server.poll() is None, "No-session compositor exited"
            time.sleep(.05)
        no_support = run("compositor-without-protocol", {"WAYLAND_DISPLAY": "wayland-no-session"})
        assert not no_support["session"]
        assert '"xdg_session_manager_v1"' not in (result / "compositor-without-protocol.log").read_text()
        assert before == (result / "state/qt-wayland-session-persistence/sessions.ini").read_bytes()
    finally:
        no_server.terminate()
        no_server.wait(timeout=5)
        no_log.close()
    # Upstream Qt regression control, with the plugin entirely absent.
    regression = start("qt-aa-identity-control", {
        "QT_QPA_GENERIC_PLUGINS": "", "PROBE_DISABLE_SM": "1", "PROBE_FORCE_IDENTITY": "1"})
    reg_process, reg_log = regression
    reg_code = reg_process.wait(timeout=10)
    reg_log.close()
    (result / "qt-aa-identity-control.json").write_text(json.dumps({"exitCode": reg_code}))
    print("Qt AA_DisableSessionManager + forced private window identity, no plugin:", reg_code, flush=True)
    plugin = build / "plugins/generic/libwaylandsessionpersistence.so"
    (result / "build-identity.json").write_text(json.dumps({"plugin": str(plugin),
        "sha256": hashlib.sha256(plugin.read_bytes()).hexdigest()}, indent=2))
    print("PASS", result, flush=True)
finally:
    (result / "reports.json").write_text(json.dumps(reports, indent=2))
    os.killpg(compositor.pid, signal.SIGTERM)
    try:
        compositor.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(compositor.pid, signal.SIGKILL)
        compositor.wait()
    compositor_log.close()
