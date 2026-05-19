#!/usr/bin/env python3
import socket
import time
import sys
import os
# Configure local network parameters
# FIXME: Please change this to the STAIP printed when STM32 executes AT+CIFSR
ESP8266_IP = "172.20.10.12"  # Confirmed from AT+CIFSR output
ESP8266_PORT = 8080

def send_file_to_stm32(filepath):
    if not filepath or not os.path.exists(filepath):
        print(f"[Python] Error: File '{filepath}' does not exist.")
        return

    # 1. Read file data and get file info
    basename = os.path.basename(filepath)
    filesize = os.path.getsize(filepath)
    
    print(f"[Python] Preparing to send '{basename}' ({filesize} bytes)")

    # 2. Create TCP Socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    try:
        print(f"[Python] Connecting to STM32 Server at {ESP8266_IP}:{ESP8266_PORT}...")
        # 3. Attempt to handshake with ESP8266
        client_socket.connect((ESP8266_IP, ESP8266_PORT))
        print("[Python] Connection established successfully!")
        
        # Wait a moment to ensure ESP8266 is ready
        time.sleep(3)
        
        # 4. Formulate and send the initial file info message
        # Format: FNAME=<filename>,FSIZE=<filesize>
        header_str = f"FNAME={basename},FSIZE={filesize}\n"
        header_bytes = header_str.encode('utf-8')
        
        print(f"[Python] Sending header: {header_str.strip()}")
        client_socket.sendall(header_bytes)
        
        # Give STM32 a moment to parse the header, open the file, etc.
        time.sleep(10)
        
        # 5. Read and send the actual file contents in chunks (binary mode)
        print("[Python] Sending file data in chunks of 32 bytes...")
        bytes_sent = 0
        chunk_size = 32
        with open(filepath, 'rb') as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                client_socket.sendall(chunk)
                bytes_sent += len(chunk)
                time.sleep(0.05)   # Small delay to prevent overwhelming the ESP8266 & STM32 buffers
                
        print(f"[Python] Finish sending file data ({bytes_sent} bytes).")
        
        # Wait slightly to ensure STM32 receives everything before closing
        input("Close socket or not (enter anything):")
        
    except Exception as e:
        print(f"[Python] ERROR occurred: {e}")
        
    finally:
        # 6. Close the channel
        client_socket.close()
        print("[Python] Socket closed.")

if __name__ == "__main__":
    # Accept filename as command line argument
    if len(sys.argv) < 2:
        print("Usage: python3 wifi_send_file.py <filename>")
        sys.exit(1)
        
    target_file = sys.argv[1]
    send_file_to_stm32(target_file)