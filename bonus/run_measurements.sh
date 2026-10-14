#!/usr/bin/env bash
# Измеряет влияние STELLA_GC_STEPS и STELLA_GC_TRESHOLD на работу сборщика.
# Требует: assemble-stella.sh в корне репозитория, STELLA_GC_STATS=1 в gc.c.
# Пишет bonus/results.csv.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BONUS="$ROOT/bonus"
BUILD="$BONUS/build"
CSV="$BONUS/results.csv"

# name:src:arg:heap1,heap2,heap3
PROGRAMS=(
  "chat:$ROOT/examples/chat.c:20:12000,16384,24576"
  "playlist:$ROOT/examples/playlist.c:10:4096,8192,16384"
  "spam:$ROOT/tests/programs/spam.c:10:2048,4096,8192"
)

STEPS=(1 4 16 64)
THRESHOLDS=(0.0 0.25 0.5 0.75)
RUNS=5

mkdir -p "$BUILD"
echo "program,heap,steps,threshold,collections,barriers,max_occupied,time_median,status" > "$CSV"

cd "$ROOT"

declare -A EXE
for entry in "${PROGRAMS[@]}"; do
  IFS=':' read -r name src arg heaps <<< "$entry"
  exe="$BUILD/$name"
  if ! bash assemble-stella.sh "$src" "$exe" >"$BUILD/$name.build.log" 2>&1; then
    echo "BUILD FAIL: $name (см. $BUILD/$name.build.log)" >&2
    exit 1
  fi
  EXE[$name]="$exe"
done

extract() { sed -n "s/^$1: //p" "$2" | head -n1; }
now()     { date +%s.%N; }
median()  { sort -n | awk '{a[NR]=$1} END{ if (NR==0){print "NA"; exit}
              if (NR%2) print a[(NR+1)/2]
              else      print (a[NR/2]+a[NR/2+1])/2 }'; }

for entry in "${PROGRAMS[@]}"; do
  IFS=':' read -r name src arg heaps <<< "$entry"
  exe="${EXE[$name]}"

  IFS=',' read -ra heap_list <<< "$heaps"
  for heap in "${heap_list[@]}"; do
    for steps in "${STEPS[@]}"; do
      for thr in "${THRESHOLDS[@]}"; do
        err="$BUILD/run.err"
        times=(); status="ok"
        collections="NA"; barriers="NA"; max_occ="NA"

        for ((r=1; r<=RUNS; r++)); do
          t0=$(now)
          STELLA_GC_STATS=1 \
          STELLA_GC_STEPS="$steps" \
          STELLA_GC_TRESHOLD="$thr" \
            "$exe" "$arg" "$heap" >/dev/null 2>"$err"
          rc=$?
          t1=$(now)
          if [ "$rc" -ne 0 ]; then
            status="exit$rc"
            break
          fi
          times+=("$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.6f", b-a}')")
          collections=$(extract "collections finished" "$err")
          barriers=$(extract "read barrier activations" "$err")
          max_occ=$(extract "maximum occupied bytes" "$err")
        done

        if [ "$status" = "ok" ] && [ "${#times[@]}" -gt 0 ]; then
          med=$(printf '%s\n' "${times[@]}" | median)
        else
          med="NA"
        fi

        echo "$name,$heap,$steps,$thr,$collections,$barriers,$max_occ,$med,$status" >> "$CSV"
      done
    done
  done
done

echo "Готово: $CSV"
