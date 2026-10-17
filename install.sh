#!/bin/bash
# rainworld-arm installer — Rain World (Windows build) on ARM64 Linux via box64 + Wine.
# No root needed. Everything goes to one folder (default ~/Games/RainWorld-ARM).
#
#   bash install.sh                      # downloads the game from YOUR Steam account
#   bash install.sh --from-dir DIR       # use an existing copy of the Windows game folder
#   bash install.sh --dest DIR           # install somewhere else
#
# You must own Rain World on Steam. This script never ships or shares game files.
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/Games/RainWorld-ARM"
FROM_DIR=""
JOBS=2   # box64 build: keep low on boards with <=4 GB RAM

while [ $# -gt 0 ]; do
    case "$1" in
        --dest) DEST="$2"; shift 2 ;;
        --from-dir) FROM_DIR="$2"; shift 2 ;;
        -h|--help) sed -n 2,9p "$0"; exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

WINE_URL=https://github.com/Kron4ek/Wine-Builds/releases/download/10.0/wine-10.0-amd64-wow64.tar.xz
WINE_SHA=aeebbbf239e548f0136f1cd72a2e109dd9a572a8f703da3c09fa40d74d5c255f
DD_URL=https://github.com/SteamRE/DepotDownloader/releases/download/DepotDownloader_3.4.0/DepotDownloader-linux-arm64.zip
DD_SHA=d9fb612ccebc1db8eeea3b4045d2221ec70431381393ce908fb72f01d4f9c812
BOX64_TAG=v0.4.4
STEAM_APP=312520

say() { printf '\n== %s\n' "$*"; }
warn() { printf '!! %s\n' "$*"; }
need() { command -v "$1" >/dev/null || { echo "Missing required command: $1"; exit 1; }; }

fetch() {  # url sha256 outfile
    curl -fsSL --retry 3 -o "$3" "$1"
    echo "$2  $3" | sha256sum -c - >/dev/null || { echo "Checksum mismatch for $1"; exit 1; }
}

say "Checks"
[ "$(uname -m)" = aarch64 ] || { echo "Needs aarch64 (ARM64), this is $(uname -m)"; exit 1; }
[ "$(getconf PAGESIZE)" = 4096 ] || warn "Page size is $(getconf PAGESIZE): box64/Wine need 4K pages (Raspberry Pi 5: use kernel8.img)."
for c in curl tar unzip sha256sum xrandr systemd-run pgrep; do need "$c"; done
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary 2>/dev/null | grep -E "deviceName|driverName" | head -2 || true
else
    warn "vulkaninfo not found (package vulkan-tools); can't check the GPU."
fi
mkdir -p "$DEST"
TMP="$DEST/.download"; mkdir -p "$TMP"

# ---------------------------------------------------------------- box64
say "box64"
if command -v box64 >/dev/null; then
    echo "Using system box64: $(box64 --version 2>&1 | grep -m1 -o "v[0-9.]*" || true)"
    BOX64_CMD=box64
elif [ -x "$DEST/box64/run-box64" ]; then
    echo "Already built: $DEST/box64"
    BOX64_CMD="$DEST/box64/run-box64"
else
    for c in git cmake gcc make; do need "$c"; done
    echo "No box64 found, building $BOX64_TAG from source (-j$JOBS, takes a while)..."
    rm -rf "$TMP/box64-src"
    git clone --depth 1 --branch "$BOX64_TAG" https://github.com/ptitSeb/box64.git "$TMP/box64-src"
    cmake -S "$TMP/box64-src" -B "$TMP/box64-src/build" -DARM64=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
    make -C "$TMP/box64-src/build" -j"$JOBS" box64
    mkdir -p "$DEST/box64/bin" "$DEST/box64/lib"
    cp "$TMP/box64-src/build/box64" "$TMP/box64-src/tests/box64-bash" "$DEST/box64/bin/"
    cp -r "$TMP/box64-src/x64lib" "$DEST/box64/lib/x86_64"
    cp "$TMP/box64-src/system/box64.box64rc" "$DEST/box64/"
    cat > "$DEST/box64/run-box64" <<'EOF'
#!/bin/bash
# box64 from this folder (not installed system-wide)
D="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export BOX64_LD_LIBRARY_PATH="$D/lib/x86_64${BOX64_LD_LIBRARY_PATH:+:$BOX64_LD_LIBRARY_PATH}"
export BOX64_RCFILE="${BOX64_RCFILE:-$D/box64.box64rc}"
export BOX64_BASH="$D/bin/box64-bash"
exec "$D/bin/box64" "$@"
EOF
    chmod +x "$DEST/box64/run-box64"
    rm -rf "$TMP/box64-src"
    BOX64_CMD="$DEST/box64/run-box64"
fi

# ---------------------------------------------------------------- Wine
say "Wine 10.0 (x86_64 WoW64, Kron4ek build)"
if [ -x "$DEST/wine10/bin/wine" ]; then
    echo "Already there."
else
    # Wine 11.x crashes under box64 0.4.4 (anon_mmap_alloc assert) — stay on 10.0
    fetch "$WINE_URL" "$WINE_SHA" "$TMP/wine.tar.xz"
    tar -xJf "$TMP/wine.tar.xz" -C "$TMP"
    rm -rf "$DEST/wine10"; mv "$TMP/wine-10.0-amd64-wow64" "$DEST/wine10"
    rm -f "$TMP/wine.tar.xz"
fi
"$BOX64_CMD" "$DEST/wine10/bin/wine" --version

