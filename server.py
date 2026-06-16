#!/usr/bin/env python3
"""
Сервер курса «Трек системного программиста».

Запуск:   python3 server.py [порт]     (по умолчанию 8765)
Открыть:  http://127.0.0.1:8765/

API:
  POST /api/exercises/{module_id}/{exercise_id}/run
  POST /api/lkm/run
  GET  /api/qemu/status
  GET  /api/ai/status
  GET  /api/ai/models
  POST /api/ai/chat          — SSE-стриминг ответа от Ollama
"""

import http.server
import socketserver
import sys
import json
import os
import tempfile
import subprocess
import shutil
import resource
import urllib.request
import urllib.error
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
ROOT = Path(__file__).resolve().parent
EXERCISES_DIR = ROOT / "content" / "exercises"
MODULES_DIR   = ROOT / "content" / "modules"
QEMU_DIR      = ROOT / ".qemu"

OLLAMA_URL    = os.environ.get("OLLAMA_URL", "http://localhost:11434")
DEFAULT_MODEL = os.environ.get("OLLAMA_MODEL", "qwen2.5-coder:7b")

# ---------- Compile flags ----------

COMPILE_FLAGS = [
    "-std=c17", "-Wall", "-Wextra", "-Wconversion", "-Wsign-conversion",
    "-fsanitize=address,undefined", "-O1", "-g"
]

# ---------- Resource limits ----------

def _set_resource_limits():
    try:
        resource.setrlimit(resource.RLIMIT_CPU, (10, 10))
    except Exception:
        pass

# ---------- Exercise runner ----------

def run_exercise(module_id: str, exercise_id: str, user_code: str) -> dict:
    ex_dir = EXERCISES_DIR / module_id / exercise_id
    test_c = ex_dir / "test.c"
    if not test_c.exists():
        return {"error": "not_found", "message": f"{module_id}/{exercise_id} не найдено"}

    with tempfile.TemporaryDirectory(prefix="cppcourse_") as tmp:
        tmp = Path(tmp)
        (tmp / "solution.c").write_text(user_code)
        try:
            comp = subprocess.run(
                ["gcc"] + COMPILE_FLAGS + [
                    str(tmp / "solution.c"), str(test_c), "-o", str(tmp / "prog")
                ],
                capture_output=True, text=True, timeout=30
            )
        except subprocess.TimeoutExpired:
            return {"error": "compile_timeout", "stderr": "Компиляция > 30 сек"}

        if comp.returncode != 0:
            return {"error": "compile", "stderr": comp.stderr,
                    "stdout": comp.stdout, "exit_code": comp.returncode}

        try:
            run = subprocess.run(
                [str(tmp / "prog")],
                capture_output=True, text=True,
                timeout=10, preexec_fn=_set_resource_limits
            )
            return {
                "exit_code": run.returncode,
                "stdout": run.stdout,
                "stderr": run.stderr,
                "passed": run.returncode == 0
            }
        except subprocess.TimeoutExpired:
            return {"error": "timeout", "exit_code": -1,
                    "stdout": "", "stderr": "", "passed": False}

# ---------- LKM runner ----------

def run_lkm(module_id: str, exercise_id: str, user_code: str) -> dict:
    status = check_qemu_status()
    if not status["available"]:
        return {"error": "qemu_unavailable", "message": status["reason"], "passed": False}

    ex_dir = EXERCISES_DIR / module_id / exercise_id
    run_script = ex_dir / "qemu_test.sh"
    if not run_script.exists():
        return {"error": "not_found", "message": "qemu_test.sh не найден"}

    with tempfile.TemporaryDirectory(prefix="cppcourse_lkm_") as tmp:
        tmp = Path(tmp)
        (tmp / "module.c").write_text(user_code)
        try:
            result = subprocess.run(
                [str(ROOT / "scripts" / "qemu-run-lkm.sh"),
                 str(tmp / "module.c"), str(run_script), str(QEMU_DIR)],
                capture_output=True, text=True, timeout=60
            )
            return {
                "exit_code": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
                "passed": result.returncode == 0
            }
        except subprocess.TimeoutExpired:
            return {"error": "timeout", "passed": False}

