#!/bin/bash
# Create a macOS .app bundle for Quake Live
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
	echo "Usage: $0 <release|debug> [arch]"
	exit 1
fi

TARGET_NAME="$1"
ARCH="${2:-$(uname -m)}"
OBJROOT="build"
BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${ARCH}"

if [ ! -d "${BUILT_PRODUCTS_DIR}" ]; then
	echo "Build directory not found: ${BUILT_PRODUCTS_DIR}"
	exit 1
fi

PRODUCT_NAME="quakelive"
WRAPPER_NAME="${PRODUCT_NAME}.app"
CONTENTS="${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/Contents"
MACOS="${CONTENTS}/MacOS"
RESOURCES="${CONTENTS}/Resources"
BASEDIR="baseq3"

# Check for client binary
CLIENT="${BUILT_PRODUCTS_DIR}/${PRODUCT_NAME}.${ARCH}"
if [ ! -f "${CLIENT}" ]; then
	echo "Client binary not found: ${CLIENT}"
	exit 1
fi

echo "Creating ${WRAPPER_NAME} for ${ARCH}..."

# Create bundle structure
mkdir -p "${MACOS}"
mkdir -p "${RESOURCES}"

# Copy executables
cp "${CLIENT}" "${MACOS}/${PRODUCT_NAME}"
if [ -f "${BUILT_PRODUCTS_DIR}/quakelive_dedicated.${ARCH}" ]; then
	cp "${BUILT_PRODUCTS_DIR}/quakelive_dedicated.${ARCH}" "${MACOS}/quakelive_dedicated"
fi

# Copy renderer (engine loads cl_renderer + ARCH_STRING + DLL_EXT, e.g. opengl2arm64.dylib)
if [ -f "${BUILT_PRODUCTS_DIR}/opengl2${ARCH}.dylib" ]; then
	cp "${BUILT_PRODUCTS_DIR}/opengl2${ARCH}.dylib" "${MACOS}/opengl2${ARCH}.dylib"
fi

# Populate Contents/Resources/baseq3/ with our bundled pk3s.
# fs_apppath on macOS points to Contents/Resources (via Sys_DefaultAppPath),
# so the engine finds these regardless of where the .app lives.
RESOURCES="${CONTENTS}/Resources"
mkdir -p "${RESOURCES}/baseq3"

# CI passes a pre-built universal iobin.pk3 via PREBUILT_IOBIN_PK3 - that
# pk3 contains signed macOS dylibs alongside the Linux/Windows modules and
# is the single authoritative copy shipped across all platforms. Dev builds
# (no env var) fall back to a macOS-only pk3 from the local dylibs.
#
# If PREBUILT_IOBIN_PK3 is *set* but the file is missing we hard-fail
# instead of silently dropping back to the local-dylibs branch - the latter
# would build an incomplete pk3 (or none at all if there are no dylibs)
# and the user wouldn't notice until the engine crashed at runtime with
# "VM_Create: failed to extract game module 'ui'".
DYLIB_DIR="${BUILT_PRODUCTS_DIR}/baseq3"
if [ -n "${PREBUILT_IOBIN_PK3:-}" ]; then
	if [ ! -f "${PREBUILT_IOBIN_PK3}" ]; then
		echo "ERROR: PREBUILT_IOBIN_PK3 is set to '${PREBUILT_IOBIN_PK3}' but that file does not exist." >&2
		exit 1
	fi
	cp "${PREBUILT_IOBIN_PK3}" "${RESOURCES}/baseq3/iobin.pk3"
	echo "Bundled pre-built iobin.pk3 from ${PREBUILT_IOBIN_PK3}"