# ---------------------------------------------------------------- Game
say "Game files"
GAME="$DEST/game"
if [ -f "$GAME/RainWorld.exe" ]; then
    echo "Already there: $GAME"
elif [ -n "$FROM_DIR" ]; then
    [ -f "$FROM_DIR/RainWorld.exe" ] || { echo "$FROM_DIR has no RainWorld.exe"; exit 1; }
    echo "Copying from $FROM_DIR ..."
    cp -a "$FROM_DIR" "$GAME"
else
    if [ ! -x "$DEST/depotdownloader/DepotDownloader" ]; then
        fetch "$DD_URL" "$DD_SHA" "$TMP/dd.zip"
        mkdir -p "$DEST/depotdownloader"; unzip -oq "$TMP/dd.zip" -d "$DEST/depotdownloader"; rm -f "$TMP/dd.zip"
        chmod +x "$DEST/depotdownloader/DepotDownloader"
    fi
    echo "The game (~4.3 GB) is downloaded from your own Steam account with DepotDownloader."
    echo "Your password / Steam Guard go straight to Steam, this script never sees them."
    read -r -p "Steam login: " STEAM_USER
    dl() { "$DEST/depotdownloader/DepotDownloader" -app $STEAM_APP -os windows -osarch 64 \
           -username "$STEAM_USER" -remember-password -dir "$GAME"; }
    # the first attempt sometimes ends with "A task was canceled" — retry once
    dl || true
    [ -f "$GAME/RainWorld.exe" ] || dl
    [ -f "$GAME/RainWorld.exe" ] || { echo "Download failed."; exit 1; }
fi

# ---------------------------------------------------------------- Mod
say "OpiPerf mod"
SA="$GAME/RainWorld_Data/StreamingAssets"
mkdir -p "$SA/mods/opiperf/plugins"
cp "$REPO/mod/modinfo.json" "$SA/mods/opiperf/"
cp "$REPO/prebuilt/OpiPerf.dll" "$SA/mods/opiperf/plugins/"
grep -qx opiperf "$SA/enabledMods.txt" 2>/dev/null || echo opiperf >> "$SA/enabledMods.txt"
touch "$SA/flatmode.txt"   # flat menu illustrations: big win on weak GPUs/CPUs
MODE=$(DISPLAY=${DISPLAY:-:0} xrandr --current 2>/dev/null | awk '/\*/{print $1; exit}' || true)
W=${MODE%x*}; H=${MODE#*x}; [ -n "$MODE" ] || { W=0; H=0; }
mkdir -p "$GAME/BepInEx/config"
cat > "$GAME/BepInEx/config/opi.perf.cfg" <<EOF
[Render]
ScaleIndex = 6
ShowFps = true
FpsCap = 60

[Shaders]
Killed =

[Window]
Width = $W
Height = $H
EOF
echo "Game window = ${MODE:-unchanged}"

# ---------------------------------------------------------------- Vulkan layer
say "Vulkan layer"
mkdir -p "$DEST/vk-layer"
if command -v gcc >/dev/null && [ -f /usr/include/vulkan/vk_layer.h ]; then
    make -s -C "$REPO/vk-layer" && cp "$REPO/vk-layer/libVkLayer_opi_fakefeatures.so" "$DEST/vk-layer/"
    echo "Built from source."
else
    cp "$REPO/prebuilt/libVkLayer_opi_fakefeatures.so" "$DEST/vk-layer/"
    echo "Using prebuilt (install gcc + libvulkan-dev to build from source)."
fi
cat > "$DEST/vk-layer/opi_fakefeatures.json" <<EOF
{
  "file_format_version": "1.2.0",
  "layer": {
    "name": "VK_LAYER_OPI_fakefeatures",
    "type": "GLOBAL",
    "library_path": "$DEST/vk-layer/libVkLayer_opi_fakefeatures.so",
    "api_version": "1.3.277",
    "implementation_version": "2",
    "description": "Fake GS/tess/multiViewport/pipeline stats for wined3d FL11 on PowerVR, with command sanitizing",
    "functions": { "vkGetInstanceProcAddr": "OPI_GetInstanceProcAddr", "vkGetDeviceProcAddr": "OPI_GetDeviceProcAddr" }
  }
}
EOF

# ---------------------------------------------------------------- Launcher
say "Launcher and shortcuts"
mkdir -p "$HOME/.local/bin" "$HOME/.config" "$HOME/.local/share/applications"
install -m 755 "$REPO/launcher/rainworld" "$HOME/.local/bin/rainworld"
cat > "$HOME/.config/rainworld-arm.conf" <<EOF
RW_GAME="$GAME"
RW_WINE="$DEST/wine10"
RW_BOX64="$BOX64_CMD"
RW_LAYER_DIR="$DEST/vk-layer"
EOF
for mode in gpu cpu; do
    name="Rain World"; cmd="$HOME/.local/bin/rainworld"
    [ $mode = cpu ] && { name="Rain World (CPU fallback)"; cmd="$cmd --cpu"; }
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nIcon=applications-games\nTerminal=false\nCategories=Game;\n' \
        "$name" "$cmd" > "$HOME/.local/share/applications/rainworld-$mode.desktop"
done
rmdir "$TMP" 2>/dev/null || true

cat <<EOF

Done.
  rainworld          GPU mode (wined3d Vulkan + layer), full resolution
  rainworld --cpu    software fallback (llvmpipe, low internal resolution)
In game: F7/F8 render scale, F9 FPS counter.
If the GPU mode hangs or glitches on your board, use --cpu (and see README: watchdog).
EOF
