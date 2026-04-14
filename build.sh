#!/usr/bin/env bash
# FluTube — Build + Install script
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

AU_INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/VST3"

PLUGIN_NAME="FluTube"

echo ""
echo -e "${CYAN}╔════════════════════════════════╗${NC}"
echo -e "${CYAN}║    FluTube — Build & Install    ║${NC}"
echo -e "${CYAN}╚════════════════════════════════╝${NC}"
echo ""

# Check yt-dlp binary exists in Resources/
if [ ! -f "$SCRIPT_DIR/Resources/yt-dlp" ]; then
    echo -e "${YELLOW}⚠  Resources/yt-dlp not found.${NC}"
    echo "   Download from: https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos"
    echo "   Save as: $SCRIPT_DIR/Resources/yt-dlp"
    echo "   Then:    chmod +x Resources/yt-dlp"
    echo ""
    echo -e "${RED}Build aborted — yt-dlp binary required.${NC}"
    exit 1
fi
chmod +x "$SCRIPT_DIR/Resources/yt-dlp"
echo -e "${GREEN}✓ yt-dlp binary found${NC}"

# Check ffmpeg/ffprobe binaries exist in Resources/ (needed by yt-dlp for audio postprocessing)
if [ ! -f "$SCRIPT_DIR/Resources/ffmpeg" ] || [ ! -f "$SCRIPT_DIR/Resources/ffprobe" ]; then
    echo -e "${YELLOW}⚠  Resources/ffmpeg or ffprobe not found — copying from Homebrew...${NC}"
    FF=$(command -v ffmpeg  2>/dev/null || true)
    FP=$(command -v ffprobe 2>/dev/null || true)
    if [ -z "$FF" ] || [ -z "$FP" ]; then
        echo -e "${RED}Error: ffmpeg not found. Install via Homebrew: brew install ffmpeg${NC}"
        exit 1
    fi
    # Resolve symlinks to get the real binary
    FF=$(python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$FF")
    FP=$(python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$FP")
    cp "$FF" "$SCRIPT_DIR/Resources/ffmpeg"
    cp "$FP" "$SCRIPT_DIR/Resources/ffprobe"
    echo -e "${GREEN}✓ ffmpeg/ffprobe copied from Homebrew${NC}"
fi
chmod +x "$SCRIPT_DIR/Resources/ffmpeg" "$SCRIPT_DIR/Resources/ffprobe"
echo -e "${GREEN}✓ ffmpeg/ffprobe found${NC}"

# Check dependencies
if ! xcode-select -p &>/dev/null; then
    echo -e "${RED}Error: Xcode Command Line Tools not found. Run: xcode-select --install${NC}"
    exit 1
fi
if ! command -v cmake &>/dev/null; then
    echo -e "${RED}Error: CMake not found. Install via Homebrew: brew install cmake${NC}"
    exit 1
fi

cd "$SCRIPT_DIR"

# Step 1: Configure
echo -e "${BOLD}Step 1/4: Configuring...${NC}"
echo "(First run downloads JUCE ~200MB and RubberBand ~5MB)"
echo ""
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    2>&1 | grep -v "^--" | grep -v "^$" || true
echo -e "${GREEN}✓ Configuration complete${NC}"
echo ""

# Step 2: Build
echo -e "${BOLD}Step 2/4: Building...${NC}"
NPROC=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" --config Release -j"$NPROC"
echo -e "${GREEN}✓ Build complete${NC}"
echo ""

# Step 3: Ensure yt-dlp is executable inside bundles
AU_BUILD="$BUILD_DIR/FluTube_artefacts/Release/AU/${PLUGIN_NAME}.component"
VST3_BUILD="$BUILD_DIR/FluTube_artefacts/Release/VST3/${PLUGIN_NAME}.vst3"
ENTITLEMENTS="$SCRIPT_DIR/FluTube.entitlements"

for bundle in "$AU_BUILD" "$VST3_BUILD"; do
    ytdlp_in_bundle="$bundle/Contents/Resources/yt-dlp"
    if [ -f "$ytdlp_in_bundle" ]; then
        chmod +x "$ytdlp_in_bundle"
        echo -e "${GREEN}✓ yt-dlp execute permission set in $(basename $bundle)${NC}"
    fi
done

# Step 4: Codesign
echo -e "${BOLD}Step 3/4: Code signing...${NC}"

sign_plugin() {
    local path="$1"
    local label="$2"
    if [ -d "$path" ]; then
        if codesign --force --deep -s - \
            --entitlements "$ENTITLEMENTS" "$path" 2>/dev/null; then
            echo -e "${GREEN}✓ $label signed (with network entitlement)${NC}"
        else
            codesign --force --deep -s - "$path" 2>/dev/null && \
                echo -e "${YELLOW}⚠ $label signed (no entitlement)${NC}" || \
                echo -e "${YELLOW}⚠ $label signing skipped${NC}"
        fi
    fi
}

sign_plugin "$AU_BUILD"   "AU"
sign_plugin "$VST3_BUILD" "VST3"
echo ""

# Step 5: Install
echo -e "${BOLD}Step 4/4: Installing...${NC}"
mkdir -p "$AU_INSTALL_DIR" "$VST3_INSTALL_DIR"

install_plugin() {
    local src="$1"
    local dst_dir="$2"
    local name="$3"
    if [ -d "$dst_dir/$name" ]; then
        echo -e "${GREEN}✓ $name already installed by build system${NC}"
    elif [ -d "$src" ]; then
        cp -R "$src" "$dst_dir/"
        echo -e "${GREEN}✓ $name installed to $dst_dir${NC}"
    else
        echo -e "${RED}✗ Build artifact not found: $src${NC}"
    fi
}

install_plugin "$AU_BUILD"   "$AU_INSTALL_DIR"   "${PLUGIN_NAME}.component"
install_plugin "$VST3_BUILD" "$VST3_INSTALL_DIR" "${PLUGIN_NAME}.vst3"

echo ""
echo -e "${CYAN}════════════════════════════════${NC}"
echo -e "${BOLD}Installation complete!${NC}"
echo -e "${CYAN}════════════════════════════════${NC}"
echo ""
echo "  Logic Pro:   Settings → Plug-in Manager → Rescan All"
echo "  Ableton:     Preferences → Plug-Ins → VST3 → Rescan"
echo ""
echo "  Paste a YouTube URL and hit LOAD. MIDI note C4 = root pitch."
echo ""
