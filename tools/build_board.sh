#!/usr/bin/env bash
# Rebuild hardware/usbpd.kicad_pcb from the schematic + tools/place.json (+ tools/route_plan.py when present).
# Stops at the first failed stage (R28).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HW="$ROOT/hardware"
K="$(python3 -c 'import sys; sys.path.insert(0, "'"$ROOT"'/tools"); import kskills; print(kskills.SCRIPTS)' 2>/dev/null)"
KC="${KICAD_CLI:-/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli}"
S="${SCRATCH:-$ROOT/build}"
mkdir -p "$S"
trap 'python3 "$K/kicad_locks.py" status "$HW" --clean >/dev/null || true' EXIT

rm -f "$S/net.xml"
"$KC" sch export netlist --format kicadxml -o "$S/net.xml" "$HW/usbpd.kicad_sch" >/dev/null
test -s "$S/net.xml"

python3 "$K/new_board.py" "$S/net.xml" "$HW/usbpd.kicad_pcb" --layers 2 --outline 67x17 --origin 100,100 \
    --corner-r 1 --placement "$ROOT/tools/place.json" --labels "$ROOT/tools/labels.json" --refs fab
python3 "$K/board_setup.py" "$HW/usbpd.kicad_pro" --stackup JLC-2L-1.6 \
    --class Power:track=1.0 --assign Power=VBUS,/VOUT,GND

if [ -f "$ROOT/tools/route_plan.py" ]; then
    rm -f "$S/router.log"
    "$K/kicad_env.sh" >/dev/null 2>&1 || true
    PY="${PY:-$HOME/.cache/kicad-skills/venv/bin/python}"
    KP="${KICAD_PYTHON:-/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3}"
    BR="$(dirname "$K")/../kicad-board-review/scripts"
    rm -rf "$S/pack0"; mkdir -p "$S/pack0"
    "$KP" "$BR/boarddump.py" --no-connectivity "$HW/usbpd.kicad_pcb" "$S/pack0" >/dev/null
    test -s "$S/pack0/board.json"
    rc=0
    "$PY" "$K/router.py" apply "$HW/usbpd.kicad_pcb" "$S/pack0/board.json" "$ROOT/tools/route_plan.py" \
        > "$S/router.log" 2>&1 || rc=$?
    grep -q '^wrote ' "$S/router.log" || { cat "$S/router.log"; echo "router did not write the board"; exit 1; }
    if grep -E '^ +(VIOLATION|REJECTED)' "$S/router.log"; then echo "router rejected items: fix the plan"; exit 1; fi
    [ "$rc" = 0 ] || { cat "$S/router.log"; exit 1; }
fi
echo "board built"
