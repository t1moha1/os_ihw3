#!/usr/bin/env bash

PORT=8080

lsof -i:8080 -t | xargs -r kill -9

gcc -pthread  server.c  -o server
gcc randclient.c -o randclient
gcc -pthread monitor.c -o monitor


echo "Запускаем сервер на порту $PORT..."
./server $PORT > server.log 2>&1 &
SV=$!
sleep 1


echo "Запускаем монитор..."
./monitor 127.0.0.1 $PORT &
MO=$!


echo "Запускаем randclient (sleep=1)..."
./randclient 127.0.0.1 $PORT 1 > randclient.log 2>&1 &
RC=$!

echo "Работаем 15 секунд..."
sleep 15


echo "Останавливаем процессы..."
kill $RC $MO $SV 2>/dev/null

wait $RC $MO $SV 2>/dev/null

echo "Логи сервера в server.log, весь вывод монитора и randclient выше."