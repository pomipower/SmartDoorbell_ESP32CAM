from flask import Flask, request
import streamlit as st
import threading
import cv2
import numpy as np
import time
import os

# --- 1. FLASK BACKEND (Receiver) ---
app = Flask(__name__)
LATEST_IMG_PATH = "latest_edge_eval.jpg"

@app.route('/edge_upload', methods=['POST'])
def edge_upload():
    # 1. Receive the Raw JPEG from the ESP32
    raw_data = request.data
    nparr = np.frombuffer(raw_data, np.uint8)
    img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

    # 2. Extract the AI Results computed by the ESP32 from the Headers
    ai_result = request.headers.get("X-AI-Result", "NO_FACE")
    ai_name = request.headers.get("X-AI-Name", "None")
    
    # 3. Draw Bounding Box if Face was detected by the Edge Node
    if ai_result != "NO_FACE":
        x = int(request.headers.get("X-Box-X", 0))
        y = int(request.headers.get("X-Box-Y", 0))
        w = int(request.headers.get("X-Box-W", 0))
        h = int(request.headers.get("X-Box-H", 0))
        
        color = (0, 255, 0) if ai_result in ["SUCCESS", "ENROLLED"] else (0, 0, 255)
        cv2.rectangle(img, (x, y), (x + w, y + h), color, 2)
        cv2.putText(img, f"{ai_result}: {ai_name}", (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

    cv2.imwrite(LATEST_IMG_PATH, img)
    print(f"Received from Edge Node: {ai_result} | {ai_name}")
    return "OK", 200

def run_flask():
    app.run(host='0.0.0.0', port=5050, use_reloader=False)

# --- 2. STREAMLIT FRONTEND (Dashboard) ---
def run_streamlit():
    st.set_page_config(page_title="Edge AI Monitor")
    st.title("🔋 ESP32-CAM Local AI Monitor")
    st.write("This dashboard proves that 100% of the Computer Vision is happening on the microcontroller.")
    
    placeholder = st.empty()
    
    while True:
        if os.path.exists(LATEST_IMG_PATH):
            # Streamlit displays the image the laptop saved
            placeholder.image(LATEST_IMG_PATH, channels="BGR", width="stretch")
        time.sleep(2)

if __name__ == '__main__':
    # Run Flask in the background, Streamlit in the foreground
    threading.Thread(target=run_flask, daemon=True).start()
    run_streamlit()