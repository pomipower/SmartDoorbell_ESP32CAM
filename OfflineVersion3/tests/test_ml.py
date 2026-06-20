import websocket
import time
import io
import sys
from PIL import Image

WS_URL = "ws://192.168.4.1/ws"
IMAGE_PATH = "face.jpg" 

def on_message(ws, message):
    if isinstance(message, bytes):
        print(f"\n[+] Received {len(message)} bytes of binary data from S3!")
        
        # Did it send the 112x112 Cropped Face? (Success)
        if len(message) == 112 * 112 * 3:
            img = Image.frombytes("RGB", (112, 112), message)
            img.save("esp32_vision_SUCCESS.png")
            print("[SUCCESS] Face detected! Saved cropped view as 'esp32_vision_SUCCESS.png'")
            img.show()
            sys.exit(0) # Exit cleanly
            
        # Did it send the FULL 320x240 image? (Failure Fallback)
        elif len(message) == 320 * 240 * 3:
            img = Image.frombytes("RGB", (320, 240), message)
            img.save("esp32_vision_FAILED.png")
            print("[FAILED] No Face Detected. Saved the ESP32's raw memory view as 'esp32_vision_FAILED.png'")
            print("         --> Open this image. If it looks corrupted or rotated, our decoder format is wrong.")
            img.show()
            sys.exit(0) # Exit cleanly
    else:
        print(f"\n[ESP32-S3 RESPONSE] -> {message}\n")

def on_error(ws, error):
    print(f"[Error] -> {error}")

def on_close(ws, close_status_code, close_msg):
    print("--- Connection Closed ---")

def on_open(ws):
    print("--- Connected to ESP32 Brain ---")
    print("1. Sending Enrollment Command for 'Admin'...")
    ws.send("ENROLL:Admin")
    time.sleep(0.5) 
    
    print(f"2. Formatting '{IMAGE_PATH}' for ESP32-S3...")
    try:
        img = Image.open(IMAGE_PATH).convert("RGB")
        img = img.resize((320, 240))
        
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=80)
        jpeg_data = buf.getvalue()

        print(f"3. Transmitting {len(jpeg_data)} bytes...")
        ws.send(jpeg_data, opcode=websocket.ABNF.OPCODE_BINARY)
        
    except FileNotFoundError:
        print(f"\nERROR: Could not find '{IMAGE_PATH}'.")

if __name__ == "__main__":
    ws = websocket.WebSocketApp(WS_URL,
                              on_open=on_open,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)
    ws.run_forever()