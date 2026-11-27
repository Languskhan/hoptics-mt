#!/usr/bin/env python3
"""Every Meshtastic setting, as a table the Hoptics MT screen can edit (Settings > All Meshtastic settings).

Reads the firmware's nanopb headers (config.pb.h, module_config.pb.h), so the table always matches the pinned
firmware: a field Meshtastic adds shows up on the next build with no screen code to write.

    python tools/make_all_settings.py fw/src/mesh/generated/meshtastic device-ui/include/graphics/view/TFT/HopticsAllSettings.h

Left out: keys (bytes), lists, and fields the protobuf comments mark deprecated.
"""
import re
import sys

CONFIGS = [  # (section name, struct, Config / ModuleConfig union member)
    ("Device", "meshtastic_Config_DeviceConfig", "device"),
    ("Position", "meshtastic_Config_PositionConfig", "position"),
    ("Power", "meshtastic_Config_PowerConfig", "power"),
    ("Network", "meshtastic_Config_NetworkConfig", "network"),
    ("Display", "meshtastic_Config_DisplayConfig", "display"),
    ("LoRa", "meshtastic_Config_LoRaConfig", "lora"),
    ("Bluetooth", "meshtastic_Config_BluetoothConfig", "bluetooth"),
    ("Security", "meshtastic_Config_SecurityConfig", "security"),
]
MODULES = [
    ("MQTT", "meshtastic_ModuleConfig_MQTTConfig", "mqtt"),
    ("Serial", "meshtastic_ModuleConfig_SerialConfig", "serial"),
    ("External notification", "meshtastic_ModuleConfig_ExternalNotificationConfig", "external_notification"),
    ("Store & forward", "meshtastic_ModuleConfig_StoreForwardConfig", "store_forward"),
    ("Range test", "meshtastic_ModuleConfig_RangeTestConfig", "range_test"),
    ("Telemetry", "meshtastic_ModuleConfig_TelemetryConfig", "telemetry"),
    ("Canned messages", "meshtastic_ModuleConfig_CannedMessageConfig", "canned_message"),
    ("Audio", "meshtastic_ModuleConfig_AudioConfig", "audio"),
    ("Remote hardware", "meshtastic_ModuleConfig_RemoteHardwareConfig", "remote_hardware"),
    ("Neighbor info", "meshtastic_ModuleConfig_NeighborInfoConfig", "neighbor_info"),
    ("Ambient lighting", "meshtastic_ModuleConfig_AmbientLightingConfig", "ambient_lighting"),
    ("Detection sensor", "meshtastic_ModuleConfig_DetectionSensorConfig", "detection_sensor"),
    ("Paxcounter", "meshtastic_ModuleConfig_PaxcounterConfig", "paxcounter"),
    ("Status message", "meshtastic_ModuleConfig_StatusMessageConfig", "statusmessage"),
    ("Traffic management", "meshtastic_ModuleConfig_TrafficManagementConfig", "traffic_management"),
    ("TAK", "meshtastic_ModuleConfig_TAKConfig", "tak"),
    ("Mesh beacon", "meshtastic_ModuleConfig_MeshBeaconConfig", "mesh_beacon"),
]
KEEP_CAPS = {"RegionCode"}  # enum names shown as the firmware spells them (US, EU_868, ...)
WORDS = {"gps": "GPS", "gpio": "GPIO", "led": "LED", "mqtt": "MQTT", "tls": "TLS", "json": "JSON", "ntp": "NTP",
         "ipv4": "IPv4", "ipv6": "IPv6", "ip": "IP", "dns": "DNS", "psk": "password", "ssid": "network name", "wifi": "Wi-Fi", "eth": "Ethernet", "lora": "LoRa", "ble": "BLE",
         "hal": "HAL", "vibra": "vibration", "num": "number", "cw": "clockwise", "ccw": "counter-clockwise",
         "id": "ID", "rx": "RX", "tx": "TX", "pa": "PA", "lna": "LNA", "fem": "FEM", "sx126x": "SX126x", "pwm": "PWM",
         "adc": "ADC", "ina": "INA", "secs": "(s)", "ms": "(ms)", "i2s": "I2S", "sd": "SD", "ws": "WS", "din": "DIN",
         "sck": "SCK", "ptt": "PTT", "rxd": "RXD", "txd": "TXD", "tak": "TAK", "tzdef": "time zone (POSIX)",
         "oled": "OLED", "api": "API", "sds": "super deep sleep", "ls": "light sleep", "12h": "12-hour"}


def human(name):
    out = []
    for w in name.split("_"):
        out.append(WORDS.get(w.lower(), w.lower()))
    s = " ".join(out)
    return s[0].upper() + s[1:] if s else s


def enum_label(enum_type, value_name):
    v = value_name[len(enum_type) + 1:] if value_name.startswith(enum_type + "_") else value_name
    if any(enum_type.endswith(k) for k in KEEP_CAPS):
        return v
    return human(v)


def strip_comments(text):
    # keep a marker for deprecated fields: the comment just above a member says so
    return text


