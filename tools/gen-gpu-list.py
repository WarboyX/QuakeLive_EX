#!/usr/bin/env python3
"""
[QL] E129. The GPU list behind the hardware prompt, from the PCI ID database.

WHY A LIST OF IDS. E127 classified the GPU from the name the OpenGL driver
reports. On Windows, AMD's driver reports most integrated GPUs to OpenGL as the
same generic "AMD Radeon(TM) Graphics" - a Vega 8 without ray tracing, a 780M
with it and a two-core 610M all read identically - so no name rule can tell
them apart. Their PCI device IDs can: the 780M is 1002:15BF whatever the driver
calls it. The client reads the IDs without touching Vulkan or even OpenGL
(cl_gpuid.c: sysfs on Linux, DXGI on Windows) and looks them up here.

WHERE THE LIST COMES FROM. pci.ids - the database lspci uses, maintained at
github.com/pciutils/pciids - names every device by vendor, device ID and chip
codename ("Phoenix1", "GA106M [GeForce RTX 3060 Mobile / Max-Q]"). The
classification below goes by CHIP GENERATION, not marketing name: whether a
GPU has hardware ray tracing is a property of its architecture.

TIERS
  rt      offer Vulkan; advanced rendering includes ray-traced AO
  vulkan  offer Vulkan; advanced rendering is water only
  none    not offered - listed so a name rule never second-guesses it

Regenerate after new GPUs ship (pci.ids updates weekly):
    curl -sSfLo /tmp/pci.ids https://raw.githubusercontent.com/pciutils/pciids/master/pci.ids
    python3 tools/gen-gpu-list.py /tmp/pci.ids

Writes code/client/cl_gpulist.h (compiled in) and docs/gpu-list.txt (the list
to review). A GPU newer than the list falls back to the name rules in
CL_GuessGPU, so a stale list costs a missed prompt, never a wrong one.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "code/client/cl_gpulist.h"
DOC = ROOT / "docs/gpu-list.txt"

NOT_A_GPU = re.compile(r"audio|usb|ucsi|serial|bridge|nvlink|nvswitch|smbus|ethernet|"
                       r"i2c|sata|ide |controller|psp|crypto|sensor|pcie dummy|root port|"
                       r"host|memory|dma|iommu|lpc|thermal|spi|uart|gpio|ahci|nvme|"
                       r"wireless|wi-fi|bluetooth|network|management|virtual|hdcp|"
                       r"acp|isp|npu|ipu|ai engine|pmc|p2sb|ish|shared sram|trace hub|"
                       r"express switch|downstream port|upstream port",
                       re.I)


def split(name):
    """'GA106M [GeForce RTX 3060 Mobile]' -> ('GA106M', 'GeForce RTX 3060 Mobile')"""
    i = name.find("[")
    if i < 0:
        return name.strip(), ""
    j = name.rfind("]")
    return name[:i].strip(), name[i + 1:j if j > i else len(name)].strip()


# ---------------------------------------------------------------- NVIDIA ----
# Turing TU102/104/106 and everything after (Ampere, Ada, Blackwell) has RT
# cores. TU116/TU117 are Turing WITHOUT them - the GTX 16 series.
NV_RT = re.compile(r"^(TU10[246]|GA10[2-7]|AD10[2-7]|GB20[2-7])")
NV_VK = re.compile(r"^(TU11[67]|GP10[2-7]|GM20[046]|GV100)")
# No display output at all, or not a GeForce/Quadro-class part.
NV_DATACENTER = re.compile(r"^(GA100|GH1|GB1|GB2[01]\d|GP100|GK2|GK110B?GL)")
# Discrete, but the entry line: not what the prompt is asking about.
NV_ENTRY = re.compile(r"\bMX\s?\d|\bGT \d|\bGT$|NVS|Tegra", re.I)


def nvidia(chip, bracket):
    if NV_DATACENTER.match(chip) and "TITAN" not in bracket.upper():
        return "none", "data-centre part"
    if NV_ENTRY.search(bracket):
        return "none", "entry line (GT / MX / NVS)"
    if NV_RT.match(chip):
        return "rt", "Turing RTX / Ampere / Ada / Blackwell"
    if NV_VK.match(chip):
        return "vulkan", "GTX 16 / Pascal / Maxwell 2 / Volta"
    return "none", "older than Maxwell 2 or unrecognised chip"


# ---------------------------------------------------------------- AMD -------
# RDNA2 and later have ray accelerators: Navi 2x/3x/4x, and the RDNA2/3/3.5
# integrated parts (Van Gogh is the Steam Deck). Mendocino/Raphael/Granite
# Ridge are RDNA2 with two compute units - technically RT, practically a
# display adapter - so they are not asked.
AMD_RT = re.compile(r"^(Navi 2[1-4]|Navi 3[1-3]|Navi 4\d|Rembrandt|Phoenix|HawkPoint|"
                    r"Hawk Point|Strix|Krackan|Van Gogh|Sarlak)", re.I)
AMD_VK = re.compile(r"^(Navi 1[0-4]|Vega 1[02]|Vega 20|Ellesmere|Polaris ?(10|11|20|21|30)|"
                    r"Baffin|Fiji|Hawaii|Grenada|Tonga|Antigua)", re.I)
AMD_WEAK_IGPU = re.compile(r"^(Mendocino|Raphael|Granite Ridge|Raven|Picasso|Renoir|Cezanne|"
                           r"Lucienne|Barcelo|Dali|Pollock|Stoney|Carrizo|Bristol|Kaveri|"
                           r"Kabini|Mullins|Beema)", re.I)


def amd(chip, bracket):
    if AMD_WEAK_IGPU.match(chip):
        return "none", "integrated: Vega-based, or RDNA2 with 2 CUs"
    if AMD_RT.match(chip):
        return "rt", "RDNA2 / RDNA3 / RDNA4 (Navi 2x-4x, 680M/780M/880M/890M/8060S)"
    if AMD_VK.match(chip):
        return "vulkan", "RDNA1 / Vega / Polaris / GCN3"
    if re.search(r"Radeon|FirePro|Instinct", bracket + chip, re.I):
        return "none", "GCN1-2, Lexa/Polaris 12, older, or unrecognised chip"
    return None, None           # not a GPU entry


# ---------------------------------------------------------------- Intel -----
# Alchemist (Arc A, DG2), Battlemage (Arc B) and the Arc-branded integrated
# graphics of Meteor / Arrow / Lunar Lake all have ray tracing units. Only Arc
# is listed: everything else Intel falls to the name rules, which say no.
def intel(chip, bracket):
    if re.search(r"\bArc\b", bracket):
        return "rt", "Arc (Alchemist, Battlemage, Arc integrated)"
    return None, None


VENDORS = {
    "10de": ("NVIDIA", nvidia),
    "1002": ("AMD", amd),
    "8086": ("Intel", intel),
}


def parse(path):
    out = []
    vendor = None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("#") or not line.strip():
            continue
        if not line.startswith("\t"):
            vendor = line[:4].lower() if line[:4].lower() in VENDORS else None
            continue
        if vendor is None or line.startswith("\t\t"):
            continue
        m = re.match(r"\t([0-9a-f]{4})\s+(.*)$", line.rstrip("\n"))
        if not m:
            continue
        dev, name = m.group(1), m.group(2).strip()
        if NOT_A_GPU.search(name):
            continue
        chip, bracket = split(name)
        tier, why = VENDORS[vendor][1](chip, bracket)
        if tier is None:
            continue
        out.append((vendor, dev, tier, why, name))
    return out


# pci.ids names some AMD integrated GPUs by codename only ("Phoenix1" is the
# 780M's entry). The dialog shows this label when the driver's own OpenGL name
# is the generic "AMD Radeon(TM) Graphics", so give those a readable one: the
# Radeon models each chip ships as, with the codename kept for reference.
CODENAME_LABEL = [
    (r"^Phoenix",   "Radeon 740M / 760M / 780M (Phoenix)"),
    (r"^HawkPoint", "Radeon 740M / 760M / 780M (Hawk Point)"),
    (r"^Rembrandt", "Radeon 660M / 680M (Rembrandt)"),
    (r"^Krackan",   "Radeon 800M series (Krackan)"),
    (r"^Strix",     "Radeon 800M series (Strix)"),
]


def display_name(name):
    chip, bracket = split(name)
    if bracket:
        return bracket
    for pat, label in CODENAME_LABEL:
        if re.match(pat, chip):
            return label
    return chip


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    version = "unknown"
    for line in open(src, encoding="utf-8", errors="replace"):
        if line.startswith("#\tVersion:") or line.startswith("# Version:"):
            version = line.split(":", 1)[1].strip()
            break
    rows = parse(src)

    tiers = {"rt": 2, "vulkan": 1, "none": 0}
    h = ["/*",
         " * GENERATED by tools/gen-gpu-list.py from pci.ids %s - do not edit." % version,
         " * docs/gpu-list.txt is the same list, readable. [QL] E129.",
         " */",
         "#ifndef CL_GPULIST_H",
         "#define CL_GPULIST_H",
         "",
         "#define GPU_TIER_NONE   0",
         "#define GPU_TIER_VULKAN 1",
         "#define GPU_TIER_RT     2",
         "",
         "typedef struct {",
         "    unsigned short vendor, device;",
         "    unsigned char tier;",
         "    const char* name;",
         "} gpuListEntry_t;",
         "",
         "static const gpuListEntry_t gpuList[] = {"]
    for vendor, dev, tier, why, name in sorted(rows):
        h.append('    { 0x%s, 0x%s, %d, "%s" },' % (vendor, dev, tiers[tier],
                                                      display_name(name).replace('"', "'")))
    h += ["};", "", "#endif", ""]
    HEADER.write_text("\n".join(h))

    d = ["# GPUs the hardware prompt knows, by PCI vendor:device ID.",
         "# GENERATED by tools/gen-gpu-list.py from pci.ids %s - do not edit." % version,
         "#",
         "#   rt      offered Vulkan; advanced rendering includes ray-traced AO",
         "#   vulkan  offered Vulkan; advanced rendering is water only",
         "#   none    not offered",
         "#",
         "# A GPU missing from this list falls back to CL_GuessGPU's name rules.",
         ""]
    for tier in ("rt", "vulkan", "none"):
        for vname in ("NVIDIA", "AMD", "Intel"):
            vid = [k for k, v in VENDORS.items() if v[0] == vname][0]
            sel = [r for r in rows if r[0] == vid and r[2] == tier]
            if not sel:
                continue
            head = sel[0][3] if tier != "none" else "reason per row"
            d.append("== %s  %s  (%d)  - %s" % (tier.upper(), vname, len(sel), head))
            for vendor, dev, _, why, name in sorted(sel, key=lambda r: r[4]):
                if tier == "none":
                    d.append("  %s:%s  %-60s  (%s)" % (vendor, dev, name, why))
                else:
                    d.append("  %s:%s  %s" % (vendor, dev, name))
            d.append("")
    DOC.write_text("\n".join(d))

    counts = {t: sum(1 for r in rows if r[2] == t) for t in tiers}
    print("gen-gpu-list: pci.ids %s -> %d rt, %d vulkan, %d none"
          % (version, counts["rt"], counts["vulkan"], counts["none"]))


if __name__ == "__main__":
    main()
