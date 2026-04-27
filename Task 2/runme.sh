#!/bin/bash

set -euo pipefail

RESULT="result.txt"
> "$RESULT"

log() {
    echo "$@" | tee -a "$RESULT"
}

log "=== Сборка программы ==="
make clean >> "$RESULT" 2>&1
make >> "$RESULT" 2>&1
log "Сборка завершена."

SHARED="sharedfile"
touch "$SHARED"
rm -f "$SHARED.lck" statistics.txt
log "Общий файл: $SHARED"

log "=== Запуск 10 процессов ==="
PIDS=()
for i in $(seq 1 10); do
    ./lock_program "$SHARED" &
    PIDS+=($!)
done
log "Запущены PID: ${PIDS[*]}"
)
log "Ожидание 5 минут..."
sleep 300

log "Отправка SIGINT..."
kill -INT "${PIDS[@]}" 2>/dev/null || true

log "Ожидание завершения всех процессов..."
wait

log "Все процессы завершены."

log ""
log "=== Проверка файла статистики ==="
STATFILE="statistics.txt"
if [ ! -f "$STATFILE" ]; then
    log "ОШИБКА: файл $STATFILE не создан!"
    exit 1
fi

LINE_COUNT=$(wc -l < "$STATFILE")
log "Количество строк в $STATFILE: $LINE_COUNT"
if [ "$LINE_COUNT" -ne 10 ]; then
    log "ОШИБКА: ожидалось 10 строк!"
    exit 1
fi

log "Содержимое $STATFILE:"
cat "$STATFILE" | tee -a "$RESULT"
log ""

LOCK_COUNTS=($(awk '{print $2}' "$STATFILE"))
MIN=$(printf '%d\n' "${LOCK_COUNTS[@]}" | sort -n | head -1)
MAX=$(printf '%d\n' "${LOCK_COUNTS[@]}" | sort -n | tail -1)
SUM=0
for c in "${LOCK_COUNTS[@]}"; do SUM=$((SUM + c)); done
AVG=$((SUM / 10))
DIFF=$((MAX - MIN))

log "Минимум блокировок: $MIN"
log "Максимум блокировок: $MAX"
log "Среднее блокировок: $AVG"

THRESHOLD=$((AVG / 10))
if [ "$DIFF" -le "$THRESHOLD" ]; then
    log "Разброс ($DIFF) приемлемый – блокировки распределены равномерно."
else
    log "ПРЕДУПРЕЖДЕНИЕ: разброс ($DIFF) превышает 10% от среднего ($AVG)."
fi

log ""
log "=== Проверка ошибок (если есть) ==="
if [ -f error.log ]; then
    log "Обнаружен файл error.log!"
else
    log "Аварийных завершений не зафиксировано."
fi

log ""
log "Тестирование успешно завершено."
