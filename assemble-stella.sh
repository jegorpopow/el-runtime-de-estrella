#!/usr/bin/env bash
set -euo pipefail

DEBUG=0
[[ "${1:-}" == "-d" ]] && { DEBUG=1; shift; }

SRC_FILE="$1"
OUT_BIN="${2:-$(basename "${SRC_FILE%.c}")}"

CFLAGS=(-std=c11 -Iinclude)
[[ $DEBUG -eq 1 ]] && CFLAGS+=(-g -O0 -fsanitize=address,undefined -DSTELLA_GC_DEBUG)

gcc "${CFLAGS[@]}" \
    "$SRC_FILE" \
    runtime/runtime.c \
    runtime/scalar.c \
    runtime/main.c \
    gc.c \
    -o "$OUT_BIN"
