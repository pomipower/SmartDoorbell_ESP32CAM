# 🚪 Smart Doorbell System (Edge-to-Cloud IoT)

## 📖 Project Overview

This project is a distributed IoT Smart Doorbell architecture. It utilizes an ESP32-CAM as an edge node for hardware interrupts and image capture, a Python Flask backend for heavy lifting (computer vision and WebSocket orchestration), an indoor ESP32-based TFT display for real-time visitor monitoring, and a Streamlit dashboard for administrative telemetry and access control.

The system features real-time facial recognition using 128-dimensional embeddings, secure relay latch control, and sub-second bi-directional communication via WebSockets.

## 🔌 Hardware Setup (Brief)

### 1\. Outdoor Unit (ESP32-CAM)

- **Camera:** OV2640 / OV3360 attached via ribbon cable.
- **Doorbell Button:** Connect one leg to **GPIO 13** and the other to **GND**.
- **Door Latch Relay:** Connect the Signal pin to **GPIO 14**, VCC to **5V**, and GND to **GND**. _(Note: Uses an Active-Low JQC3F-05VDC-C module)._
- **Power:** 5V via the MB Download module.

### 2\. Indoor Monitor (ESP32 Display Module)

- _e.g., Waveshare ESP32-S3 Touch / CYD (Cheap Yellow Display)_
- **Power:** 5V via USB-C.
- **Interfacing:** Operates entirely over Wi-Fi. No physical connections to the outdoor unit are required.

## 💻 Software Setup & Run Instructions

### Phase 1: Environment Preparation (PC/Server)

- Ensure Python 3.8+ is installed on your machine.
- Clone this repository and navigate to the root directory.
- Install the required Python dependencies:  
   pip install -r requirements.txt

- Create a folder named known_faces in the root directory.
- Place clear, well-lit photos of authorized users in the known_faces directory. Name the files exactly as you want them to appear (e.g., John.jpg, Sarah.png).

### Phase 2: Firmware Flashing (ESP32 Nodes)

- Open the Arduino IDE.
- Install the following libraries via the Library Manager:
  - ArduinoJson by Benoit Blanchon
  - WebSockets by Markus Sattler
  - TFT*eSPI by Bodmer *(Configure User*Setup.h in the library folder for your specific display)*
  - TJpg_Decoder by Bodmer
- Open Doorbell_Outdoor_Cam.ino and Doorbell_Indoor_Display.ino.
- Update the Wi-Fi credentials (ssid, password) in both files.
- Update the serverName, websocket_server, and image_url variables with the **IPv4 Address of your PC running the Flask server**.
- Flash the respective codes to your ESP32-CAM and ESP32 Display Module.

### Phase 3: System Execution

To bring the system online, the boot sequence is critical. Open two separate terminal windows.

**Terminal 1 (Backend Server):**

python doorbell_server.py

_Wait for the "Smart Doorbell Server Active on Port 5050..." message._

**Terminal 2 (Admin Dashboard):**

streamlit run doorbell_dashboard.py

_Your browser will automatically open the admin panel._

**Hardware Boot:**

- Power up the Indoor Display Module. Wait for the "System Armed" message indicating a successful WebSocket connection to your server.
- Power up the ESP32-CAM.
- Press the physical button to trigger the facial recognition pipeline!