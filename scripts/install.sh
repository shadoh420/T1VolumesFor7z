#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEVENZIP_DIR="${1:-}"
SO_SRC=""

for candidate in \
    "$SCRIPT_DIR/TribesVol.so" \
    "$SCRIPT_DIR/../TribesVol.so" \
    "$SCRIPT_DIR/../build/TribesVol.so"; do
    if [[ -f "$candidate" ]]; then
        SO_SRC="$candidate"
        break
    fi
done

if [[ -z "$SO_SRC" ]]; then
    echo "Error: TribesVol.so not found." >&2
    exit 1
fi

if [[ -z "$SEVENZIP_DIR" ]]; then
    for dir in \
        /usr/lib/7zip \
        /usr/libexec/7zip \
        /usr/lib64/7zip \
        /usr/local/lib/7zip \
        /usr/lib/p7zip; do
        if [[ -f "$dir/7z.so" ]]; then
            SEVENZIP_DIR="$dir"
            break
        fi
    done
fi

if [[ -z "$SEVENZIP_DIR" || ! -f "$SEVENZIP_DIR/7z.so" ]]; then
    echo "Error: Modular 7-Zip (7z.so) not found." >&2
    echo "Install the distro '7zip' package (not standalone 7zz), or pass its directory:" >&2
    echo "  sudo $0 /path/to/7zip-dir" >&2
    exit 1
fi

FORMATS_DIR="$SEVENZIP_DIR/Formats"
install -d -m 0755 "$FORMATS_DIR"
install -m 0755 "$SO_SRC" "$FORMATS_DIR/TribesVol.so"
echo "Installed TribesVol.so -> $FORMATS_DIR/TribesVol.so"
