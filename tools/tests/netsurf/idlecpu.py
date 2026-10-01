#!/usr/bin/env python3
# tools/tests/netsurf/idlecpu.py -- the CPU an idle Jet Browser burns (docs/06 §32): open each
# page in the PC bench's NetSurf (host.mk), let it load and settle, then sample the process's CPU
# time (/proc/<pid>/stat: user + system) over an idle window, nothing done to the page.
#
#   python3 tools/tests/netsurf/idlecpu.py [options] page-or-url ...
#     --bin PATH        the bench's netsurf (default $OUT/build/netsurf, OUT=/tmp/nsbench)
#     --settle S        seconds before the window (default 15)
#     --window S        the window (default 10)
#     --modes 0,1       the NS_GPU values to measure (default "0,1")
#     --screen WxH      the simulated screen (default 1920x1080)
#     --state S         the window's state during the window: focused (default), unfocused,
#                       hidden (minimised) -- the desktop simulator's "winstate" step
#     --floor           first the bench's own floor: pages/basic.html (no animation, no
#                       script; the simulator's loop turns every 16 ms -- on the Pi the
#                       loop sleeps until an event or a timer)
#     --env K=V         more environment (repeatable)
#     --max PCT         fail (exit 1) when a measure is above PCT % of a core
#
# Prints "<page> NS_GPU=<g> [state]: <s> s CPU in <w> s (<pct> % of a core)".
#
#   OUT=/tmp/nsbench make -f tools/tests/netsurf/host.mk OUT=/tmp/nsbench/build -j8
#   python3 tools/tests/netsurf/idlecpu.py --floor ~/nssites/kotonstudio.com/fr/index.html
import os, sys, time, signal, subprocess, argparse

ap = argparse.ArgumentParser()
ap.add_argument("pages", nargs="+")
ap.add_argument("--bin", default=os.path.join(os.environ.get("OUT", "/tmp/nsbench"), "build", "netsurf"))
ap.add_argument("--settle", type=float, default=15)
ap.add_argument("--window", type=float, default=10)
ap.add_argument("--modes", default="0,1")
ap.add_argument("--screen", default="1920x1080")
ap.add_argument("--state", default="focused", choices=["focused", "unfocused", "hidden"])
ap.add_argument("--floor", action="store_true")
ap.add_argument("--env", action="append", default=[])
ap.add_argument("--max", type=float, default=0)
a = ap.parse_args()

tick = os.sysconf("SC_CLK_TCK")
here = os.path.dirname(os.path.abspath(__file__))
state = {"focused": 1, "unfocused": 0, "hidden": 4}[a.state]	# KAPI_WIN_KEYS / 0 / MINIMISED

def cpu(pid):
    f = open("/proc/%d/stat" % pid).read().rsplit(")", 1)[1].split()
    return (int(f[11]) + int(f[12])) / tick

def measure(page, g):
    url = page if "://" in page else "file://" + os.path.realpath(page)
    env = dict(os.environ)
    env.update(SIM_SCREEN=a.screen, SIM_SLEEP="1", SIM_POS="0,0", SIM_ARGS=url, NS_GPU=g)
    turns = int((a.settle + a.window + 30) * 60)
    # (each "wait" ~16-20 ms: the window's state set a little before the window; the
    # process is killed at its end)
    before = int(max(a.settle - 2, 1) * 50)
    steps = "wait;" * before
    if state != 1:
        steps += "winstate %d;" % state
    env["SIM"] = steps + "wait;" * turns
    env.update(kv.split("=", 1) for kv in a.env)
    p = subprocess.Popen([a.bin], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(a.settle)
    if p.poll() is not None:
        return None
    c1 = cpu(p.pid); t1 = time.time()
    time.sleep(a.window)
    c2 = cpu(p.pid); t2 = time.time()
    p.send_signal(signal.SIGKILL); p.wait()
    return c2 - c1, t2 - t1

fail = 0
pages = ([os.path.join(here, "pages", "basic.html")] if a.floor else []) + a.pages
for page in pages:
    for g in a.modes.split(","):
        r = measure(page, g)
        name = ("(floor) " if a.floor and page == pages[0] else "") + page
        if r is None:
            print("%s NS_GPU=%s: exited" % (name, g)); fail = 1; continue
        pct = 100.0 * r[0] / r[1]
        bad = a.max and pct > a.max
        fail |= bool(bad)
        print("%s NS_GPU=%s %s: %.2f s CPU in %.0f s (%.1f %% of a core)%s"
              % (name, g, a.state, r[0], r[1], pct, "  FAIL" if bad else ""), flush=True)
sys.exit(fail)
