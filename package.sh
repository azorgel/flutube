#!/usr/bin/env bash
# Creates a distributable .pkg installer for FluTube.
# Run this after build.sh has succeeded.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_NAME="FluTube"
VERSION="1.0"
IDENTIFIER="com.voltalabs.flutube"
BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build/FluTube_artefacts/Release"
DIST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dist"
PKG_ROOT="$DIST_DIR/pkg_root"
OUTPUT="$DIST_DIR/${PLUGIN_NAME} Installer.pkg"

echo ""
echo "  FluTube — Package Builder"
echo ""

# ── Verify build artifacts exist ──────────────────────────────
AU_SRC="$BUILD_DIR/AU/${PLUGIN_NAME}.component"
VST3_SRC="$BUILD_DIR/VST3/${PLUGIN_NAME}.vst3"

if [ ! -d "$AU_SRC" ] || [ ! -d "$VST3_SRC" ]; then
    echo "ERROR: Build artifacts not found. Run bash build.sh first."
    exit 1
fi

# ── Build payload directory (mirrors install paths) ───────────
rm -rf "$DIST_DIR"
mkdir -p "$PKG_ROOT/Library/Audio/Plug-Ins/Components"
mkdir -p "$PKG_ROOT/Library/Audio/Plug-Ins/VST3"

cp -R "$AU_SRC"   "$PKG_ROOT/Library/Audio/Plug-Ins/Components/"
cp -R "$VST3_SRC" "$PKG_ROOT/Library/Audio/Plug-Ins/VST3/"

# Sync Resources/ into both bundles (cmake POST_BUILD only runs on rebuild,
# so do it explicitly here to guarantee ffmpeg/ffprobe are always included)
RESOURCES_SRC="$SCRIPT_DIR/Resources"
for bundle in \
    "$PKG_ROOT/Library/Audio/Plug-Ins/Components/${PLUGIN_NAME}.component/Contents/Resources" \
    "$PKG_ROOT/Library/Audio/Plug-Ins/VST3/${PLUGIN_NAME}.vst3/Contents/Resources"; do
    cp -R "$RESOURCES_SRC/." "$bundle/"
    chmod +x "$bundle/yt-dlp" "$bundle/ffmpeg" "$bundle/ffprobe" 2>/dev/null || true
done

# Strip quarantine from everything inside the payload
xattr -cr "$PKG_ROOT" 2>/dev/null || true

# ── Build the .pkg ────────────────────────────────────────────
pkgbuild \
    --root           "$PKG_ROOT" \
    --identifier     "$IDENTIFIER" \
    --version        "$VERSION" \
    --install-location "/" \
    "$OUTPUT"

echo ""
echo "  Done!  Send this file to your friend:"
echo "  $OUTPUT"
echo ""
echo "  Their install steps:"
echo "  1. Double-click '${PLUGIN_NAME} Installer.pkg'"
echo "  2. If blocked: System Settings → Privacy & Security → Open Anyway"
echo "  3. Click through installer (needs Mac password)"
echo "  4. Open Logic → Settings → Plug-in Manager → Rescan All"
echo "  5. New track → AU Instruments → Volta Labs → FluTube"
echo ""
