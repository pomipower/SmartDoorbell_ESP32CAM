import websocket
import threading
import io
from PIL import Image

def on_message(ws, message):
    print(f"\n[PANLEE TOUCH REGISTERED] The ESP32 sent: {message}")

def on_open(ws):
    print("\n[+] Connected to ESP32_ML_BRAIN WebSockets!")
    print("[+] Processing face.jpg...")
    
    try:
        # Open and resize the image to fit the LVGL container exactly
        img = Image.open("face.jpg")
        img = img.resize((320, 240))
        
        # Save the resized image to a virtual memory buffer
        img_buffer = io.BytesIO()
        img.save(img_buffer, format='JPEG', quality=85)
        binary_data = img_buffer.getvalue()
        
        print(f"[+] Sending resized image ({len(binary_data)} bytes)...")
        ws.send(binary_data, websocket.ABNF.OPCODE_BINARY)
        print("[+] Image sent successfully. Check the Panlee screen!")
        print("[?] Tap 'ADMIT' or 'DENY' on the touch screen to test the I2C driver.")
        
    except Exception as e:
        print(f"[-] Error processing image: {e}")

ws = websocket.WebSocketApp("ws://192.168.4.1/ws",
                            on_open=on_open,
                            on_message=on_message)

if __name__ == "__main__":
    print("Looking for ESP32_ML_BRAIN network...")
    ws.run_forever()