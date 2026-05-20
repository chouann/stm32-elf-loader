#!/usr/bin/env python3
import socket
import time
import sys

# Configure local network parameters
# FIXME: Please change this to the STAIP printed when STM32 executes AT+CIFSR
ESP8266_IP = "172.20.10.12"  # Confirmed from AT+CIFSR output
ESP8266_PORT = 8080

def send_data_to_stm32():
    # 1. Create a TCP Socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    try:
        print(f"[Python] Connecting to STM32 Server at {ESP8266_IP}:{ESP8266_PORT}...")
        # 2. Attempt to handshake with ESP8266
        client_socket.connect((ESP8266_IP, ESP8266_PORT))
        time.sleep(3)
        print("[Python] Connection established successfully!")
        
        while(1):
            message = input("Input the message you want to send to stm32 (Just only press enter to exit):")
            if message == "":
                break
            # 3. Ensure message ends with newline for consistency
            if not message.endswith('\n'):
                message += '\n' 
            
            # Ensure encoding to byte stream (bytes)
            data_to_send = message.encode('utf-8')
            print(f"[Python] Sending data ({len(data_to_send)} bytes): {message.strip()}")
            
            # 4. Physically send out the data
            client_socket.sendall(data_to_send) # blocking
            print(f"[Python] Finish Sending data ({len(data_to_send)} bytes): {message.strip()}")
        
        # Wait slightly to keep the connection open before closing
        time.sleep(1)
        
    except Exception as e:
        print(f"[Python] ERROR occurred: {e}")
        
    finally:
        # 5. Close the channel
        client_socket.close()
        print("[Python] Socket closed.")

if __name__ == "__main__":
    send_data_to_stm32()