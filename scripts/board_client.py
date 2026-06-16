#!/usr/bin/env python3
"""Minimal client to test the board's wifi serve command channel.

Usage (board must be in 'wifi serve' mode):
    uv run board_client.py --host <BOARD_IP> LIST
    uv run board_client.py --host <BOARD_IP> PS
    uv run board_client.py --host <BOARD_IP> "RUN blink_green.o"
    uv run board_client.py --host <BOARD_IP> "KILL 0"
    uv run board_client.py --host <BOARD_IP> PUT path/to/app.o
"""

import argparse
import os
import socket
import sys
import time

CHUNK_SIZE = 64
MAX_FILE_SIZE = 16 * 1024


def send_command(host: str, port: int, cmd: str) -> str:
    """Send a text command, read the full response, return it.

    Response is considered complete when it contains a known terminator:
    'END\\n' for LIST/PS, or a line starting with 'OK' or 'ERR' for others.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect((host, port))
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
    """Wait for ACK or early ERR. Returns None on ACK, error string otherwise."""
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
    return "timeout waiting for ACK"


def send_file(host: str, port: int, filepath: str) -> bool:
    """Upload a .o file using the ACK flow-control protocol."""
    basename = os.path.basename(filepath)
    filesize = os.path.getsize(filepath)
    if filesize == 0:
        print("File is empty")
        return False
    if filesize > MAX_FILE_SIZE:
        print(f"File too large: {filesize} > {MAX_FILE_SIZE}")
        return False

    with open(filepath, "rb") as f:
        payload = f.read()

    header = f"FNAME={basename}, FSIZE={filesize}\n"

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(10)
        s.connect((host, port))
        s.sendall(header.encode())

        err = _wait_ack(s)
        if err:
            print(f"Header: {err}")
            return False
        print(f"Header ACK received, sending {filesize} bytes...")

        offset = 0
        while offset < filesize:
            end = min(offset + CHUNK_SIZE, filesize)
            s.sendall(payload[offset:end])

            err = _wait_ack(s)
            if err:
                print(f"Chunk at {offset}: {err}")
                return False

            offset = end
            pct = offset * 100 // filesize
            print(f"\r  {offset}/{filesize} ({pct}%)", end="", flush=True)

        print("\nUpload complete.")
        return True


def main():
    parser = argparse.ArgumentParser(description="Board serve client")
    parser.add_argument("--host", required=True, help="Board IP address")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("command", help="LIST | PS | RUN <name> | KILL <id> | PUT <file>")
    parser.add_argument("extra", nargs="?", default=None)
    args = parser.parse_args()

    cmd = args.command.strip()

    if cmd == "PUT":
        filepath = args.extra
        if not filepath:
            print("PUT requires a file path")
            sys.exit(1)
        ok = send_file(args.host, args.port, filepath)
        sys.exit(0 if ok else 1)
    else:
        full_cmd = cmd if not args.extra else f"{cmd} {args.extra}"
        resp = send_command(args.host, args.port, full_cmd)
        print(resp, end="" if resp.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
