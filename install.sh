#!/bin/sh
#
# Oopsie installer -- builds from source and installs into your home directory.
# No root required.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/phdgamer0/oopsie/linux_user/install.sh | sh
#
# Environment overrides:
#   PREFIX            install directory             (default: $HOME/.local/opt/oopsie)
#   BIN_DIR           where the launcher is linked  (default: $HOME/.local/bin)
#   REPO_URL          source repository             (default: https://github.com/phdgamer0/oopsie.git)
#   REF               branch or tag to build        (default: linux_user)
#   CC                C compiler to use             (default: auto-detect gcc, clang, cc)
#   JOBS              parallel build jobs           (default: nproc)
#   SKIP_NET_CHECK=1  skip the connectivity probe
#
set -eu

REPO_URL=${REPO_URL:-https://github.com/phdgamer0/oopsie.git}
REF=${REF:-linux_user}
PREFIX=${PREFIX:-${HOME}/.local/opt/oopsie}
BIN_DIR=${BIN_DIR:-${HOME}/.local/bin}
JOBS=${JOBS:-0}

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
   C_RESET=$(printf '\033[0m')
   C_RED=$(printf '\033[31m')
   C_YEL=$(printf '\033[33m')
   C_GRN=$(printf '\033[32m')
   C_BLD=$(printf '\033[1m')
else
   C_RESET=''
   C_RED=''
   C_YEL=''
   C_GRN=''
   C_BLD=''
fi

step() { printf '%s==>%s %s\n' "$C_BLD" "$C_RESET" "$1"; }
info() { printf '    %s\n' "$1"; }
ok()   { printf '    %sok%s   %s\n' "$C_GRN" "$C_RESET" "$1"; }
warn() { printf '%swarn%s %s\n' "$C_YEL" "$C_RESET" "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

pkg_hint() {
   _id=$( . /etc/os-release 2>/dev/null; printf '%s' "${ID:-}" )
   case "$_id" in
      debian|ubuntu|linuxmint|pop|elementary)
         printf 'sudo apt-get update && sudo apt-get install -y build-essential cmake git' ;;
      fedora|rhel|centos|rocky|alma)
         printf 'sudo dnf install -y gcc cmake make git glibc-devel' ;;
      arch|manjaro|endeavouros)
         printf 'sudo pacman -S --needed base-devel cmake git' ;;
      alpine)
         printf 'sudo apk add build-base cmake git' ;;
      suse|opensuse*)
         printf 'sudo zypper install gcc cmake make git' ;;
      *)
         printf '' ;;
   esac
}

die() {
   printf '\n%serror%s %s\n' "$C_RED" "$C_RESET" "$1" >&2
   _hint=$(pkg_hint)
   if [ -n "$_hint" ]; then
      printf 'On this system that usually means:\n    %s\n' "$_hint" >&2
   fi
   exit 1
}

TMPWORK=''
cleanup() {
   if [ -n "$TMPWORK" ]; then
      rm -rf "$TMPWORK"
      TMPWORK=''
   fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap 'cleanup; exit 129' HUP

step "Checking prerequisites"

missing_tools=''
for tool in git make mktemp sed grep cat rm mkdir cp ln; do
   have "$tool" || missing_tools="${missing_tools} ${tool}"
done
if [ -n "$missing_tools" ]; then
   die "missing core tools:${missing_tools}"
fi
ok "core tools present"

if [ -n "${CC:-}" ]; then
   candidates=$CC
else
   candidates=''
   for c in gcc clang cc; do
      have "$c" && candidates="${candidates} ${c}"
   done
fi
if [ -z "$(printf '%s' "$candidates" | tr -d ' ')" ]; then
   die "no C compiler found. Install gcc or clang."
fi

TMPWORK=$(mktemp -d)
cat > "$TMPWORK/probe.c" <<'PROBE'
#include <stddef.h>
#ifndef unreachable
#error "no C23 unreachable()"
#endif
int main(void) { return 0; }
PROBE

CC_BIN=''
CC_STD=''
for c in $candidates; do
   for std in c2x c23; do
      if "$c" -std="$std" "$TMPWORK/probe.c" -o "$TMPWORK/probe.bin" >/dev/null 2>&1; then
         CC_BIN=$c
         CC_STD=$std
         break
      fi
   done
   if [ -n "$CC_BIN" ]; then
      break
   fi
done
rm -f "$TMPWORK/probe.bin"

if [ -z "$CC_BIN" ]; then
   die "no C compiler with C23 support found (tried:${candidates}).
Oopsie needs C23 because src/main.c uses the unreachable() macro from <stddef.h>.
GCC 13+ or Clang 15+ is required."
fi
ok "C compiler: ${CC_BIN} (-std=${CC_STD})"

CMAKE_BIN=''
for c in cmake cmake3; do
   if have "$c"; then
      CMAKE_BIN=$c
      break
   fi
done
if [ -z "$CMAKE_BIN" ]; then
   die "cmake not found. Oopsie needs cmake 3.16 or newer."
fi

cmake_ver=$("$CMAKE_BIN" --version 2>/dev/null | head -1 | sed 's/[^0-9]*\([0-9][0-9.]*\).*/\1/')
cmake_maj=$(printf '%s' "$cmake_ver" | cut -d. -f1)
cmake_min=$(printf '%s' "$cmake_ver" | cut -d. -f2)
case "$cmake_maj" in
   ''|*[!0-9]*) die "could not determine cmake version from '$cmake_ver'." ;;
