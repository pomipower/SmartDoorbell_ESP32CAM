# Smart Doorbell V2.2 - Dual ESP32 System with Facial Recognition

A high-performance, dual-microcontroller smart doorbell system. This project features a real-time 10 FPS JPEG video stream over WebSockets, hardware-accelerated JPEG decoding on an internal display, and a FastAPI Python backend for asynchronous facial recognition.

## System Architecture

* **Outdoor Unit (`camera_idf`):** ESP32-CAM running an ESP-IDF v5.2 firmware. Captures video and streams binary JPEG frames directly to the backend via WebSockets.
* **Backend Server (`server`):** FastAPI Python application. Acts as the WebSocket router, performs facial recognition using HOG models (`face_recognition`), and maintains the state of the door.
* **Admin Dashboard (`dashboard`):** Streamlit web interface for monitoring the live feed, reviewing security logs, and manually resolving "Unknown Visitor" events.
* **Indoor Display (`Doorbell_Indoor_Display_RGB_Video`):** PANLEE BC02 (WT32-S3-WROVER-N16R8) running Arduino IDE C++. Features a FreeRTOS dual-core Ping-Pong buffer to simultaneously download network frames on Core 1 and decode JPEGs to the 480x480 RGB display on Core 0 without blocking.
  * *(Note: The `display_idf` folder contains experimental ESP-IDF driver logic and is kept for documentation purposes).*

## Hardware Requirements
* ESP32-CAM module (with OV3660 camera)
* PANLEE BC02 ZX3D95CE01S-TR-V12 (4.0" 480x480 IPS Display with GC9503V driver)
* PC/Server running Python 3.10+

## Setup & Build Guide

### 1. Backend Server & Dashboard
Ensure you have Python installed, then install the dependencies:
```bash
pip install -r requirements.txt
```

**Backend Server**
Run the FastAPI backend with keepalive pings disabled to prevent streaming timeouts:
```bash
uvicorn doorbell_server_fastapi:app --host 0.0.0.0 --port 5050 --ws-ping-interval 0 --ws-ping-timeout 0
```

**Admin Dashboard**
In a separate terminal, launch the Streamlit admin panel:
```bash
streamlit run doorbell_dashboard_fastapi.py
```

### 2. Outdoor Camera Firmware (ESP-IDF)
Navigate to the `camera_idf` directory and build using the Espressif IoT Development Framework (v5.2+):
```bash
cd camera_idf
idf.py set-target esp32
idf.py build
idf.py -p <COM_PORT> flash monitor
```

### 3. Indoor Display Firmware (Arduino)
Open the .ino file inside Doorbell_Indoor_Display_RGB_Video using Arduino IDE 2.x.

**Libraries required:**
* `LovyanGFX` by Lovyan
* `PanelLan` by Lovyan
* `TFT_eSPI` by Bodmer
* `WebSockets` by Markus Sattler

**Board Settings (ESP32S3 Dev Module):**
* Flash Size: 16MB (128Mb)
* PSRAM: OPI PSRAM (Required for dual-buffer video streaming)
* Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
* Flash Mode: QIO 80MHz

Compile and upload via the UART programmer.

