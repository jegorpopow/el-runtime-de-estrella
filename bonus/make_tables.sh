#!/usr/bin/env bash
# Превращает bonus/results.csv в Markdown-таблицы (по одной на program×heap).
set -u
BONUS="$(cd "$(dirname "$0")" && pwd)"
CSV="$BONUS/results.csv"

# Уникальные пары (program,heap), строки steps и столбцы thresholds
program_heaps=$(awk -F, 'NR>1 {print $1","$2}' "$CSV" | sort -u)
steps=$(awk        -F, 'NR>1 {print $3}'     "$CSV" | sort -n -u)
thresholds=$(awk   -F, 'NR>1 {print $4}'     "$CSV" | sort -n -u)

for ph in $program_heaps; do
    name="${ph%%,*}"
    heap="${ph##*,}"
    echo "### \`$name\`, heap = $heap"
    echo

    # Шапка
    row="| steps \\ threshold |"
    for t in $thresholds; do
        row="$row $t |"
    done
    echo "$row"

    # Разделитель захардкожен под 4 порога
    echo "|---|---|---|---|---|"

    # Тело
    for s in $steps; do
        row="| $s |"
        for t in $thresholds; do
            v=$(awk -F, -v n="$name" -v h="$heap" -v s="$s" -v t="$t" \
                  'NR>1 && $1==n && $2==h && $3==s && $4==t {print $8; exit}' "$CSV")
            [ -z "$v" ] && v="—"
            row="$row $v |"
        done
        echo "$row"
    done
    echo
done