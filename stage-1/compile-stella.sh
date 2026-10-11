#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $(basename "$0") <stella-file>" >&2
    exit 1
fi

STELLA_FILE=$1

if [[ ! -f "$STELLA_FILE" ]]; then
    echo "Error: file '$STELLA_FILE' not found" >&2
    exit 1
fi

IMAGE=fizruk/stella
DIGEST=sha256:86d6123aff226787a02b1b8021c7d4f2cd3703586155f5a96d6f0a7c21d805c6

docker run --rm -i "$IMAGE@$DIGEST" compile /dev/stdin -o /dev/stdout < "$STELLA_FILE"