# ---------- QEMU status ----------

def check_qemu_status() -> dict:
    if not shutil.which("qemu-system-x86_64"):
        return {"available": False,
                "reason": "qemu-system-x86_64 не установлен. Установи: apt install qemu-system-x86"}
    if not (QEMU_DIR / "bzImage").exists():
        return {"available": False,
                "reason": "Ядро QEMU не готово. Запусти: bash scripts/qemu-setup.sh"}
    if not (QEMU_DIR / "initrd.img").exists():
        return {"available": False,
                "reason": "Initrd не создан. Запусти: bash scripts/qemu-setup.sh"}
    return {"available": True, "reason": ""}

# ---------- Ollama helpers ----------

def ollama_request(path: str, payload: dict | None = None, timeout: int = 5):
    """Простой синхронный запрос к Ollama (без потоковой передачи)."""
    url = OLLAMA_URL.rstrip("/") + path
    data = json.dumps(payload).encode() if payload else None
    req = urllib.request.Request(url, data=data,
                                  headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read())
    except Exception:
        return None


def check_ollama_status() -> dict:
    result = ollama_request("/api/tags")
    if result is None:
        return {"available": False,
                "reason": f"Ollama не отвечает на {OLLAMA_URL}. Запусти: ollama serve"}
    models = [m["name"] for m in result.get("models", [])]
    return {"available": True, "models": models, "url": OLLAMA_URL}


def list_ollama_models() -> list[str]:
    result = ollama_request("/api/tags")
    if not result:
        return []
    return [m["name"] for m in result.get("models", [])]


def load_module_text(module_id: str) -> str:
    """Найти .md файл модуля и вернуть его текст (без frontmatter)."""
    # Ищем по паттерну: content/modules/*-{module_id}-*.md или содержащий id
    for f in sorted(MODULES_DIR.glob("*.md")):
        if module_id in f.stem.lower():
            return f.read_text(encoding="utf-8")
    return ""


def build_system_prompt(module_id: str, module_title: str) -> str:
    module_text = load_module_text(module_id)

    # Обрезаем до ~12000 символов — достаточно для большинства моделей (llama3.1:8b имеет 128K ctx)
    MAX_CHARS = 12000
    if len(module_text) > MAX_CHARS:
        module_text = module_text[:MAX_CHARS] + "\n\n[... текст сокращён ...]"

    return f"""Ты — опытный наставник курса «Трек системного программиста» — углублённого курса по C, Linux API и разработке ядра Linux.

Студент сейчас изучает модуль: **{module_title}**

Ниже — полный текст этого модуля:
---
{module_text}
---

Твои правила:
1. Отвечай ТОЛЬКО по материалу этого модуля и смежным системным темам (C, Linux, ядро).
2. Давай технически точные ответы с примерами кода на C, когда уместно.
3. Для заданий из раздела «Практика» — давай ТОЛЬКО подсказки и наводящие вопросы, не полные решения.
4. Отвечай по-русски (если пользователь не пишет на другом языке).
5. Используй код-блоки с указанием языка (```c) для примеров кода.
6. Если вопрос совсем не по теме модуля — мягко верни обратно к теме.
7. Будь лаконичен: не пересказывай весь раздел, отвечай по существу вопроса."""


