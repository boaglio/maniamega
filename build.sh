#!/bin/bash
# ManiaMega build script.
# Builds the project (in this directory) against the MSXgl engine in ./MSXgl.
# MSXgl bundles its own SDCC + Node, so no system toolchain is required.
#─────────────────────────────────────────────────────────────────────────────
cd "$(dirname "$0")"

if [ ! -f MSXgl/engine/script/js/build.js ]; then
	echo "!! MSXgl not found. Fetch the submodule first:"
	echo "     git submodule update --init --depth 1"
	exit 1
fi

# Prefer a system Node; fall back to the one bundled with MSXgl.
if type -P node >/dev/null 2>&1; then
	node MSXgl/engine/script/js/build.js "$@"
else
	MSXgl/tools/build/Node/node MSXgl/engine/script/js/build.js "$@"
fi
