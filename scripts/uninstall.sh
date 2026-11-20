#!/usr/bin/env bash
set -euo pipefail

SEVENZIP_DIR="${1:-}"
REMOVED=0

remove_from() {
    local target="$1/Formats/TribesVol.so"
    if [[ -f "$target" ]]; then
        rm -f "$target"
        echo "Removed $target"
        REMOVED=$((REMOVED + 1))
    fi
}

if [[ -n "$SEVENZIP_DIR" ]]; then
    remove_from "$SEVENZIP_DIR"
else
    for dir in \
        /usr/lib/7zip \
        /usr/libexec/7zip \
        /usr/lib64/7zip \
        /usr/local/lib/7zip \
        /usr/lib/p7zip; do
        remove_from "$dir"
    done
fi

if [[ "$REMOVED" -eq 0 ]]; then
    echo "No TribesVol.so plugin found."
fi