esac
if [ "$cmake_maj" -lt 3 ] || { [ "$cmake_maj" -eq 3 ] && [ "${cmake_min:-0}" -lt 16 ]; }; then
   die "cmake ${cmake_ver} is too old. Oopsie needs 3.16 or newer."
fi
ok "cmake ${cmake_ver}"

if [ "${SKIP_NET_CHECK:-0}" != "1" ]; then
   step "Checking network access"
   if have curl; then
      probe_url() { curl -fsS --max-time 20 -o /dev/null "$1" >/dev/null 2>&1; }
   elif have wget; then
      probe_url() { wget -q --spider -T 20 "$1" >/dev/null 2>&1; }
   else
      warn "neither curl nor wget found; skipping connectivity probe"
      probe_url() { return 0; }
   fi
   net_problem=''
   for u in https://github.com https://raw.githubusercontent.com "$REPO_URL"; do
      if probe_url "$u"; then
         ok "$(printf '%s' "$u" | sed 's|https://||')"
      else
         warn "cannot reach $u"
         net_problem=1
      fi
   done
   if [ -n "$net_problem" ]; then
      die "network check failed.
The build needs network access to fetch termbox2.h and clone liburing.
You can bypass this probe with SKIP_NET_CHECK=1."
   fi
fi

if [ "$JOBS" = 0 ] || [ -z "$JOBS" ]; then
   if have nproc; then
      JOBS=$(nproc)
   elif have getconf; then
      JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 2)
   else
      JOBS=2
   fi
fi

step "Fetching source (${REF})"
SRC="$TMPWORK/src"
if ! GIT_TERMINAL_PROMPT=0 git clone --depth 1 --single-branch --branch "$REF" "$REPO_URL" "$SRC"; then
   die "could not clone ${REPO_URL} (ref: ${REF}).
If the repository is private, make it public or make your SSH key available.
GIT_TERMINAL_PROMPT=0 is set on purpose so this fails fast instead of hanging
forever waiting for input."
fi
ok "cloned"

BUILD="$SRC/build"

step "Building"
printf '    this compiles liburing from source, expect a minute on a cold cache\n'
if ! "$CMAKE_BIN" -S "$SRC" -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="$CC_BIN" >"$TMPWORK/configure.log" 2>&1; then
   printf '\n'
   tail -n 25 "$TMPWORK/configure.log" >&2
   printf '\n'
   die "cmake configure failed. Last lines above."
fi
ok "configured"

if ! "$CMAKE_BIN" --build "$BUILD" -j "$JOBS" >"$TMPWORK/build.log" 2>&1; then
   printf '\n'
   tail -n 25 "$TMPWORK/build.log" >&2
   printf '\n'
   die "build failed. Last lines above."
fi

for artifact in Oopsie liboopsie_shim.so; do
   if [ ! -f "$BUILD/$artifact" ]; then
      die "build finished but ${artifact} is missing from ${BUILD}"
   fi
done
ok "built (jobs: ${JOBS})"

step "Installing to ${PREFIX}"
mkdir -p "$PREFIX" "$BIN_DIR"
rm -f "$PREFIX/Oopsie" "$PREFIX/liboopsie_shim.so"
cp "$BUILD/Oopsie" "$PREFIX/Oopsie"
cp "$BUILD/liboopsie_shim.so" "$PREFIX/liboopsie_shim.so"
chmod 0755 "$PREFIX/Oopsie" "$PREFIX/liboopsie_shim.so"
ln -sf "$PREFIX/Oopsie" "$BIN_DIR/oopsie"
ln -sf "$PREFIX/Oopsie" "$BIN_DIR/Oopsie"
ok "files copied"

if "$PREFIX/Oopsie" __smoke_test__ 2>&1 | grep -q 'Unknown command'; then
   ok "binary runs"
else
   warn "binary smoke test gave unexpected output"
fi

if LD_PRELOAD="$PREFIX/liboopsie_shim.so" true >/dev/null 2>&1; then
   ok "shim loads"
else
   warn "shim failed to load; monitoring may not work"
fi

printf '\n%sOopsie installed.%s\n' "$C_GRN" "$C_RESET"
printf '  binary   %s\n' "$PREFIX/Oopsie"
printf '  shim     %s\n' "$PREFIX/liboopsie_shim.so"
printf '  command  %s/oopsie\n' "$BIN_DIR"
printf '\n'

case ":${PATH}:" in
   *":${BIN_DIR}:"*)
      printf 'Start monitoring with:\n  oopsie start\n\n'
      ;;
   *)
      printf 'Start monitoring with:\n'
      printf '  export PATH="%s:$PATH"\n' "$BIN_DIR"
      printf '  oopsie start\n\n'
      printf '%snote%s add that export line to your shell profile to make it permanent.\n\n' "$C_YEL" "$C_RESET"
      ;;
esac

printf 'Remove with:\n'
printf '  oopsie uninstall            # clears the recorded history in /tmp/oopsie\n'
printf '  rm -rf %s %s/oopsie %s/Oopsie\n' "$PREFIX" "$BIN_DIR" "$BIN_DIR"
printf '\n'
