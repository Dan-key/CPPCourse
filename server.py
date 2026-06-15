#!/usr/bin/env python3
"""Локальный сервер для интерфейса курса.

Запуск:  python3 server.py [порт]   (по умолчанию 8765)
Открыть: http://127.0.0.1:8765/
"""
import http.server
import socketserver
import sys
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
ROOT = Path(__file__).resolve().parent


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def do_GET(self):
        if self.path == "/":
            self.path = "/web/index.html"
        return super().do_GET()

    def log_message(self, fmt, *args):
        pass  # тише в консоли


def main():
    with socketserver.TCPServer(("127.0.0.1", PORT), Handler) as httpd:
        print(f"Курс доступен на http://127.0.0.1:{PORT}/")
        print("Остановить: Ctrl+C")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nОстановлено.")


if __name__ == "__main__":
    main()
