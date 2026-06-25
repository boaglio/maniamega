#!/bin/bash
# ____________________________
# ManiaMega — build & launch helper
#─────────────────────────────────────────────────────────────────────────────
# Usage:
#   ./run.sh                 Build, then launch in openMSX (MSX2)
#   ./run.sh --no-build      Launch the existing ROM without rebuilding
#   ./run.sh --machine NAME  Use a specific openMSX machine config
#
# Env overrides:
#   EMULATOR=/path/to/openmsx   MACHINE=Boosted_MSX2_EN
#─────────────────────────────────────────────────────────────────────────────
set -e
cd "$(dirname "$0")"

EMULATOR="${EMULATOR:-openmsx}"
MACHINE="${MACHINE:-C-BIOS_MSX2}"   # free BIOS bundled with openMSX; no system ROMs needed
ROM="emul/rom/maniamega.rom"
DO_BUILD=1

while [ $# -gt 0 ]; do
	case "$1" in
		--no-build) DO_BUILD=0 ;;
		--machine)  MACHINE="$2"; shift ;;
		*) echo "Unknown option: $1"; exit 1 ;;
	esac
	shift
done

if [ "$DO_BUILD" = "1" ]; then
	echo ">> Building maniamega..."
	./build.sh
fi

if [ ! -f "$ROM" ]; then
	echo "!! ROM not found: $ROM  (run without --no-build first)"
	exit 1
fi

if ! command -v "$EMULATOR" >/dev/null 2>&1; then
	echo "!! Emulator '$EMULATOR' not found on PATH."
	echo "   Install openMSX, or set EMULATOR=/path/to/openmsx"
	exit 1
fi

echo ">> Launching $ROM on machine $MACHINE ..."
exec "$EMULATOR" -machine "$MACHINE" -cart "$ROM"
