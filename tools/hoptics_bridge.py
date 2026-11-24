#!/usr/bin/env python3
"""
Hoptics bridge - forwards what your Hoptics MT node sees to Discord and/or a log file.

Runs on a computer on the same Wi-Fi as the node (a Raspberry Pi is ideal). It polls the node's
read-only /json/hoptics endpoint, so it needs no Meshtastic library, no channel keys and no MQTT broker.

Why the Discord webhook lives here and not on the radio:
  * a webhook URL is a password - anyone holding it can post to your channel. Keeping it on the Pi
    means it is never stored on a device that can be lost in the field or read over the air/USB;
  * Meshtastic ESP32 nodes turn Bluetooth off when Wi-Fi is on, and the radio's job is the mesh.

Posts:  new nodes heard, channel chat (only if you enabled "Show channel chat on web" on the node),
        and an optional periodic status line. Direct messages are never exported by the node.
Logs:   --log FILE appends one JSON line per new packet / message / node (easy to feed Grafana,
        InfluxDB, Loki, a spreadsheet...).

Standard library only.  Examples:
  HOPTICS_DISCORD_WEBHOOK='https://discord.com/api/webhooks/...' python3 hoptics_bridge.py 192.168.1.57
  python3 hoptics_bridge.py 192.168.1.57 --log mesh.jsonl --no-discord
  python3 hoptics_bridge.py 192.168.1.57 --status-every 3600
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DISCORD_HOSTS = {"discord.com", "discordapp.com", "canary.discord.com", "ptb.discord.com"}


def fetch(node, timeout=10):
    url = node if node.startswith("http") else f"http://{node}"
    url = url.rstrip("/") + "/json/hoptics"
    with urllib.request.urlopen(url, timeout=timeout) as r:
        if r.status != 200:
            raise RuntimeError(f"HTTP {r.status}")
        return json.loads(r.read().decode("utf-8"))


def check_webhook(url, allow_any):
    u = urllib.parse.urlparse(url)
    if allow_any:
        return
    if u.scheme != "https" or u.hostname not in DISCORD_HOSTS or not u.path.startswith("/api/webhooks/"):
        sys.exit("refusing webhook URL: expected https://discord.com/api/webhooks/... (use --allow-any-webhook to override)")


def discord_post(url, text, username):
    body = {
        "content": text[:1900],
        "username": username,
        # never let mesh text ping @everyone / @here / roles / users in your server
        "allowed_mentions": {"parse": []},
    }
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "User-Agent": "hoptics-bridge/0.1"})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=10):
                return True
        except urllib.error.HTTPError as e:
            if e.code == 429:  # rate limited - respect Retry-After
                time.sleep(float(e.headers.get("Retry-After", "2")))
                continue
            print(f"discord: HTTP {e.code}", file=sys.stderr)
            return False
        except Exception as e:  # network hiccup
            print(f"discord: {e}", file=sys.stderr)
            time.sleep(2)
    return False


def clean(s, n=200):
    # strip control characters and neutralise markdown that could hide or fake content
    s = "".join(ch for ch in str(s) if ch >= " " or ch == "\n")
    for ch in "`*_~|>":
        s = s.replace(ch, "\\" + ch)
    return s[:n]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("node", help="node IP or hostname, e.g. 192.168.1.57")
    ap.add_argument("--interval", type=float, default=10, help="poll seconds (default 10, minimum 3)")
    ap.add_argument("--log", help="append JSON lines to this file")
    ap.add_argument("--no-discord", action="store_true", help="log only")
    ap.add_argument("--status-every", type=int, default=0, help="post a status summary every N seconds (0 = off)")
    ap.add_argument("--username", default="Hoptics MT", help="name shown on Discord posts")
    ap.add_argument("--allow-any-webhook", action="store_true", help=argparse.SUPPRESS)  # for local testing only
    ap.add_argument("--once", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()

    hook = None if a.no_discord else os.environ.get("HOPTICS_DISCORD_WEBHOOK")
    if not a.no_discord and not hook:
        sys.exit("set HOPTICS_DISCORD_WEBHOOK (kept in the environment, never on the command line or the radio), or use --no-discord")
    if hook:
        check_webhook(hook, a.allow_any_webhook)
    if not hook and not a.log:
        sys.exit("nothing to do: give --log FILE and/or a Discord webhook")

    known_nodes, seen_live, seen_chat = None, set(), set()
    last_status = time.time()
    logf = open(a.log, "a", encoding="utf-8") if a.log else None

    def log(kind, obj):
        if logf:
            logf.write(json.dumps({"ts": int(time.time()), "kind": kind, **obj}, ensure_ascii=False) + "\n")
            logf.flush()

    def name(d, nid):
        for n in d["nodes"]:
            if n["id"] == nid:
                return f'{n["long"] or nid} ({n["short"]})'
        return nid

    while True:
        try:
            d = fetch(a.node)
        except Exception as e:
            print(f"node unreachable: {e}", file=sys.stderr)
            time.sleep(max(3, a.interval))
            continue

        ids = {n["id"] for n in d["nodes"]}
        if known_nodes is None:
            known_nodes = ids  # don't announce everyone at start-up
            seen_live = {(l["from"], l["t"], l["port"]) for l in d["live"]}
            seen_chat = {(m["from"], m["t"], m["text"]) for m in (d.get("chat") or [])}
        else:
            for n in d["nodes"]:
                if n["id"] not in known_nodes:
                    log("node", n)
                    if hook:
                        hops = "direct" if n.get("hops") == 0 else (f'{n["hops"]} hop' + ("" if n["hops"] == 1 else "s") if n.get("hops") is not None else "? hops")
                        discord_post(hook, f'**New node heard:** {clean(n["long"] or n["id"])} `{clean(n["short"],8)}` - {clean(n["role"])}, {hops}', a.username)
            known_nodes |= ids
            for l in reversed(d["live"]):
                k = (l["from"], l["t"], l["port"])
                if k not in seen_live:
                    seen_live.add(k)
                    log("packet", l)
            for m in sorted(d.get("chat") or [], key=lambda m: m["t"]):
                k = (m["from"], m["t"], m["text"])
                if k not in seen_chat:
                    seen_chat.add(k)
                    log("chat", m)
                    if hook:
                        discord_post(hook, f'**{clean(name(d, m["from"]), 60)}** in #{clean(m["ch"], 20)}: {clean(m["text"], 240)}', a.username)
            if len(seen_live) > 2000:
                seen_live = set(list(seen_live)[-500:])

        if hook and a.status_every and time.time() - last_status >= a.status_every:
            last_status = time.time()
            s = d["stats"]
            online = sum(1 for n in d["nodes"] if not n.get("me") and n.get("last") and d["now"] - n["last"] <= 7200)
            discord_post(hook, f'Mesh status: {online} nodes online, channel util {s.get("chutil") or 0:.0f}%, '
                               f'{s["pkts"]} packets this session ({s["mqtt"]} via MQTT).', a.username)
        if a.once:
            break
        time.sleep(max(3, a.interval))


if __name__ == "__main__":
    main()
