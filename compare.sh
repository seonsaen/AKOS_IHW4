#!/bin/bash

echo "=== Сравнительный анализ поведения программ ==="
echo ""

echo "1. Компиляция программ..."
gcc -std=c11 -pthread rps.c -o rps
gcc -std=c11 -pthread rps_alt.c -o rps_alt

echo "2. Запуск тестов с одинаковыми начальными условиями..."
echo ""

# Фиксируем seed для воспроизводимости
export SEED=12345

echo "Тест 1: 3 студента"
echo "-------------------"
SEED=12345 ./rps -n 3 -o test1_base.txt
SEED=12345 ./rps_alt -n 3 -o test1_alt.txt
if diff -q test1_base.txt test1_alt.txt > /dev/null; then
    echo "Поведение идентично"
else
    echo "Поведение отличается"
    diff test1_base.txt test1_alt.txt | head -20
fi
echo ""

echo "Тест 2: 5 студентов"
echo "-------------------"
SEED=54321 ./rps -n 5 -o test2_base.txt
SEED=54321 ./rps_alt -n 5 -o test2_alt.txt
if diff -q test2_base.txt test2_alt.txt > /dev/null; then
    echo "Поведение идентично"
else
    echo "Поведение отличается"
    diff test2_base.txt test2_alt.txt | head -20
fi
echo ""

echo "Тест 3: Ввод из файла (4 студента)"
echo "----------------------------------"
echo "4" > config_test.txt
SEED=99999 ./rps -i config1.txt -o test3_base.txt
SEED=99999 ./rps_alt -i config1.txt -o test3_alt.txt
if diff -q test3_base.txt test3_alt.txt > /dev/null; then
    echo "Поведение идентично"
else
    echo "Поведение отличается"
    diff test3_base.txt test3_alt.txt | head -20
fi
echo ""

echo "4. Очистка временных файлов..."
rm -f test*.txt rps rps_alt

echo "=== Тестирование завершено ==="