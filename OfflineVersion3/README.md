# **OfflineVersion3: Distributed Edge-AI Smart Doorbell**

A completely offline, high-speed, dual-node Facial Recognition Smart Doorbell system. Built using Espressif's ESP-IDF v5.1.4 and the esp-dl neural network framework, this architecture achieves end-to-end edge inference and actuation in **\~600 milliseconds** without any cloud connectivity, and supports **Real-Time Live Video Streaming** at 10+ FPS.

## **🧠 System Architecture**

The system utilizes a distributed edge-computing architecture to balance memory and processing loads:

1. **The Brain (ESP32-S3):** Acts as the central hub. It broadcasts a WPA2 Wi-Fi SoftAP, hosts an asynchronous WebSocket server, serves a completely embedded HTML/JS Web Dashboard, and executes the heavy SIMD vector mathematics for the neural networks. It also features a real-time LVGL capacitive touch UI driven by a specialized Triple-Buffer PSRAM architecture.  
2. **The Eye (ESP32-CAM):** Acts as a low-power, high-speed sensor node. It idles until a physical interrupt occurs, double-flushes the camera ring buffer to guarantee real-time capture, and blasts perfectly exposed QVGA JPEGs over the WebSocket. It also features a dedicated FreeRTOS streaming task for live video calls.

### **⚙️ Technical Specifications**

**Hardware Requirements:**

* **Server Node:** ESP32-S3 (Must have Octal PSRAM for tensor allocations & triple buffers)  
* **Client Node:** AI-Thinker ESP32-CAM (Original ESP32, 4MB PSRAM)  
* **Camera Module:** OV3660 (or OV2640)  
* **Actuator:** JQC3F-05VDC-C (5V Relay Module)  
* **Input:** Tactile Push Button (Wired to GPIO 14 to GND, Active-Low)  
* **Power:** Dedicated 5V, 2A power supply (USB power is insufficient and will cause brownouts during Wi-Fi \+ Flash LED spikes).

**Software & Frameworks:**

* **Framework:** ESP-IDF v5.1.4  
* **ML Framework:** espressif/esp-dl v1.1.0  
* **Camera Driver:** espressif/esp32-camera ^2.0.0  
* **Face Detection Model:** HumanFaceDetectMSR01  
* **Face Recognition Model:** FaceRecognition112V1S8 (8-bit Quantized MobileFaceNet)

## **🚀 Key Features**

* **Ultra-Low Latency Inference:** Image capture to relay actuation takes \< 650ms.  
* **Live Video Streaming:** Tap the "Live Video" button on the display to initiate a high-speed video call. The neural network is intelligently throttled to 1 inference per second during streams to preserve PSRAM bandwidth.  
* **Triple-Buffered Memory:** Completely eradicates screen tearing and JPEG decode corruption by utilizing a 300KB circular PSRAM buffer, safely decoupling the network payload writes from the LVGL UI rendering.  
* **Native Web Dashboard:** A responsive HTML/JS UI hosted in the S3's ROM. Features Enrollment Mode, Security Mode, Live Feed Viewer, and an Admin Panel.  
* **SPIFFS Database:** Persistent storage for 128-D embedding vectors and captured JPEG visitor logs.  
* **Open-Drain Relay Logic:** Safely interfaces the 3.3V ESP32 GPIO with a 5V mechanical relay coil, preventing diode leakage.

## **🛠️ Setup & Build Guide**

### **1\. The ML Brain (ESP32-S3)**

1. Navigate to the Brain V3 firmware directory:  
   cd final\_firmware/05\_ml\_server\_brain\_v3

2. Set the target to ESP32-S3:  
   idf.py set-target esp32s3

3. Open menuconfig:  
   idf.py menuconfig

   * Navigate to Component config \-\> HTTP Server and increase **Max HTTP Request Header Length** to 2048 (to handle modern browser WebSocket handshakes).  
4. Build and flash:  
   idf.py build flash monitor

### **2\. The CAM Client (ESP32-CAM)**

1. Navigate to the Client V3 firmware directory:  
   cd final\_firmware/04\_cam\_client\_v3

2. Set the target to the original ESP32:  
   idf.py set-target esp32

3. *Note: The sdkconfig.defaults and partitions.csv automatically configure the required 2MB app partition and PSRAM initialization.*  
4. Build and flash:  
   idf.py build flash monitor

## **🎮 Usage**

1. Power on both devices. The S3 will broadcast the ESP32\_ML\_BRAIN network.  
2. Connect a laptop/smartphone to the Wi-Fi network (Password: 12345678).  
3. Open a browser and navigate to http://192.168.4.1/.  
4. Use the **Dashboard** to toggle between Enrollment and Security modes.  
5. Press the physical button on the ESP32-CAM (GPIO 14\) to trigger a snapshot pipeline, or press the **"Live Video"** button on the physical display to start a video stream\!