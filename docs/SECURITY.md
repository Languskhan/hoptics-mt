# Hoptics MT security notes

Each item gives the problem, the evidence, and what Hoptics MT does about it. Line numbers refer to
meshtastic/firmware at `e42ae674` (2.8.3) unless noted.

## 1. The node's web server has no login and accepts commands from any website (fixed in part)

**Evidence**
- `src/mesh/http/ContentHandler.cpp` registers `/api/v1/toradio` (full radio control, including
  admin/config messages from the local client), `/restart`, `/upload` and `/json/fs/delete/static` on
  **both** the HTTPS and the plain-HTTP server (lines 92-117). There is no authentication check
  anywhere in the file.
- Every API response sets `Access-Control-Allow-Origin: *` (lines 142, 192, 352, 386, 651 and more),
  so a page on *any* website open in a browser on your network can send requests to the node.
- Meshtastic's HTTP API spec lists only "HTTP and HTTPS ... self signed certificates" under Security:
  https://meshtastic.org/docs/development/device/http-api/

**What Hoptics MT does** (`patches/firmware.patch`, only when built with `HOPTICS_UI`)
- CORS is limited to `https://client.meshtastic.org`. The official hosted client keeps working, and
  the client served from the node itself is same-origin so it doesn't need CORS. Other websites can no
  longer drive the radio from your browser.
- `/restart`, `/upload` (replaces web files on the node) and `/json/fs/delete/static` are registered on
  HTTPS only.
- New `/hoptics` page: `Content-Security-Policy` allows no scripts from anywhere and no network calls
  except back to the node. Also `X-Frame-Options: DENY`, `nosniff`, `no-referrer` and `no-store`.
- New `/json/hoptics`: read-only, GET only, no CORS header (other sites can't read it from your browser).

**Not fixed (upstream design)**: anyone *on your Wi-Fi* can still use `/api/v1/toradio` and the TCP API
(port 4403) without a password. That's how the official apps connect. Keep Wi-Fi-enabled nodes on a
trusted network (or a separate IoT VLAN).

## 2. Hostile names and messages can't inject code into the web page

Node names and chat come from strangers over the air. The node escapes `"`, `\`, control characters and
`<` in `/json/hoptics` (`HopticsUI::buildSnapshotJson`), and the page renders only with `textContent`
and canvas, never `innerHTML`.
**Tested:** a node named `</script><script>alert(2)</script>` and a message
`<img src=x onerror=alert(1)>` were rendered by headless Chrome as plain text, with zero `<img>` or
`<script>` elements created.

## 3. Private messages stay private

- `/json/hoptics` and the bridge **never include direct messages**. The snapshot builder skips every
  conversation whose key isn't a channel.
- Channel chat is only included after you turn on Settings → Web page → "Show channel chat on web"
  (off by default).
- DMs are sent with Meshtastic's public-key encryption when the other node's key is known, the same as
  the stock UI. Meshtastic DMs have used X25519 public-key crypto since 2.5
  (https://meshtastic.org/blog/introducing-new-public-key-cryptography-in-v2_5/). The DM-spoofing
  downgrade (CVE-2025-53627) is fixed in 2.7.15+
  (https://nvd.nist.gov/vuln/detail/CVE-2025-53627), and this build is 2.8.3.
- **Do a full-erase flash once.** Some vendors' factory flashing gave many devices identical key pairs,
  so their DMs could be decrypted (CVE-2025-52464,
  https://www.sentinelone.com/vulnerability-database/cve-2025-52464/). A full erase makes the node
  generate its own keys.

## 4. Bluetooth pairing

Turning Bluetooth on from Hoptics sets `FIXED_PIN` with a new random 6-digit PIN from the ESP32
hardware RNG (`esp_random()`), shown in Settings. The stock MUI does the same thing (`random(100000,
999999)`, device-ui `TFTView_320x240.cpp` lines 1089-1093). Meshtastic turns Bluetooth off whenever
Wi-Fi is on (https://meshtastic.org/docs/configuration/radio/bluetooth/).

## 5. MQTT

Hoptics only *shows* MQTT (the Live filter, violet pings, the status-bar pill). It doesn't change MQTT
behaviour. Meshtastic's own guidance applies: turn on **Encryption Enabled** (otherwise packets reach
the broker unencrypted even on encrypted channels), and prefer TLS and a private broker. The public
broker is readable by anyone subscribed:
https://meshtastic.org/docs/configuration/module/mqtt/ and
https://meshtastic.org/docs/software/apple/user/mqtt/

## 6. Discord bridge

- The webhook URL is a secret, so it's read from an environment variable on the bridge computer and
  never stored on the radio.
- The bridge refuses URLs that aren't `https://discord.com/api/webhooks/...` (or the discordapp/canary/ptb
  hosts).
- Every post sets `allowed_mentions: {"parse": []}`, so mesh text containing `@everyone`, `@here` or
  role/user mentions can't ping your server. Discord's docs recommend `allowed_mentions` for
  user-generated content: https://docs.discord.com/developers/resources/webhook
- Discord markdown in mesh text is escaped, posts are truncated, and HTTP 429 rate limits are honoured.
  **Tested** against a fake endpoint: `@everyone` arrived with mentions disabled and `**bold**` escaped.
