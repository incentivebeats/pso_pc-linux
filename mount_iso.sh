#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing .env file:"
  echo "  $ENV_FILE"
  exit 1
fi

set -a
. "$ENV_FILE"
set +a

if [[ -z "${ISO_PATH:-}" ]]; then
  echo "ISO_PATH is not set in .env"
  exit 1
fi

if [[ -z "${MOUNT_PATH:-}" ]]; then
  echo "MOUNT_PATH is not set in .env"
  exit 1
fi

if [[ ! -f "$ISO_PATH" ]]; then
  echo "ISO file does not exist:"
  echo "  $ISO_PATH"
  exit 1
fi

for cmd in sudo mount mountpoint; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command:"
    echo "  $cmd"
    exit 1
  fi
done

case "${1:-mount}" in
  mount)
    if mountpoint -q "$MOUNT_PATH"; then
      echo "Already mounted:"
      echo "  $MOUNT_PATH"
      exit 0
    fi

    echo "Creating mount point:"
    echo "  $MOUNT_PATH"
    sudo mkdir -p "$MOUNT_PATH"

    echo
    echo "Mounting ISO:"
    echo "  $ISO_PATH"
    echo
    echo "To:"
    echo "  $MOUNT_PATH"
    echo

    sudo mount -o loop,ro "$ISO_PATH" "$MOUNT_PATH"

    echo
    echo "Mounted successfully."
    echo
    echo "Now run:"
    echo "  ./install.sh"
    ;;

  unmount | umount)
    if ! mountpoint -q "$MOUNT_PATH"; then
      echo "Not mounted:"
      echo "  $MOUNT_PATH"
      exit 0
    fi

    echo "Unmounting:"
    echo "  $MOUNT_PATH"
    sudo umount "$MOUNT_PATH"

    echo
    echo "Unmounted successfully."
    ;;

  *)
    echo "Usage:"
    echo "  ./mount_iso.sh"
    echo "  ./mount_iso.sh mount"
    echo "  ./mount_iso.sh unmount"
    exit 1
    ;;
esac