def parse(headers):
    src = "\n".join(open(h).read() for h in headers)
    enums = {}
    for m in re.finditer(r"typedef enum _(\w+) \{(.*?)\} \1;", src, re.S):
        body = re.sub(r"/\*.*?\*/", "", m.group(2), flags=re.S)
        vals = []
        for v in re.finditer(r"(\w+)\s*=\s*(-?\d+)", body):
            vals.append((v.group(1), int(v.group(2))))
        enums[m.group(1)] = vals
    structs = {}
    for m in re.finditer(r"typedef struct _(\w+) \{(.*?)\} \1;", src, re.S):
        members = []
        deprecated = False
        body = m.group(2)
        pos = 0
        for tok in re.finditer(r"/\*.*?\*/|[^;/]+;", body, re.S):
            t = tok.group(0)
            if t.startswith("/*"):
                deprecated = bool(re.search(r"\b[Dd]eprecated\b|\bDEPRECATED\b|\bMoved to\b", t))
                continue
            decl = " ".join(t.strip().rstrip(";").split())
            mm = re.match(r"(?:struct _)?([\w ]+?)\s+(\w+)(?:\[(\w+)\])?$", decl)
            if mm:
                members.append((mm.group(1).strip(), mm.group(2), mm.group(3), deprecated))
            deprecated = False
        structs[m.group(1)] = members
    return enums, structs


def fields_of(struct, structs, enums, prefix="", cexpr=""):
    """(label, C member path, kind, enum type or None) for every editable member, nested messages flattened."""
    out = []
    members = structs.get(struct, [])
    names = {n for _, n, _, _ in members}
    for ctype, name, arr, dep in members:
        if dep or name.startswith("has_") or name.endswith("_count") or name == "which_payload_variant":
            continue
        if (name + "_count") in names:
            continue  # a list
        label = (prefix + " " + human(name)).strip()
        path = cexpr + name
        if ctype == "char" and arr:
            kind = "SECRET" if re.search(r"psk|password", name) else "STR"
            out.append((label, path, kind, None))
        elif ctype in ("bool",):
            out.append((label, path, "BOOL", None))
        elif ctype in ("float", "double"):
            out.append((label, path, "FLOAT", None))
        elif ctype == "uint32_t" and cexpr.endswith("ipv4_config."):
            out.append((label, path, "IP", None))  # an IPv4 address in one number: shown as a.b.c.d
        elif re.match(r"u?int(8|16|32|64)_t$", ctype) and not arr:
            out.append((label, path, "NUM", None))
        elif ctype in enums and not arr:
            out.append((label, path, "ENUM", ctype))
        elif ctype in structs and not arr:
            out += fields_of(ctype, structs, enums, human(name), path + ".")
        # anything else (bytes, unions, arrays): not editable here
    return out


def main(gen_dir, out_path):
    enums, structs = parse([f"{gen_dir}/{h}" for h in ("config.pb.h", "module_config.pb.h", "atak.pb.h")])
    lines = [
        "// Generated by tools/make_all_settings.py from the pinned Meshtastic firmware's protobufs. Do not edit.",
        "#pragma once",
        "#include \"meshtastic/config.pb.h\"",
        "#include \"meshtastic/module_config.pb.h\"",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace hxall",
        "{",
        "enum Kind : uint8_t { BOOL, NUM, FLOAT, STR, SECRET, ENUM, IP };",
        "struct Section {",
        "    const char *name;",
        "    bool module;  // ModuleConfig (else Config)",
        "    uint8_t tag;  // which_payload_variant",
        "};",
        "struct Field {",
        "    uint8_t section;",
        "    const char *label;",
        "    uint16_t offset; // from the start of the section's struct (the union member)",
        "    uint8_t kind, size;",
        "    bool isSigned;",
        "    const char *opts;    // ENUM: option names, one per line",
        "    const int32_t *vals; // ENUM: their values",
        "    uint8_t nvals;",
        "};",
        "",
    ]
    secs, flds, vtabs = [], [], []
    for module, table in ((False, CONFIGS), (True, MODULES)):
        for name, struct, member in table:
            if struct not in structs:
                print(f"note: {struct} not in this firmware, skipped", file=sys.stderr)
                continue
            tag = f"meshtastic_{'ModuleConfig' if module else 'Config'}_{member}_tag"
            si = len(secs)
            secs.append(f'    {{"{name}", {"true" if module else "false"}, {tag}}},')
            for label, path, kind, etype in fields_of(struct, structs, enums):
                off = f"offsetof({struct}, {path})"
                size = f"sizeof((({struct} *)0)->{path})"
                signed = "false"
                opts, vals, nv = "nullptr", "nullptr", "0"
                if kind == "NUM":
                    signed = f"(({struct} *)0)->{path} - 1 < 0"
                    signed = f"std::is_signed<decltype((({struct} *)0)->{path})>::value"
                if kind == "ENUM":
                    ev = [(n, v) for n, v in enums[etype] if not n.endswith("_MIN") and not n.endswith("_MAX")]
                    vi = len(vtabs)
                    vtabs.append(f"static const int32_t V{vi}[] = {{{', '.join(str(v) for _, v in ev)}}};")
                    opts = '"' + "\\n".join(enum_label(etype, n) for n, _ in ev) + '"'
                    vals, nv, signed = f"V{vi}", str(len(ev)), "true"
                flds.append(f'    {{{si}, "{label}", (uint16_t){off}, {kind}, (uint8_t){size}, {signed}, {opts}, {vals}, {nv}}},')
    lines.insert(6, "#include <type_traits>")
    lines += vtabs
    lines += ["", "static const Section SECTIONS[] = {"] + secs + ["};"]
    lines += ["static const Field FIELDS[] = {"] + flds + ["};"]
    lines += ["constexpr int NUM_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);",
              "constexpr int NUM_FIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);", "} // namespace hxall", ""]
    open(out_path, "w").write("\n".join(lines))
    print(f"{len(secs)} sections, {len(flds)} settings -> {out_path}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
