#!/usr/bin/env python3
"""Write an ESP Web Tools manifest for a board folder (used by the one-click installer page)."""
import glob, json, os, sys

out, board, ver, ota = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
factory = os.path.basename(glob.glob(os.path.join(out, "*.factory.bin"))[0])
parts = [{"path": factory, "offset": 0}]
if os.path.exists(os.path.join(out, "mt-esp32s3-ota.bin")) and ota:
    parts.append({"path": "mt-esp32s3-ota.bin", "offset": int(ota, 16)})
manifest = {
    "name": f"Hoptics MT ({board})",
    "version": f"Meshtastic {ver}",
    "new_install_prompt_erase": True,   # first install: erase (fresh keys); updates: keep settings
    "new_install_improv_wait_time": 0,  # we don't speak Improv; don't make people wait
    "builds": [{"chipFamily": "ESP32-S3", "parts": parts}],
}
with open(os.path.join(out, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2)
print(json.dumps(manifest, indent=2))
