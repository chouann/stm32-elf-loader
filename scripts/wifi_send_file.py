#!/usr/bin/env python3
import argparse
import os
import socket
import sys
import time

CHUNK_SIZE = 64
MAX_FILE_SIZE = 16 * 1024  # must match WIFI_MAX_FILE_SIZE on the board


def wait_ack(sock, timeout=10):
    """Accumulate received data until ACK is seen or timeout."""
    sock.settimeout(timeout)
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            data = sock.recv(64)
        except socket.timeout:
            return False
        if not data:
            return False
        buf += data
        if b"ACK" in buf:
            return True
    return False


def send_file(host, port, filepath):
    if not os.path.exists(filepath):
        print(f"[Python] Error: '{filepath}' does not exist.")
        return 1
    if not filepath.endswith(".o"):
        print("[Python] Error: only .o files are accepted by the board.")
        return 1

    basename = os.path.basename(filepath)
    filesize = os.path.getsize(filepath)
    if filesize == 0 or filesize > MAX_FILE_SIZE:
        print(f"[Python] Error: file size {filesize} exceeds board limit ({MAX_FILE_SIZE}).")
        return 1
    total_chunks = (filesize + CHUNK_SIZE - 1) // CHUNK_SIZE
    print(f"[Python] Sending '{basename}' ({filesize} bytes, {total_chunks} chunks)")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
        print("[Python] Connected.")
        time.sleep(3)

        header = f"FNAME={basename}, FSIZE={filesize}\n"
        sock.sendall(header.encode("utf-8"))
        print(f"[Python] Header sent: {header.strip()}")

        if not wait_ack(sock):
            print("[Python] ERROR: No ACK for header")
            return 1

        sent_chunks = 0
        with open(filepath, "rb") as f:
            while True:
                chunk = f.read(CHUNK_SIZE)
                if not chunk:
                    break
                sock.sendall(chunk)
                sent_chunks += 1

                if not wait_ack(sock):
                    print(f"[Python] ERROR: No ACK at chunk {sent_chunks}")
                    return 1

                print(f"\r[Python] {sent_chunks}/{total_chunks} chunks", end="")

        print(f"\n[Python] Done. Sent {filesize} bytes.")
        return 0

    except Exception as e:
        print(f"[Python] ERROR: {e}")
        return 1
    finally:
        sock.close()
        print("[Python] Socket closed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Send a .o file to STM32 over WiFi")
    parser.add_argument("file", help="path to the file to send")
    parser.add_argument("--host", required=True, help="STM32 IP (from AT+CIFSR)")
    parser.add_argument("--port", type=int, default=8080, help="TCP port (default 8080)")
    args = parser.parse_args()
    sys.exit(send_file(args.host, args.port, args.file))
