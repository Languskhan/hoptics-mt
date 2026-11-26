---
name: build-and-flash
description: Build Hoptics firmware (MT or MC) for a board and flash it over USB with esptool. Use when asked to build, flash, install or update firmware on a Heltec V4/V4-R8, T-Deck Plus or ThinkNode M9.
---

# Build and flash Hoptics firmware

## 1. Pick the target (ask only if unclear)
- Which firmware: Hoptics MT (Meshtastic, repo hoptics-mt) or Hoptics-MC (MeshCore, repo hoptics-mc).
- Which board, and whether this is a **first install** (erases settings) or an **update** (keeps them).
- Envs are listed in the repo's CLAUDE.md.

## 2. Get the firmware
Prefer CI builds over local builds (local takes much longer on Windows).
- Latest main build:
  - MT: `gh run list -R Languskhan/hoptics-mt -w build.yml -L 3`, then
    `gh run download <run-id> -R Languskhan/hoptics-mt -n hoptics-mt-<board>`
  - MC: `git fetch origin builds` and take the files from that branch, or `gh run download` the same way.
- Need a change that isn't pushed yet? Push a branch, wait for the build, download its artifacts.
- `gh` missing: `winget install GitHub.cli`, then `gh auth login`.
- Check the SHA256SUMS.txt that comes with the files before flashing.

## 3. Prepare the PC (first time only)
- `pip install esptool` (needs Python). Check: `esptool version`.
- Find the port: plug the board in and run `esptool chip-id` (it auto-detects), or look in Device Manager
  under Ports (COM#). Close any app using the port (Meshtastic web, MeshCore app, serial monitor).

## 4. Boot mode
- Heltec V4 / V4-R8: hold USER, tap RST, release USER.
- Other boards: try esptool first (most enter download mode on their own). If it can't connect, ask the
  user to put the board in download mode; don't guess button steps.

## 5. Flash (ask the user before running these)
First install, Hoptics MT:
```
esptool --chip esp32s3 -p COM# erase-flash
esptool --chip esp32s3 -p COM# write-flash 0x0 <...>.factory.bin <ota_offset> mt-esp32s3-ota.bin
```
Take `<ota_offset>` from the `manifest.json` in the same folder (0x650000 on the Heltec V4 builds); never guess it.

Update, Hoptics MT (keeps settings): `esptool --chip esp32s3 -p COM# write-flash 0x10000 <...>.update.bin`

First install, Hoptics-MC: erase-flash, then `write-flash 0x0 <...>.factory.bin` (the merged image).
Update, Hoptics-MC: `write-flash <app0> <...>.update.bin`, where `<app0>` is the app0 offset in that env's partition
table (0x10000 on the default tables). Check it before flashing.

## 6. Check it worked
- Reset the board, watch the serial log for about 30 s (`pio device monitor -b 115200 -p COM#` or the
  Meshtastic/MeshCore web console). Look for the Hoptics UI start line and no `Guru Meditation` or `abort`.
- If it crash-loops, decode the backtrace with the matching `.elf` (see the repo README).
- Tell the user what was flashed (firmware, version, board, first install or update) in two lines.

## Rules
- Never flash or erase without the user's OK in this conversation.
- If a step fails twice, stop and show the error instead of trying random fixes.
