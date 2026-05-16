#!/bin/bash

set -euo pipefail

RESULT="result.txt"
TESTDIR="/tmp/myinit_test"
LOG="/tmp/myinit.log"
PIDFILE="/tmp/myinit.pid"

> "$RESULT"
rm -rf "$TESTDIR"
mkdir -p "$TESTDIR"
> "$LOG"
rm -f "$PIDFILE"

log() { echo "$@" | tee -a "$RESULT"; }

log "========================================="
log "Тестирование myinit (задача 3)"
log "Дата: $(date)"
log "========================================="
log ""

log "--- Сборка программы ---"
make clean > /dev/null 2>&1 || true
if make >> "$RESULT" 2>&1; then
    log "Сборка успешна."
else
    log "ОШИБКА сборки."
    exit 1
fi

IN1="$TESTDIR/in1"; OUT1="$TESTDIR/out1"
IN2="$TESTDIR/in2"; OUT2="$TESTDIR/out2"
IN3="$TESTDIR/in3"; OUT3="$TESTDIR/out3"
touch "$IN1" "$OUT1" "$IN2" "$OUT2" "$IN3" "$OUT3"

CONFIG1="$TESTDIR/config1"
cat > "$CONFIG1" <<EOF
/bin/sleep 1000 $IN1 $OUT1
/bin/sleep 1000 $IN2 $OUT2
/bin/sleep 1000 $IN3 $OUT3
EOF

CONFIG2="$TESTDIR/config2"
cat > "$CONFIG2" <<EOF
/bin/sleep 1000 $IN1 $OUT1
EOF

log "--- Тест 1: запуск трёх дочерних процессов ---"
log "Ожидаемый результат: 3 процесса-потомка."
./myinit "$CONFIG1" &
sleep 2

if [ ! -f "$PIDFILE" ]; then
    log "ОШИБКА: PID-файл /tmp/myinit.pid не создан."
    exit 1
fi
MYINIT_PID=$(cat "$PIDFILE")
log "PID демона: $MYINIT_PID"

CHILDREN=$(pgrep -P "$MYINIT_PID" 2>/dev/null || true)
COUNT=$(echo "$CHILDREN" | wc -w)
log "Фактически процессов: $COUNT"
if [ "$COUNT" -eq 3 ]; then
    log "Тест 1 пройден."
else
    log "Тест 1 ПРОВАЛЕН (PID: $CHILDREN)"
    kill "$MYINIT_PID" 2>/dev/null || true
    exit 1
fi

log ""
log "--- Тест 2: перезапуск завершённого процесса ---"
log "Ожидаемый результат: после убийства второго потомка снова 3 процесса."
CHILD2=$(echo "$CHILDREN" | awk '{print $2}')
log "Завершаем процесс PID $CHILD2 командой kill..."
kill "$CHILD2"
sleep 2

CHILDREN_NEW=$(pgrep -P "$MYINIT_PID" 2>/dev/null || true)
COUNT_NEW=$(echo "$CHILDREN_NEW" | wc -w)
log "Процессов после перезапуска: $COUNT_NEW"
if [ "$COUNT_NEW" -eq 3 ]; then
    log "Тест 2 пройден."
else
    log "Тест 2 ПРОВАЛЕН."
    kill "$MYINIT_PID" 2>/dev/null || true
    exit 1
fi

log ""
log "--- Тест 3: обработка SIGHUP и уменьшение числа процессов ---"
log "Ожидаемый результат: после SIGHUP остаётся 1 процесс."
cp "$CONFIG2" "$CONFIG1"
kill -HUP "$MYINIT_PID"
sleep 2

CHILDREN_FINAL=$(pgrep -P "$MYINIT_PID" 2>/dev/null || true)
COUNT_FINAL=$(echo "$CHILDREN_FINAL" | wc -w)
log "Процессов после SIGHUP: $COUNT_FINAL"
if [ "$COUNT_FINAL" -eq 1 ]; then
    log "Тест 3 пройден."
else
    log "Тест 3 ПРОВАЛЕН."
    kill "$MYINIT_PID" 2>/dev/null || true
    exit 1
fi

log ""
log "--- Анализ лог-файла ---"
kill "$MYINIT_PID" 2>/dev/null || true
sleep 1

log "Содержимое $LOG:"
cat "$LOG" | tee -a "$RESULT"
log ""

log "Проверка записей в логе..."
errors=0
check_log() {
    if grep -q "$1" "$LOG"; then
        log "  [OK] $2"
    else
        log "  [FAIL] $2"
        errors=$((errors+1))
    fi
}

check_log "Started child PID"        "старт дочерних процессов"
check_log "exited\|killed by signal" "фиксация завершения"
check_log "Respawning child"         "перезапуск упавшего процесса"
check_log "SIGHUP received"          "реакция на SIGHUP"
check_log "All children terminated." "завершение всех процессов по SIGHUP"

log ""
if [ $errors -eq 0 ]; then
    log "Все проверки лога пройдены."
else
    log "Часть проверок не пройдена (ошибок: $errors)."
    exit 1
fi

log ""
log "========================================="
log "Тестирование успешно завершено."
log "========================================="