elif [ -d "${DYLIB_DIR}" ]; then
	# Stage to a temp dir - baseq3/ may also contain pak01.pk3 which must
	# not end up inside iobin.pk3.
	STAGE=$(mktemp -d)
	dylib_count=0
	for dylib in "${DYLIB_DIR}"/*.dylib; do
		[ -f "$dylib" ] || continue
		cp "$dylib" "${STAGE}/"
		dylib_count=$((dylib_count + 1))
	done
	if [ "$dylib_count" -eq 0 ]; then
		echo "ERROR: ${DYLIB_DIR} exists but contains no *.dylib files." >&2
		echo "       Either set PREBUILT_IOBIN_PK3 or build the game dylibs first." >&2
		rm -rf "${STAGE}"
		exit 1
	fi
	python3 code/tools/make_deterministic_pk3.py \
		--input "${STAGE}" \
		--output "${RESOURCES}/baseq3/iobin.pk3"
	rm -rf "${STAGE}"
	echo "Bundled iobin.pk3 in Contents/Resources/baseq3/ (from ${dylib_count} local dylibs)"
else
	echo "ERROR: no source for iobin.pk3 - PREBUILT_IOBIN_PK3 is unset and ${DYLIB_DIR} does not exist." >&2
	exit 1
fi

# Belt-and-braces: the iobin step above is supposed to leave a file behind.
# Hard-fail if it didn't (e.g. cp silently mangled by something exotic).
if [ ! -f "${RESOURCES}/baseq3/iobin.pk3" ]; then
	echo "ERROR: ${RESOURCES}/baseq3/iobin.pk3 was not created." >&2
	exit 1
fi

# Copy pak01.pk3 if the CI built it before this step.
if [ -f "${BUILT_PRODUCTS_DIR}/baseq3/pak01.pk3" ]; then
	cp "${BUILT_PRODUCTS_DIR}/baseq3/pak01.pk3" "${RESOURCES}/baseq3/"
	echo "Bundled pak01.pk3 in Contents/Resources/baseq3/"
fi

# Bundle all Homebrew dylib dependencies into Contents/Frameworks/.
#
# The hardened-runtime app (signed with a Developer ID) cannot load dylibs
# from Homebrew because their Team ID doesn't match.  The fix is to copy
# every non-system dylib into Contents/Frameworks/, rewrite all load paths
# to @rpath/<name>, sign them with the same certificate (done by build.yml),
# and add an @executable_path/../Frameworks rpath to each in-bundle binary.
#
# This is done recursively so transitive deps (e.g. freetype -> libpng) are
# also caught automatically. dlopen'd deps leave no load command and are
# invisible to the walk; the submodule-built SDL2 is bundled by hand below.
FRAMEWORKS="${CONTENTS}/Frameworks"
mkdir -p "${FRAMEWORKS}"

# bundle_dep <src_path>
#   Copy a Homebrew dylib into Frameworks/, fix its -id, add a self-relative
#   rpath, and recurse into its own deps.  No-op if already bundled.
bundle_dep() {
    local src="$1"
    local name
    name=$(basename "$src")
    local dst="${FRAMEWORKS}/${name}"
    [ -f "$dst" ] && return 0   # already bundled
    [ -f "$src" ] || { echo "  Warning: $src not found"; return 1; }
    cp "$src" "$dst"
    chmod 755 "$dst"
    install_name_tool -id "@rpath/${name}" "$dst"
    # Bundled dylibs find their peers via @loader_path (= Frameworks/)
    install_name_tool -add_rpath "@loader_path/" "$dst" 2>/dev/null || true
    echo "  Bundled ${name}"
    rewrite_homebrew_deps "$dst"
}

# rewrite_homebrew_deps <binary>
#   For every /opt/homebrew or /usr/local dep in <binary>:
#     - rewrite the load command to @rpath/<name>
#     - bundle the dep (which recurses)
rewrite_homebrew_deps() {
    local bin="$1"
    # Collect deps first (otool output can't pipe into install_name_tool safely)
    local deps
    deps=$(otool -L "$bin" 2>/dev/null | awk 'NR>1 {print $1}')
    local dep
    while IFS= read -r dep; do
        case "$dep" in
            /opt/homebrew/*|/usr/local/Cellar/*|/usr/local/opt/*)
                local name
                name=$(basename "$dep")
                install_name_tool -change "$dep" "@rpath/${name}" "$bin" 2>/dev/null
                bundle_dep "$dep"
                ;;
        esac
    done <<< "$deps"
}

echo "Bundling Homebrew dependencies..."
for bin in \
    "${MACOS}/${PRODUCT_NAME}" \
    "${MACOS}/quakelive_dedicated" \
    "${MACOS}/opengl2${ARCH}.dylib"; do
    [ -f "$bin" ] || continue
    # Add rpath so this binary (and any dylib it dlopen's) can find Frameworks/
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin" 2>/dev/null || true
    rewrite_homebrew_deps "$bin"
done
echo "Done bundling Homebrew dependencies."

# SDL2 is built from the code/SDL2 submodule by CI and shipped beside the
# raw engine binaries; its load command is @rpath/libSDL2-2.0.0.dylib, which
# the otool walk above ignores (not a brew path), so bundle it by hand.
if [ -f "${BUILT_PRODUCTS_DIR}/libSDL2-2.0.0.dylib" ]; then
    bundle_dep "${BUILT_PRODUCTS_DIR}/libSDL2-2.0.0.dylib"
fi

# The app must ship exactly one real SDL2. Homebrew's "sdl2" is now
# sdl2-compat (the SDL2 ABI over a dlopen'd libSDL3, invisible to otool),
# which dies on user machines with "Failed loading SDL3 library." Refuse to
# package a bundle that has no SDL2 or carries the shim.
SDL2_BUNDLED=$(ls "${FRAMEWORKS}"/libSDL2*.dylib 2>/dev/null || true)
if [ -z "${SDL2_BUNDLED}" ]; then
    echo "ERROR: no libSDL2 dylib was bundled into ${FRAMEWORKS}." >&2
    echo "       Build the code/SDL2 submodule and place libSDL2-2.0.0.dylib in ${BUILT_PRODUCTS_DIR}." >&2
    exit 1
fi
for lib in ${SDL2_BUNDLED}; do
    if grep -qaF 'libSDL3.dylib' "$lib"; then
        echo "ERROR: $(basename "$lib") is the sdl2-compat shim (wants libSDL3 at runtime)." >&2
        echo "       Link against the code/SDL2 submodule build, not Homebrew's sdl2." >&2
        exit 1
    fi
done

# Generate .icns from quakelive.ico
ICNS_NAME="quakelive.icns"
ICO_SRC="misc/quakelive.ico"
if [ -f "${ICO_SRC}" ] && command -v sips &>/dev/null && command -v iconutil &>/dev/null; then
	ICONSET=$(mktemp -d)/quakelive.iconset
	mkdir -p "${ICONSET}"

	# Extract the largest image from the ico and resize to all required sizes
	sips -s format png "${ICO_SRC}" --out "${ICONSET}/icon_512x512@2x.png" &>/dev/null
	for SIZE in 16 32 128 256 512; do
		sips -z ${SIZE} ${SIZE} "${ICONSET}/icon_512x512@2x.png" --out "${ICONSET}/icon_${SIZE}x${SIZE}.png" &>/dev/null
	done
	for SIZE in 16 32 128 256; do
		DOUBLE=$((SIZE * 2))
		sips -z ${DOUBLE} ${DOUBLE} "${ICONSET}/icon_512x512@2x.png" --out "${ICONSET}/icon_${SIZE}x${SIZE}@2x.png" &>/dev/null
	done

	iconutil -c icns "${ICONSET}" -o "${RESOURCES}/${ICNS_NAME}" 2>/dev/null
	rm -rf "$(dirname "${ICONSET}")"
	echo "Generated ${ICNS_NAME} from ${ICO_SRC}"
elif [ -f "misc/quakelive.icns" ]; then
	cp "misc/quakelive.icns" "${RESOURCES}/${ICNS_NAME}"
else
	echo "Warning: No icon source found, .app will have no icon"
	ICNS_NAME=""
fi

# PkgInfo
echo -n "APPLQLIV" > "${CONTENTS}/PkgInfo"

# Get version
VERSION=$(grep '^VERSION=' Makefile 2>/dev/null | head -1 | sed 's/VERSION=//')
[ -z "$VERSION" ] && VERSION=$(git describe --always 2>/dev/null || echo "dev")

# Info.plist
cat > "${CONTENTS}/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>${PRODUCT_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>${ICNS_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>com.tjone270.${PRODUCT_NAME}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Quake Live</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CGDisableCoalescedUpdates</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSRequiresAquaSystemAppearance</key>
    <false/>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 2015 id Software LLC, a ZeniMax Media company. All rights reserved.</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
PLIST

echo "Created ${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}"
