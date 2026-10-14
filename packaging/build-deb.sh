#!/usr/bin/env bash
# Build a Termux .deb of socsonde.
#
#  On the phone, in Termux (pkg install clang make vulkan-headers):
#      ./packaging/build-deb.sh
#  Cross-compiling with the Android NDK (CI or a desktop):
#      ANDROID_NDK_HOME=/path/to/ndk ./packaging/build-deb.sh aarch64
#
# Output: socsonde_<version>_<arch>.deb in the current directory.
# Install on the phone with: apt install ./socsonde_<version>_<arch>.deb

set -euo pipefail
umask 022 # Termux defaults to 077. Package files must be readable by all
cd "$(dirname "$0")/.."

TERMUX_PREFIX=/data/data/com.termux/files/usr
API=28 # Termux's minimum Android API level
VERSION="${VERSION:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)}"
VERSION="${VERSION:-0.0.0}"
HOMEPAGE="${HOMEPAGE:-https://github.com/matiaslin/socsonde}"
MAINTAINER="${MAINTAINER:-<your name> <you@example.com>}"

if [ "${PREFIX:-}" = "$TERMUX_PREFIX" ]; then
    # Building on the phone: Termux's own clang, and its architecture name.
    ARCH="${1:-$(dpkg --print-architecture)}"
    CC=cc
    STRIP="strip"
else
    ARCH="${1:?usage: $0 <aarch64|arm|x86_64|i686>   (set ANDROID_NDK_HOME)}"
    NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
    [ -n "$NDK" ] || { echo "set ANDROID_NDK_HOME to the Android NDK" >&2; exit 2; }
    case "$ARCH" in
        aarch64) TRIPLE=aarch64-linux-android ;;
        arm)     TRIPLE=armv7a-linux-androideabi ;;
        x86_64)  TRIPLE=x86_64-linux-android ;;
        i686)    TRIPLE=i686-linux-android ;;
        *) echo "unknown arch '$ARCH' (use Termux names: aarch64, arm, x86_64, i686)" >&2; exit 2 ;;
    esac
    BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
    CC="$BIN/${TRIPLE}${API}-clang" # the NDK sysroot already includes vulkan.h
    STRIP="$BIN/llvm-strip"
fi

make clean >/dev/null
make CC="$CC" socsonde
"$STRIP" socsonde

STAGE="$(mktemp -d)"
chmod 755 "$STAGE"
trap 'rm -rf "$STAGE"' EXIT
make install DESTDIR="$STAGE" PREFIX="$TERMUX_PREFIX" >/dev/null
SIZE_KB="$(du -sk "$STAGE" | cut -f1)"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: socsonde
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $SIZE_KB
Homepage: $HOMEPAGE
Description: CPU and GPU monitoring for Android, with test loads
 Reads the CPU and GPU counters an unrooted app is allowed to access, and
 includes CPU and GPU test loads to check that the readings respond.
EOF

OUT="socsonde_${VERSION}_${ARCH}.deb"
# We use xz because zstd can't be read by Termux's apt.
dpkg-deb --build --root-owner-group -Zxz "$STAGE" "$OUT" >/dev/null
make clean >/dev/null
echo "built $OUT"
