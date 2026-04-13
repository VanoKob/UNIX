#!/bin/bash
set -e

REPORT="result.txt"


echo "ОТЧЁТ О ТЕСТИРОВАНИИ SPARSE-ФАЙЛОВ" > "$REPORT"
echo "Дата: $(date)" >> "$REPORT"
echo "" >> "$REPORT"


make clean >/dev/null 2>&1
make >/dev/null 2>&1 || { echo "Ошибка сборки" >> "$REPORT"; exit 1; }

./create_test_file.sh A

./myprogram A B


gzip -kf A B

gzip -cd B.gz | ./myprogram C

./myprogram -b 100 A D

{
echo "ОПИСАНИЕ ТЕСТОВ:"
echo "1. A->B: создание sparse-файла (блок 4096)"
echo "2. B.gz|C: создание sparse-файла из пайпа"
echo "3. A->D: создание sparse-файла (блок 100)"
echo ""
echo "ОЖИДАЕТСЯ: A - не sparse (много блоков), остальные - sparse (мало блоков)"
echo ""
echo "ФАКТИЧЕСКИЕ РЕЗУЛЬТАТЫ:"
echo "Файл   Размер(байт)  Блоков(512Б)"
echo "--------------------------------"
for f in A A.gz B B.gz C D; do
    [ -f "$f" ] && stat -c "%-6s %11s %6b" "$f"
done
} >> "$REPORT"

cat "$REPORT"
