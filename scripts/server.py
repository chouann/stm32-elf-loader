#!/usr/bin/env python3
"""Flask bridge between the browser App Store UI and the board's wifi serve.

Usage:
    uv run server.py --board-host <BOARD_IP>

Then open http://localhost:5000 in a browser.
"""

import argparse
import os
import re
import socket
import subprocess
import tempfile
import threading
import time

from flask import Flask, jsonify, request, send_from_directory

CHUNK_SIZE = 64
MAX_FILE_SIZE = 16 * 1024
BOARD_PORT = 8080

board_host: str = ""
board_lock = threading.Lock()

SAFE_OBJ_RE = re.compile(r"^[A-Za-z0-9_-]+\.o$")
SAFE_SRC_RE = re.compile(r"^[A-Za-z0-9_-]+\.c$")

app = Flask(__name__, static_folder=None)


# ---- board communication -------------------------------------------


def board_command(cmd: str, timeout: float = 5.0) -> str:
    with board_lock:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(timeout)
            s.connect((board_host, BOARD_PORT))
            s.sendall((cmd + "\n").encode())
            buf = ""
            while True:
                try:
                    data = s.recv(1024)
                    if not data:
                        break
                    buf += data.decode(errors="replace")
                    if "END\n" in buf:
                        break
                    for line in buf.splitlines():
                        if line.startswith("OK") or line.startswith("ERR"):
                            return buf
                except socket.timeout:
                    break
            return buf


def _wait_ack(sock: socket.socket) -> str | None:
    """Wait for ACK or early ERR from the board. Returns None on ACK, error string otherwise."""
    buf = b""
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            buf += sock.recv(256)
        except socket.timeout:
            break
        if b"ACK" in buf:
            return None
        if b"ERR" in buf:
            return buf.decode(errors="replace").strip()
    return "ERR timeout waiting for ACK"


MAX_UPLOAD_RETRIES = 2


def _board_upload_once(filepath: str, remote_name: str) -> str:
    filesize = os.path.getsize(filepath)
    with open(filepath, "rb") as f:
        payload = f.read()

    header = f"FNAME={remote_name}, FSIZE={filesize}\n"

    with board_lock:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(10)
            s.connect((board_host, BOARD_PORT))
            s.sendall(header.encode())

            err = _wait_ack(s)
            if err:
                return err

            offset = 0
            while offset < filesize:
                end = min(offset + CHUNK_SIZE, filesize)
                s.sendall(payload[offset:end])

                err = _wait_ack(s)
                if err:
                    return err
                offset = end

    return "OK"


def board_upload(filepath: str, remote_name: str | None = None) -> str:
    basename = remote_name or os.path.basename(filepath)
    filesize = os.path.getsize(filepath)
    if filesize == 0:
        return "ERR empty file"
    last_err = ""
    for attempt in range(MAX_UPLOAD_RETRIES + 1):
        try:
            result = _board_upload_once(filepath, basename)
        except OSError as e:
            result = f"ERR socket: {e}"
        if result == "OK":
            return "OK"
        last_err = result
        if attempt < MAX_UPLOAD_RETRIES:
            time.sleep(0.5)
    return last_err


# ---- API routes ----------------------------------------------------


@app.route("/")
def index():
    return send_from_directory(os.path.dirname(__file__), "index.html")


@app.route("/api/apps")
def api_apps():
    resp = board_command("LIST")
    apps = []
    for line in resp.strip().splitlines():
        if line == "END":
            break
        parts = line.split()
        if len(parts) >= 3:
            apps.append(
                {
                    "name": parts[0],
                    "size": int(parts[1]),
                    "source": parts[2],
                }
            )
    return jsonify(apps)


@app.route("/api/ps")
def api_ps():
    resp = board_command("PS")
    tasks = []
    for line in resp.strip().splitlines():
        if line == "END":
            break
        parts = line.split()
        if len(parts) >= 4:
            tasks.append(
                {
                    "id": int(parts[0]),
                    "name": parts[1],
                    "memory": int(parts[2]),
                    "stack": parts[3],
                }
            )
    return jsonify(tasks)


