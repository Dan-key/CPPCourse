#!/usr/bin/env python3
"""
io-playground — симулятор сетевого пира для модуля C2 (epoll / io_uring).

Запускаешь свой epoll/io_uring-сервер или клиент и натравливаешь на него этот
инструмент, чтобы СПРОВОЦИРОВАТЬ конкретные сценарии сисколлов (EAGAIN,
частичные записи, медленные клиенты, обрывы, C10k-флуд) и посмотреть их через
strace.

Зависимостей нет — только стандартная библиотека Python 3.

Режимы СЕРВЕРА (слушают порт, твой код подключается к ним):
  echo   --port P                  принимает соединения и возвращает данные назад
  sink   --port P                  читает и выбрасывает (проверка твоей записи)
  source --port P --bytes N        льёт N байт каждому клиенту (проверка чтения/ET-дренажа)

Режимы КЛИЕНТА (подключаются к твоему серверу host:port):
  slow   --connect H:P --rate R --delay D
                                   читает медленно (R байт раз в D сек) → твой write упрётся в EAGAIN
  flood  --connect H:P --conns N   открывает N одновременных соединений (стресс C10k / accept-цикла)
  flaky  --connect H:P --conns N   шлёт по 1 байту и рвёт соединения в случайные моменты
                                   (проверка обработки EOF / ECONNRESET / частичных чтений)

Примеры:
  # Терминал 1: твой сервер под трассировкой сисколлов
  strace -f -e trace=network,epoll_wait,epoll_ctl,read,write,accept4,close ./my_server
  # Терминал 2: нагрузка
  python3 scripts/io-playground.py flood --connect 127.0.0.1:8080 --conns 2000
  # Сводка сисколлов:
  strace -c -f ./my_server
"""

import argparse
import os
import random
import socket
import sys
import threading
import time


# ---------- helpers ----------

def parse_hostport(s: str):
    host, _, port = s.rpartition(":")
    if not host or not port:
        sys.exit(f"ожидался формат host:port, получено {s!r}")
    return host, int(port)


def make_listener(port: int) -> socket.socket:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(1024)
    print(f"[io-playground] слушаю 127.0.0.1:{port}", flush=True)
    return s


# ---------- server modes ----------

def serve_echo(port: int):
    ls = make_listener(port)
    while True:
        c, addr = ls.accept()
        threading.Thread(target=_echo_one, args=(c, addr), daemon=True).start()


def _echo_one(c: socket.socket, addr):
    total = 0
    try:
        while True:
            data = c.recv(65536)
            if not data:
                break
            c.sendall(data)
            total += len(data)
    except OSError:
        pass
    finally:
        c.close()
        print(f"[echo] {addr} закрыт, отражено {total} Б", flush=True)


def serve_sink(port: int):
    ls = make_listener(port)
    while True:
        c, addr = ls.accept()
        threading.Thread(target=_sink_one, args=(c, addr), daemon=True).start()


def _sink_one(c: socket.socket, addr):
    total = 0
    try:
        while True:
            data = c.recv(65536)
            if not data:
                break
            total += len(data)
    except OSError:
        pass
    finally:
        c.close()
        print(f"[sink] {addr} закрыт, поглощено {total} Б", flush=True)


def serve_source(port: int, nbytes: int):
    ls = make_listener(port)
    while True:
        c, addr = ls.accept()
        threading.Thread(target=_source_one, args=(c, addr, nbytes), daemon=True).start()


def _source_one(c: socket.socket, addr, nbytes: int):
    chunk = b"x" * 65536
    sent = 0
    try:
        while sent < nbytes:
            n = c.send(chunk[: min(len(chunk), nbytes - sent)])
            sent += n
    except OSError:
        pass
    finally:
        c.close()
        print(f"[source] {addr}: отправлено {sent} Б", flush=True)


# ---------- client modes ----------

def client_slow(host: str, port: int, rate: int, delay: float):
    c = socket.create_connection((host, port))
    print(f"[slow] подключился к {host}:{port}; читаю по {rate} Б раз в {delay} с", flush=True)
    total = 0
    try:
        while True:
            data = c.recv(rate)
            if not data:
                break
            total += len(data)
            time.sleep(delay)   # медленный читатель → у сервера write упрётся в EAGAIN
    except OSError:
        pass
    finally:
        c.close()
        print(f"[slow] закрыт, прочитано {total} Б", flush=True)


def client_flood(host: str, port: int, conns: int):
    socks = []
    ok = 0
    for _ in range(conns):
        try:
            s = socket.create_connection((host, port), timeout=5)
            s.sendall(b"hello\n")
            socks.append(s)
            ok += 1
        except OSError as e:
            print(f"[flood] соединение не удалось: {e}", flush=True)
            break
    print(f"[flood] открыто {ok}/{conns} одновременных соединений", flush=True)
    time.sleep(2)               # подержать открытыми, чтобы увидеть на сервере
    for s in socks:
        s.close()
    print("[flood] все закрыты", flush=True)


def client_flaky(host: str, port: int, conns: int):
    threads = []
    for i in range(conns):
        t = threading.Thread(target=_flaky_one, args=(host, port, i), daemon=True)
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    print("[flaky] готово", flush=True)


def _flaky_one(host: str, port: int, idx: int):
    try:
        c = socket.create_connection((host, port), timeout=5)
        # шлём по 1 байту с паузами, рвём в случайный момент
        for _ in range(random.randint(1, 20)):
            c.send(bytes([random.randint(32, 126)]))
            time.sleep(random.uniform(0, 0.02))
        # жёсткий обрыв (RST) с некоторой вероятностью — спровоцировать ECONNRESET
        if random.random() < 0.5:
            c.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                         b"\x01\x00\x00\x00\x00\x00\x00\x00")  # linger=on, timeout=0 → RST
        c.close()
    except OSError:
        pass


# ---------- main ----------

def main():
    p = argparse.ArgumentParser(description="io-playground — симулятор пира для C2")
    p.add_argument("mode", choices=["echo", "sink", "source", "slow", "flood", "flaky"])
    p.add_argument("--port", type=int, default=9000, help="порт для серверных режимов")
    p.add_argument("--connect", type=str, help="host:port для клиентских режимов")
    p.add_argument("--bytes", type=int, default=10_000_000, help="source: сколько лить")
    p.add_argument("--rate", type=int, default=64, help="slow: байт за чтение")
    p.add_argument("--delay", type=float, default=0.2, help="slow: пауза между чтениями, с")
    p.add_argument("--conns", type=int, default=1000, help="flood/flaky: число соединений")
    args = p.parse_args()

    try:
        if args.mode == "echo":
            serve_echo(args.port)
        elif args.mode == "sink":
            serve_sink(args.port)
        elif args.mode == "source":
            serve_source(args.port, args.bytes)
        else:
            if not args.connect:
                sys.exit("клиентскому режиму нужен --connect host:port")
            host, port = parse_hostport(args.connect)
            if args.mode == "slow":
                client_slow(host, port, args.rate, args.delay)
            elif args.mode == "flood":
                client_flood(host, port, args.conns)
            elif args.mode == "flaky":
                client_flaky(host, port, args.conns)
    except KeyboardInterrupt:
        print("\n[io-playground] остановлен", flush=True)


if __name__ == "__main__":
    main()
