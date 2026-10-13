#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build/abi-violation"
EXPECTED="stella-c-gc-abi-v1: ABI violation:"

DEBUG=0
[[ "${1:-}" == "-d" ]] && DEBUG=1

CFLAGS=(-std=c11 -I"$ROOT/include")
(( DEBUG )) && CFLAGS+=(-g -O0 -fsanitize=address,undefined -DSTELLA_GC_DEBUG)

mkdir -p "$BUILD_DIR"
pass=0 fail=0 failed=()

for src in "$ROOT"/tests/abi-violation/*.c; do
    name="$(basename "$src" .c)"
    exe="$BUILD_DIR/$name"
    err="$BUILD_DIR/$name.stderr"

    if ! gcc "${CFLAGS[@]}" \
            "$src" \
            "$ROOT/runtime/runtime.c" \
            "$ROOT/runtime/scalar.c" \
            "$ROOT/gc.c" \
            -o "$exe"; then
        echo "BUILD FAIL: $name"; fail=$((fail + 1)); failed+=("$name"); continue
    fi

    "$exe" >/dev/null 2>"$err"; rc=$?
    if [ "$rc" -ne 0 ] && grep -qF "$EXPECTED" "$err"; then
        echo "OK:   $name"
        pass=$((pass + 1))
    else
        echo "FAIL: $name (exit $rc)"
        fail=$((fail + 1)); failed+=("$name")
    fi
done

echo
echo "Passed: $pass   Failed: $fail"
(( fail == 0 )) || { printf '  %s\n' "${failed[@]}"; exit 1; }
