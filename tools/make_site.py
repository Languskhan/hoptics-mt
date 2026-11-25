#!/usr/bin/env python3
"""Build the one-click web installer (GitHub Pages) from a release's files.

usage: make_site.py <release-files-dir> <site-dir> <tag>

For every board it finds a factory image for, it writes site/firmware/<board>/ with an ESP Web Tools
manifest. The factory image is split around the NVS partition (read from the image's own partition
table), so an update that does NOT erase keeps NVS: the Hoptics screen settings (units, temperature
correction, map, boot picture) and Bluetooth pairings. A new install with "Erase" still gets a clean chip.

Only boards whose site/nodes.json "status" is "available" are published: a board still in testing gets no
files on the site, even if the release happens to contain its images.
"""
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import sys

BOARDS = {
    "heltec-v4-expansion-kit": "Heltec V4 Expansion Kit",
    "heltec-v4-r8-expansion-kit-v2": "Heltec V4-R8 Expansion Kit V2",
    # in progress (nodes.json "soon" until tested on the hardware; "-mt" = the Meshtastic build)
    "lilygo-t-deck-plus-mt": "LilyGO T-Deck Plus",
    "elecrow-thinknode-m9-mt": "Elecrow ThinkNode M9",
}

src, site, tag = sys.argv[1], sys.argv[2], sys.argv[3]

# Boards listed on the website (site/nodes.json) are built too, if the release has a factory image for them.
status = {}
try:
    for n in json.load(open(os.path.join(site, "nodes.json")))["nodes"]:
        status[n["id"]] = n.get("status", "available")
        if n.get("flash", "esp") == "esp":
            BOARDS.setdefault(n["id"], n["name"])
except (OSError, ValueError, KeyError) as e:
    print(f"nodes.json not used: {e}")


def partitions(image):
    """(name, type, subtype, offset, size) from the partition table at 0x8000 of a factory image."""
    out = []
    table = image[0x8000:0x8000 + 0xC00]
    for i in range(0, len(table), 32):
        e = table[i:i + 32]
        if e[:2] != b"\xaa\x50":
            break
        off, size = struct.unpack("<II", e[4:12])
        out.append((e[12:28].split(b"\0")[0].decode(), e[2], e[3], off, size))
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_sums(board_files):
    """Refuse to publish files that don't match the release's SHA256SUMS."""
    sums = {}
    for s in glob.glob(os.path.join(src, "SHA256SUMS*.txt")):
        for line in open(s):
            parts = line.split()
            if len(parts) == 2:
                sums[parts[1].lstrip("*")] = parts[0]
    for f in board_files:
        name = os.path.basename(f)
        if name in sums and sums[name] != sha256(f):
            sys.exit(f"checksum mismatch for {name}")


fw_root = os.path.join(site, "firmware")
os.makedirs(fw_root, exist_ok=True)
info = {"tag": tag, "boards": {}}
ota = os.path.join(src, "mt-esp32s3-ota.bin")

for board, title in BOARDS.items():
    if status.get(board, "available") != "available":
        print(f"{board} is '{status[board]}' in nodes.json, not published")
        continue
    # exact name match: a glob like "<board>-mt*" would also catch "<board>-mt-mt2.8.3" of a longer id
    pattern = re.compile(rf"hoptics-mt-{re.escape(board)}-mt[0-9.]+\.factory\.bin")
    found = sorted(f for f in glob.glob(os.path.join(src, "hoptics-mt-*.factory.bin"))
                   if pattern.fullmatch(os.path.basename(f)))
    if not found:
        print(f"no factory image for {board}, skipped")
        continue
    factory = found[0]
    update = factory.replace(".factory.bin", ".update.bin")
    check_sums([p for p in (factory, update, ota) if os.path.exists(p)])
    mt_ver = re.search(r"-mt([0-9.]+)\.factory\.bin$", factory).group(1)
    image = open(factory, "rb").read()
    table = partitions(image)
    nvs = next(p for p in table if p[0] == "nvs")
    app1 = next((p for p in table if p[1] == 0 and p[2] == 0x11), None)
    nvs_end = nvs[3] + nvs[4]
    if len(image) <= nvs_end:
        sys.exit(f"{board}: factory image ends inside NVS?")

    out = os.path.join(fw_root, board)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "boot.bin"), "wb") as f:  # bootloader + partition table
        f.write(image[:nvs[3]])
    with open(os.path.join(out, "app.bin"), "wb") as f:  # otadata (boot app0) + the firmware
        f.write(image[nvs_end:])
    parts = [{"path": "boot.bin", "offset": 0}, {"path": "app.bin", "offset": nvs_end}]
    if app1 and os.path.exists(ota):
        shutil.copy(ota, os.path.join(out, "mt-esp32s3-ota.bin"))
        parts.append({"path": "mt-esp32s3-ota.bin", "offset": app1[3]})
    # the plain files too, for people who flash by hand
    for p in (factory, update):
        if os.path.exists(p):
            shutil.copy(p, out)

    manifest = {
        "name": f"Hoptics MT for the {title}",
        "version": f"{tag} (Meshtastic {mt_ver})",
        "new_install_prompt_erase": True,  # first install: erase (fresh keys); updates: keep settings
        "new_install_improv_wait_time": 0,  # we don't speak Improv; don't make people wait
        "builds": [{"chipFamily": "ESP32-S3", "parts": parts}],
    }
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    info["boards"][board] = {
        "meshtastic": mt_ver,
        "factory": os.path.basename(factory),
        "update": os.path.basename(update) if os.path.exists(update) else None,
        "nvs_kept": f"{nvs[3]:#x}-{nvs_end:#x}",
    }
    print(f"{board}: " + ", ".join(f"{p['path']} @{p['offset']:#x}" for p in parts)
          + f" (NVS {nvs[3]:#x}-{nvs_end:#x} left alone)")

if not info["boards"]:
    sys.exit("no firmware found - nothing to publish")
with open(os.path.join(fw_root, "version.json"), "w") as f:
    json.dump(info, f, indent=2)
print(json.dumps(info, indent=2))
