# Hoptics MT

Custom Meshtastic touch UI (replaces BaseUI and MUI). Upstream Meshtastic is NOT copied here: the build
fetches it at a pinned commit and applies our patches.

## Layout
- `patches/device-ui.patch`, `patches/firmware.patch`: all firmware changes live here.
- `web/hoptics.html`: the on-device web dashboard (embedded by `tools/embed_web.py`).
- `site/`: the website (GitHub Pages). Add boards in `site/nodes.json`, news in `site/updates.json`.
- `harness/`: renders every screen on a PC, no radio needed (`harness/build_and_run.sh`).
- `.github/workflows/build.yml`: the real build recipe. Pinned upstream ref is `FIRMWARE_REF` there.

## Boards (PlatformIO env)
- Heltec V4 Expansion Kit: `heltec-v4-tft`
- Heltec V4-R8 Expansion Kit V2: `heltec-v4-r8-tft` (bigger history flags, see build.yml)
- LilyGO T-Deck Plus: `t-deck-plus-tft` (beta, landscape)
- Elecrow ThinkNode M9: `thinknode_m9-tft` (beta, landscape)

## Build
- Default: let GitHub Actions build (push a branch or run the workflow), then download artifacts with
  `gh run download`. Local builds take a long time on Windows.
- Local: follow the steps in build.yml in Git Bash (fetch upstream at FIRMWARE_REF, apply both patches,
  embed the web page, `pio run -e <env>`).
- Screen changes: check with the harness before building firmware.

## Flash
Use the `build-and-flash` skill. Never flash without asking first.

## Rules
- Firmware is beta; keep every board marked beta on the site.
- Most code only runs on the board: don't demand tests first. The user tests on hardware.
- Keep changes small and inside the patches; don't edit upstream files outside them.
