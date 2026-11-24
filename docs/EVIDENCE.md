# Evidence behind the design choices

Every decision and claim in this project, with what it rests on. "Code" means a file in the pinned
upstream checkout (meshtastic/firmware `e42ae674`, meshtastic/device-ui `fb3c0699`). "Measured" means
I ran it. Where I couldn't find something, I say so rather than claiming it doesn't exist.

## Why the stock V4 touch kit experience is poor, and what was fixed

| Claim | Evidence |
|---|---|
| The touch kit needs the `heltec-v4-tft` build with "Meshtastic UI" ticked; the plain V4 build gives the button UI | Heltec's product page and wiki: https://heltec.org/project/wifi-lora-32-v4-expansion-housing , https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-expansion-kit/usage-guide |
| A node can stay on BaseUI after reflashing | Code: `src/main.cpp` only starts the TFT UI when `config.display.displaymode == COLOR` (~line 883), and `src/mesh/NodeDB.cpp` only sets COLOR in the *fresh install* defaults (~line 1173). An old saved config keeps BaseUI. Hoptics forces COLOR at boot. |
| Both kits are 240x320 portrait with the same panel driver; the R8 has more RAM for the UI | Code: `variants/esp32s3/heltec_v4/platformio.ini` (`DISPLAY_SIZE=240x320`, `RAM_SIZE=1432`) and `variants/esp32s3/heltec_v4_r8/platformio.ini` (`RAM_SIZE=5120`). R8 is a different pinout and needs its own firmware: https://meshtastic.org/docs/hardware/devices/heltec/ |
| A new UI can reuse the stock plumbing | Code: device-ui `MeshtasticView.h` / `ViewController.h` deliver nodes, packets, telemetry, messages and config to any view; `ViewFactory.cpp` picks the view at compile time. |
| The new UI fits the V4 kit's memory | Measured (harness, LVGL pool sized like the V4 kit): 346 KB used of 1,376 KB, about 700 KB peak during a theme rebuild, back to 346 KB after three rebuilds (no leak). |

## Other custom Meshtastic firmware for this board (checked before building one)

| Project | What it is |
|---|---|
| SnowGlobeio/Heltec-V4-tft | Community builds tracking upstream firmware + MUI, adds a portrait MUI. Last documented 2.7.22 (April 2026). https://github.com/SnowGlobeio/Heltec-V4-tft |
| meshtacore / Jentastic | Meshtastic soft fork with infrastructure tools; no screen changes and no V4 TFT target listed. https://github.com/xejarque/meshtacore-firmware |
| wadamesh-meshtastic (nukes1810) | Despite the name, MeshCore firmware, not Meshtastic. https://github.com/nukes1810/wadamesh-meshtastic |

## Features and why

| Feature | Why / evidence |
|---|---|
| Airtime visibility (channel util, top talkers, traffic mix) | The most common complaint about Meshtastic is chattiness / flooding in busy areas: https://mtlynch.io/first-impressions-of-meshcore/ , https://adrelien.com/meshtastic-vs-meshcore-i-run-both-heres-the-honest-truth-2026/ . Meshtastic uses managed flooding for broadcasts and next-hop routing for DMs since 2.6: https://simeononsecurity.com/articles/meshcore-vs-meshtastic-comparison-guide/ . A UI can't change the protocol, but it can show who is using the air. |
| Live map with packet pings | The live packet maps people like are server-side web tools fed by MQTT: MeshCore's CoreScope (https://github.com/Kpa-clawbot/CoreScope), LetsMesh, and for Meshtastic Live Mesh Map (https://www.rtl-sdr.com/live-mesh-map-a-self-hosted-live-map-for-meshtastic-and-meshcore-lora-mesh-networks/) and MeshView (https://github.com/pablorevilla-meshtastic/meshview). I did not find one that runs *on the node's own screen*. That isn't proof none exists. |
| MQTT view | Each packet carries a `via_mqtt` flag (code: `MeshPacket.via_mqtt`), and the device reports broker connection state (`NetworkConnectionStatus.is_mqtt_connected` in `connection_status.pb.h`). |
| Web page only on Wi-Fi, Bluetooth off then | Meshtastic disables Bluetooth when Wi-Fi is enabled: https://meshtastic.org/docs/configuration/radio/bluetooth/ , https://meshtastic.org/docs/configuration/radio/network/ |
| Web page polls a new JSON snapshot instead of the protobuf API | `/api/v1/fromradio` serves one client at a time (https://github.com/liamcottle/meshtastic-websocket-proxy), so a dashboard polling it would fight with your phone app or web client. |
| Frequency shown on Home | Same slot logic as the stock MUI (code: device-ui `TFTView_320x240.cpp` ~line 6053, `LoRaPresets::getDefaultSlot` + `getRadioFreq`). |
| Random BT PIN on screen | Same approach as stock MUI (code: device-ui `TFTView_320x240.cpp` lines 1089-1093). |
| Flash offsets 0x0 (full) / 0x10000 (update) | Code: Meshtastic's own `bin/device-install.sh` (`FIRMWARE_OFFSET=0x00`) and `bin/device-update.sh` (`UPDATE_OFFSET=0x10000`). |
| City locator data | GeoNames cities database, CC BY 4.0: https://www.geonames.org/ (attribution in README). |

## Discord and loggers

Mature options already exist, so Hoptics adds a small read-only bridge rather than re-implementing them:

| Project | What it does |
|---|---|
| raudette/meshtastic_discord_bridge | Two-way Discord ↔ mesh via a USB-connected radio. https://github.com/raudette/meshtastic_discord_bridge |
| IceNet-01/Mesh-Bridge | Web GUI relay station, Discord webhook and two-way bot, email. https://github.com/IceNet-01/Mesh-Bridge |
| MeshInfo | Map, traceroutes, MQTT log, Discord integration. https://github.com/MeshAddicts/meshinfo |
| austinmesh/bridger | MQTT → InfluxDB metrics. https://github.com/austinmesh/bridger |
| bayme.sh-bot, Meshcord | MQTT → Discord loggers (listed in https://github.com/ShakataGaNai/awesome-meshtastic/) |

The Hoptics bridge needs no Meshtastic library, channel keys or MQTT broker, because it reads the node's
own `/json/hoptics`.

## What is NOT verified yet

- Not run on real hardware. The UI was rendered on a PC harness, and the adapter compiled against
  device-ui on Linux. The ESP32 build runs in GitHub Actions; check its result for each release.
- Touch, display and backlight drivers are the stock ones from device-ui, unchanged.
