#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"

if [[ "${EUID}" -eq 0 ]]; then
  echo "Do not run this as root. Run it as your normal user."
  exit 1
fi

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing .env file:"
  echo "  $ENV_FILE"
  exit 1
fi

set -a
. "$ENV_FILE"
set +a

: "${PREFIX:=$HOME/.local/share/pso_pc}"

REQUESTED_WINEARCH="${WINEARCH:-}"

# Do not let an inherited WINEARCH poison later Wine/winetricks calls.
# We only apply WINEARCH deliberately during initial wineboot below.
unset WINEARCH

if [[ -z "${MOUNT_PATH:-}" ]]; then
  echo "MOUNT_PATH is not set in .env"
  exit 1
fi

for cmd in wine wineboot winetricks find mountpoint; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command:"
    echo "  $cmd"
    exit 1
  fi
done

if ! mountpoint -q "$MOUNT_PATH"; then
  echo "PSO install ISO is not mounted at:"
  echo "  $MOUNT_PATH"
  echo
  echo "Run this first:"
  echo "  ./mount_iso.sh"
  exit 1
fi

SETUP_EXE="$(
  find "$MOUNT_PATH" -maxdepth 2 -type f -iname 'setup.exe' -print -quit
)"

if [[ -z "$SETUP_EXE" ]]; then
  echo "Could not find setup.exe inside:"
  echo "  $MOUNT_PATH"
  echo
  echo "Make sure the mounted ISO is the PSO PC Installation Disc."
  exit 1
fi

export WINEPREFIX="$PREFIX"

PREFIX_ALREADY_EXISTED=0
if [[ -e "$WINEPREFIX" ]]; then
  PREFIX_ALREADY_EXISTED=1
fi

echo
echo "Using Wine prefix:"
echo "  $WINEPREFIX"
echo
echo "Using installer:"
echo "  $SETUP_EXE"
echo

create_or_update_prefix() {
  if [[ "$PREFIX_ALREADY_EXISTED" -eq 1 ]]; then
    echo "Wine prefix already exists; updating it."
    echo "Ignoring WINEARCH because existing prefixes cannot safely change architecture."
    wineboot -u
    return
  fi

  if [[ -n "$REQUESTED_WINEARCH" ]]; then
    echo "Creating Wine prefix with requested WINEARCH:"
    echo "  $REQUESTED_WINEARCH"
    echo

    if WINEARCH="$REQUESTED_WINEARCH" wineboot -u; then
      return
    fi

    rc=$?

    if [[ "$REQUESTED_WINEARCH" == "win32" ]]; then
      echo
      echo "Creating a win32-only prefix failed."
      echo "This Wine build may be using WoW64 mode, where WINEARCH=win32 is not supported."
      echo

      if [[ -d "$WINEPREFIX" ]]; then
        FAILED_PREFIX="${WINEPREFIX}.failed-win32.$(date +%Y%m%d-%H%M%S)"
        echo "Moving failed partial prefix aside:"
        echo "  $FAILED_PREFIX"
        mv -- "$WINEPREFIX" "$FAILED_PREFIX"
      fi

      echo
      echo "Retrying with Wine's default prefix mode."
      echo "This should create a WoW64-capable prefix on modern Wine."
      wineboot -u
      return
    fi

    return "$rc"
  fi

  echo "Creating Wine prefix with Wine's default architecture."
  echo "On modern Arch/Wine, this is usually WoW64-capable."
  wineboot -u
}

create_or_update_prefix

echo
echo "Installing required Wine components..."
winetricks -q directmusic dsound cjkfonts corefonts

echo
echo "Setting required DLL overrides..."
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v dsound /d native,builtin /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v dmusic /d native,builtin /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v dmime /d native,builtin /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v dmsynth /d native,builtin /f

echo
echo "Starting PSO installer."
echo "Choose the Normal install option when prompted."
echo

wine "$SETUP_EXE"

echo
echo "PSO initial install finished."
echo
echo "Prefix:"
echo "  $WINEPREFIX"
echo
echo "Likely install paths:"
echo "  $WINEPREFIX/drive_c/Program Files/SEGA/PhantasyStarOnline"
echo "  $WINEPREFIX/drive_c/Program Files (x86)/SEGA/PhantasyStarOnline"
echo
echo "Next manual step:"
echo "  Copy the IVES launchers into the installed PSO folder."