def stream_ollama_chat(module_id: str, module_title: str, model: str,
                        messages: list, handler_wfile):
    """
    Стримим ответ от Ollama в handler_wfile как SSE.
    messages = [{role, content}, ...] — история чата без системного промпта.
    """
    system_prompt = build_system_prompt(module_id, module_title)

    ollama_messages = [{"role": "system", "content": system_prompt}] + messages

    payload = json.dumps({
        "model": model,
        "messages": ollama_messages,
        "stream": True,
        "options": {
            "num_ctx": 16384,
            "temperature": 0.7,
        }
    }).encode()

    url = OLLAMA_URL.rstrip("/") + "/api/chat"
    req = urllib.request.Request(url, data=payload,
                                  headers={"Content-Type": "application/json"})

    def send_event(data: dict):
        line = "data: " + json.dumps(data, ensure_ascii=False) + "\n\n"
        handler_wfile.write(line.encode())
        handler_wfile.flush()

    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            for raw_line in resp:
                raw_line = raw_line.strip()
                if not raw_line:
                    continue
                try:
                    chunk = json.loads(raw_line)
                except json.JSONDecodeError:
                    continue

                if chunk.get("done"):
                    send_event({"done": True})
                    break

                content = chunk.get("message", {}).get("content", "")
                if content:
                    send_event({"chunk": content})

    except urllib.error.URLError as e:
        send_event({"error": f"Ollama недоступна: {e.reason}"})
    except Exception as e:
        send_event({"error": str(e)})
    finally:
        try:
            handler_wfile.write(b"data: [DONE]\n\n")
            handler_wfile.flush()
        except Exception:
            pass


# ---------- Threaded HTTP server ----------

class ThreadingServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    # ---- GET ----

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/web/index.html")
            self.end_headers()
            return

        if self.path == "/api/qemu/status":
            return self._json(check_qemu_status())

        if self.path == "/api/ai/status":
            return self._json(check_ollama_status())

        if self.path == "/api/ai/models":
            return self._json({"models": list_ollama_models()})

        return super().do_GET()

    # ---- POST ----

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", 0))
            body   = self.rfile.read(length)
            data   = json.loads(body) if body else {}
        except Exception:
            return self._error(400, "Invalid JSON")

        path = self.path

        # /api/exercises/{module}/{exercise}/run
        if path.startswith("/api/exercises/") and path.endswith("/run"):
            parts = path.removeprefix("/api/exercises/").removesuffix("/run").split("/")
            if len(parts) == 2:
                result = run_exercise(parts[0], parts[1], data.get("code", ""))
                return self._json(result)

        # /api/lkm/run
        elif path == "/api/lkm/run":
            result = run_lkm(
                data.get("module_id", ""),
                data.get("exercise_id", ""),
                data.get("code", "")
            )
            return self._json(result)

        # /api/ai/chat  — SSE streaming
        elif path == "/api/ai/chat":
            module_id    = data.get("module_id", "")
            module_title = data.get("module_title", module_id)
            model        = data.get("model", DEFAULT_MODEL)
            messages     = data.get("messages", [])  # [{role, content}]

            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self._cors()
            self.end_headers()

            stream_ollama_chat(module_id, module_title, model, messages, self.wfile)
            return

        self._error(404, "Not found")

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors()
        self.end_headers()

    # ---- helpers ----

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _json(self, data: dict, status: int = 200):
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def _error(self, status: int, msg: str):
        self._json({"error": msg}, status)

    def log_message(self, fmt, *args):
        if args and "/api/" in str(args[0]):
            print(f"[API] {args[0]}")


# ---------- Main ----------

def main():
    has_gcc   = shutil.which("gcc")
    ai_status = check_ollama_status()
    qemu_ok   = check_qemu_status()["available"]

    with ThreadingServer(("127.0.0.1", PORT), Handler) as httpd:
        print(f"Курс:  http://127.0.0.1:{PORT}/")
        print(f"GCC:   {'OK' if has_gcc else 'не найден — установи build-essential'}")
        print(f"QEMU:  {'OK' if qemu_ok else 'не готов (scripts/qemu-setup.sh)'}")
        if ai_status["available"]:
            print(f"Ollama: OK — модели: {', '.join(ai_status['models'])}")
            print(f"Дефолтная модель: {DEFAULT_MODEL}  (OLLAMA_MODEL=<name> чтобы сменить)")
        else:
            print(f"Ollama: {ai_status['reason']}")
        print("Остановить: Ctrl+C")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nОстановлено.")


if __name__ == "__main__":
    main()