@app.route("/api/run", methods=["POST"])
def api_run():
    body = request.get_json(silent=True) or {}
    name = body.get("name", "")
    if not name or not SAFE_OBJ_RE.match(name):
        return jsonify({"error": "missing or invalid name"}), 400
    resp = board_command(f"RUN {name}").strip()
    if resp.startswith("OK"):
        parts = resp.split()
        task_id = int(parts[1]) if len(parts) > 1 else -1
        return jsonify({"ok": True, "task_id": task_id})
    return jsonify({"ok": False, "error": resp}), 500


@app.route("/api/kill", methods=["POST"])
def api_kill():
    body = request.get_json(silent=True) or {}
    task_id = body.get("id")
    if task_id is None or not isinstance(task_id, int):
        return jsonify({"error": "missing or invalid id"}), 400
    if task_id < 0 or task_id > 3:
        return jsonify({"error": "id out of range"}), 400
    resp = board_command(f"KILL {task_id}").strip()
    return jsonify({"ok": resp.startswith("OK")})


@app.route("/api/rm", methods=["POST"])
def api_rm():
    body = request.get_json(silent=True) or {}
    name = body.get("name", "")
    if not name or not SAFE_OBJ_RE.match(name):
        return jsonify({"error": "missing or invalid name"}), 400
    resp = board_command(f"RM {name}").strip()
    ok = resp.startswith("OK")
    return jsonify({"ok": ok, "error": None if ok else resp})


@app.route("/api/upload", methods=["POST"])
def api_upload():
    f = request.files.get("file")
    if not f or not f.filename:
        return jsonify({"error": "no file"}), 400
    basename = os.path.basename(f.filename)
    if not SAFE_OBJ_RE.match(basename):
        return jsonify({"error": "must be a valid .o filename"}), 400

    tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".o", dir="/tmp")
    try:
        f.save(tmp)
        tmp.close()
        size = os.path.getsize(tmp.name)
        if size > MAX_FILE_SIZE:
            return jsonify({"error": f"too large ({size})"}), 400
        resp = board_upload(tmp.name, remote_name=basename)
    finally:
        os.unlink(tmp.name)

    if resp.startswith("OK"):
        return jsonify({"ok": True})
    return jsonify({"ok": False, "error": resp}), 500


@app.route("/api/deploy", methods=["POST"])
def api_deploy():
    """Build apps/ and upload a specific .o to the board."""
    body = request.get_json(silent=True) or {}
    name = str(body.get("name", "")).strip()
    if not name or not name.endswith(".c"):
        return jsonify({"error": "name must end with .c"}), 400
    if not SAFE_SRC_RE.match(name):
        return jsonify({"error": "invalid filename"}), 400
    target = name.removesuffix(".c") + ".o"

    apps_dir = os.path.join(os.path.dirname(__file__), "..", "apps")
    apps_dir = os.path.abspath(apps_dir)

    result = subprocess.run(
        ["make", "-j4", target],
        cwd=apps_dir,
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode != 0:
        return jsonify({"ok": False, "error": f"build failed:\n{result.stderr}"}), 500

    o_path = os.path.join(apps_dir, target)
    if not os.path.isfile(o_path):
        return jsonify({"ok": False, "error": f"{target} not found after build"}), 500

    resp = board_upload(o_path)
    if resp.startswith("OK"):
        return jsonify({"ok": True})
    return jsonify({"ok": False, "error": resp}), 500


# ---- main ----------------------------------------------------------


def main():
    global board_host
    parser = argparse.ArgumentParser(description="App Store bridge server")
    parser.add_argument("--board-host", required=True, help="Board IP address")
    parser.add_argument("--port", type=int, default=5000, help="Flask listen port")
    parser.add_argument("--listen-host", default="127.0.0.1", help="Flask bind address (default localhost only)")
    args = parser.parse_args()
    board_host = args.board_host

    print(f"Board: {board_host}:{BOARD_PORT}")
    print(f"Open http://{args.listen_host}:{args.port}")
    app.run(host=args.listen_host, port=args.port, debug=False)


if __name__ == "__main__":
    main()
