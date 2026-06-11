#!/usr/bin/env python3
import argparse
import socket
import sys
import time


def send_msg(host, port, msg):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        print(f"[Python] Connecting to {host}:{port}...")
        sock.connect((host, port))
        time.sleep(3)
        print("[Python] Connected.")

        if msg:
            if not msg.endswith("\n"):
                msg += "\n"
            sock.sendall(msg.encode("utf-8"))
            print(f"[Python] Sent: {msg.strip()}")
        else:
            while True:
                line = input("Message (empty to exit): ")
                if line == "":
                    break
                if not line.endswith("\n"):
                    line += "\n"
                sock.sendall(line.encode("utf-8"))
                print(f"[Python] Sent: {line.strip()}")

        time.sleep(1)
        return 0

    except Exception as e:
        print(f"[Python] ERROR: {e}")
        return 1
    finally:
        sock.close()
        print("[Python] Socket closed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Send messages to STM32 over WiFi")
    parser.add_argument("--host", required=True, help="STM32 IP (from AT+CIFSR)")
    parser.add_argument("--port", type=int, default=8080, help="TCP port (default 8080)")
    parser.add_argument("--msg", help="message to send (interactive mode if omitted)")
    args = parser.parse_args()
    sys.exit(send_msg(args.host, args.port, args.msg))
