#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$ROOT/tests/programs"
BUILD_DIR="$ROOT/build-tests/programs"

DEBUG=""
[[ "${1:-}" == "-d" ]] && DEBUG="-d"

mkdir -p "$BUILD_DIR"
pass=0 fail=0 failed=()

run() {
    local name="$1"; shift
    local src="$SRC_DIR/$name.c"
    local exe="$BUILD_DIR/$name"

    if ! bash "$ROOT/assemble-stella.sh" $DEBUG "$src" "$exe" >/dev/null; then
        echo "BUILD FAIL: $name"; fail=$((fail + 1)); failed+=("$name"); return
    fi

    if "$exe" "$@" >/dev/null 2>&1; then
        echo "OK:   $name $*"
        pass=$((pass + 1))
    else
        echo "FAIL: $name $* (exit $?)"
        fail=$((fail + 1)); failed+=("$name")
    fi
}

run spam 40 8192
run tree 20 8192
run share 10 4000
run many-roots 30 3400
run refs 10 1024

echo
echo "Passed: $pass   Failed: $fail"
(( fail == 0 )) || { printf '  %s\n' "${failed[@]}"; exit 1; }
