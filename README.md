# Hoptics MT

A from-scratch touch UI for **Meshtastic** on the Heltec 2.8" touch kits, built because the stock
screen experience on the V4 Expansion Kit wasn't good enough. It's a personal build, not affiliated
with the Meshtastic project.

| Board | Build target | Notes |
|---|---|---|
| Heltec WiFi LoRa 32 **V4 Expansion Kit** (original, touch) | `heltec-v4-tft` | 2 MB PSRAM, 60-minute graphs |
| Heltec **V4-R8 Expansion Kit V2** (touch, SD slot) | `heltec-v4-r8-tft` | 8 MB PSRAM, 120-minute graphs, more nodes |

It replaces both BaseUI and MUI. Everything else (radio, routing, encryption, MQTT, the phone apps)
is upstream Meshtastic 2.8.3, unchanged.

![preview](docs/screenshots/preview.png)

## What you get

**First boot**: a setup wizard asks for your **region** (the radio stays silent until it's set), your
**names** and a **theme**. No phone app needed.

**On the device (6 tabs)**
- **Home**: nodes online, channel utilisation, your airtime, packets per minute, a 30-minute activity
  graph, radio summary (region, preset, frequency, hop limit, power) and last-heard list.
- **Live**: every packet as it's heard, like a DMR hotspot last-heard list: who, packet type, direct
  or how many hops, RSSI/SNR. Filter All / RF only / **MQTT only** (MQTT rows have a violet edge).
- **Map**: nodes that share GPS are plotted around you on an OpenStreetMap street map (themed to match:
  dark, daylight, night-red, amber), with range rings. Drag to look around, Fit to come back. Tiles
  download over Wi-Fi only for what you are looking at (OSM's tile policy forbids prefetching), are
  checked against pinned root CAs, and are saved on flash (up to 1.5 MB, always leaving 1 MB free) so
  viewed areas still show without Wi-Fi. Settings > MAP turns it off or clears the saved tiles. Direct
  neighbours get a link line coloured by SNR, and **every packet makes its sender ping**, so you can
  watch where traffic comes from. RF pings are coloured by packet type, MQTT pings are violet, and a
  direct packet flashes a beam to you. Tap a dot for that node.
- **Chat**: channels and direct messages, bubbles with delivery ticks, on-screen keyboard, and pop-ups
  for new messages.
- **Nodes**: sort by recent, signal, hops or name. Node detail shows signal, battery, channel use,
  position, distance, an SNR history graph, and buttons for **DM**, **Trace route** and **Request position**.
- **Stats**: channel load and airtime over time, packets per minute, RSSI/SNR of recent packets,
  nodes by hop count, traffic mix, and **top talkers** (who is using the airtime).
- **Settings** (gear icon): theme, brightness, screen timeout, message pop-ups, names, region, preset,
  role, Bluetooth (with a fresh random PIN shown on screen), web chat sharing, reboot and power off
  (two-tap confirm).
- **City locator** (offline, like WADAMESH): a "Now in <town>" pop-up when you arrive somewhere new,
  your town on Home, and "Near: 13 km NE of <town>" for other nodes. Uses GeoNames towns of 5,000+
  people built into the firmware, so it needs no internet. It's also shown on the web page.
- **Themes**: Midnight (dark), **Daylight** (high-contrast dark-on-white for direct sun), **Night Red**
  (red-only, keeps night vision) and Amber terminal.

**On your network** (when the node is on Wi-Fi): open `http://<node-ip>/hoptics` for a live dashboard
in the same style: tiles, live map with pings, traffic feed with the MQTT filter, charts, sortable
node table, and day/night toggle (`/hoptics?day` to bookmark day mode). It's read-only.

**Discord and loggers**: `tools/hoptics_bridge.py` runs on any PC or Pi on the same Wi-Fi. It posts new
nodes and (optionally) channel chat to a Discord webhook, and/or writes JSON-lines logs for Grafana,
InfluxDB or a spreadsheet. See [Discord / logging](#discord--logging).

## Install (easiest)

Open the **one-click installer** page (GitHub Pages for this repo, live once the repo is public) in Chrome
or Edge, plug the kit in, hold USER, tap RST, and press **Install** for your kit. Choose **Erase** the first
time. That's it: the setup wizard takes over on the screen. Updates use the same page without erasing.

## Flashing by hand

1. Download the `.factory.bin` for your board from the latest release (or the build artifacts).
2. Put the board in bootloader mode: hold **USER**, tap **RST**, release **USER**.
3. Flash the **factory** image at `0x0` **with a full erase**. These are the same offsets Meshtastic's own
   `bin/device-install.sh` (factory at `0x00`) and `bin/device-update.sh` (update at `0x10000`) use:
   ```bash
   esptool --chip esp32s3 erase-flash
   esptool --chip esp32s3 write-flash 0x0 hoptics-mt-<board>-mt2.8.3.factory.bin
   ```
   (older esptool spells these `erase_flash` / `write_flash`). Espressif's browser flasher
   (https://espressif.github.io/esptool-js/) works too: Erase, then add the file at offset `0x0`.
   - A full erase also makes the radio generate **new encryption keys**. Some vendors' factory
     flashing produced devices with duplicate key pairs (CVE-2025-52464), so this is worth doing once.
4. After first boot, open **Settings → Radio → Region** and set your region (US etc.).
5. Later updates: `esptool --chip esp32s3 write-flash 0x10000 hoptics-mt-<board>-...update.bin` (no erase), and your settings are kept.

The official Meshtastic web client normally served at `http://<node-ip>/` lives in a separate file-system
image that this build doesn't ship. Use https://client.meshtastic.org, the phone apps, or `/hoptics`.

Flashing back to stock Meshtastic works as usual (web flasher, "Heltec V4 TFT", MUI on).

## Updates, stable and beta

- **Tested** builds (every push and release) are built on the Meshtastic version listed in
  `.github/workflows/build.yml`, which these patches were written and checked against.
- **Weekly upstream builds** (`upstream.yml`) put Hoptics on top of the **newest Meshtastic stable**
  *and* **newest alpha/beta**, read from the same release list Meshtastic's flasher uses. They show up as
  workflow artifacts named `...-stable` and `...-beta`. If a new upstream release breaks our patches,
  that run fails instead of shipping something broken.
- **Over Wi-Fi**: the installer also writes Meshtastic's unified OTA loader (same file and slot as
  upstream), so ESP32 Wi-Fi/BLE OTA works. Push an `...update.bin` from the Meshtastic Android app's
  firmware update screen or the Meshtastic Python CLI's ESP32 Wi-Fi OTA (meshtastic/python PR #898).
  The node can't safely download and flash itself: the 16 MB layout has one app slot plus the loader, so
  there's nowhere to stage a self-download while running.

## Security

Short version (details and sources in [docs/SECURITY.md](docs/SECURITY.md)):
- Stock Meshtastic's built-in web server has **no login**, and its API answers **any website** your
  browser has open (`Access-Control-Allow-Origin: *`). Hoptics MT keeps the API for the official web
  client, but only lets `client.meshtastic.org` talk to it from a browser. It also makes **reboot,
  file upload and file delete HTTPS-only**.
- The `/hoptics` page and `/json/hoptics` are read-only and send strict browser security headers. All
  names and messages are rendered as plain text; a hostile node name or message can't inject script
  (tested).
- **Direct messages are never exported** to the web page or bridge. Channel chat is only shared if you
  switch on Settings → Web page → "Show channel chat on web".
- Bluetooth pairing uses a **new random 6-digit PIN** each time you enable it, shown in Settings.
- The Discord webhook stays on the computer running the bridge, never on the radio. The bridge
  blocks `@everyone` / role pings and escapes Discord markdown.
- Not fixed (it's upstream Meshtastic): the TCP API on port 4403 has no authentication when Wi-Fi is
  on. Keep Wi-Fi-enabled nodes on a network you trust.

## Discord / logging

```bash
# on a Pi on the same Wi-Fi as the node
export HOPTICS_DISCORD_WEBHOOK='https://discord.com/api/webhooks/...'   # keep it out of shell history/scripts you share
python3 tools/hoptics_bridge.py 192.168.1.57 --status-every 3600 --log mesh.jsonl
python3 tools/hoptics_bridge.py 192.168.1.57 --no-discord --log mesh.jsonl   # logging only
```
For a two-way Discord bot or MQTT-based logging there are mature projects already; see
[docs/EVIDENCE.md](docs/EVIDENCE.md#discord-and-loggers).

## How it's built

- `patches/device-ui.patch`: adds `HopticsUI` (pure LVGL screens, no protobufs) and `HopticsView`
  (adapter to the device-ui controller), enables LVGL charts and extra font sizes, and makes the view
  factory pick Hoptics when `-DHOPTICS_UI` is set.
- `patches/firmware.patch`: forces the colour UI at boot (stock firmware can leave a V4 TFT stuck
  on BaseUI if the saved config says so), adds `/hoptics` + `/json/hoptics`, and applies the web hardening.
- `web/hoptics.html`: the web dashboard (embedded into the firmware at build time).
- `.github/workflows/build.yml`: fetches pinned upstream firmware and device-ui, applies the patches,
  and builds both boards.
- `harness/`: renders every screen on a PC with fake data (that's how the screenshots were made).
  Measured LVGL memory: about 350 KB in use of the V4 kit's 1.4 MB pool, peaking at about 700 KB during
  a theme switch, with no growth after repeated switches.

## Tested so far

- **Rendering**: every screen in all 4 themes, rendered on a PC with the real UI code (see `harness/`).
- **Stress simulation** (`harness/stress.cpp`, built with AddressSanitizer + UBSan): an 8-hour session with
  400 nodes against the 300-node cap (eviction tested), hostile/emoji/oversized names, 111k packets
  including a 30-packets-per-second flood every hour, 1,253 messages, constant tab and overlay switching,
  the keyboard, node removals, and a theme switch every 15 minutes. **No crashes, no sanitizer errors,
  and LVGL memory flat at about 466 KB** after fixing an unbounded DM-conversation list the simulation found.
- **City lookup**: 20,000 random positions checked against brute force on a 50,000-town table, all matched.
- **Web page**: rendered in headless Chrome under the same security headers the node sends, with an
  injection test (hostile names and messages stay plain text).
- **Discord bridge**: run against a fake webhook (mentions blocked, markdown escaped, JSON log written).
- **Adapter**: compiles against the real device-ui sources (Linux build).

## Status

v0.1.0: the UI has been rendered and exercised on a PC, and the adapter compiles against device-ui.
**Not yet tested on hardware.** The ESP32 builds run in GitHub Actions. First-flash feedback welcome.

**Next (planned)**: Wi-Fi file manager for the V4-R8's SD card (logs, map tiles), and optional offline
map tiles on the R8.

License: GPL-3.0 (same as Meshtastic firmware and device-ui). City data © GeoNames (https://www.geonames.org),
CC BY 4.0.
