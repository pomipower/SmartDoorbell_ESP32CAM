# **OfflineVersion3: Distributed Edge-AI Smart Doorbell**

A completely offline, high-speed, dual-node Facial Recognition Smart Doorbell system. Built using Espressif's ESP-IDF v5.1.4 and the esp-dl neural network framework, this architecture achieves end-to-end edge inference and actuation in **\~600 milliseconds** without any cloud connectivity.

## **🧠 System Architecture**

The system utilizes a distributed edge-computing architecture to balance memory and processing loads:

1. **The Brain (ESP32-S3):** Acts as the central hub. It broadcasts a WPA2 Wi-Fi SoftAP, hosts an asynchronous WebSocket server, serves a completely embedded HTML/JS Web Dashboard, and executes the heavy SIMD vector mathematics for the neural networks.  
2. **The Eye (ESP32-CAM):** Acts as a low-power, high-speed "dumb client". It idles until a physical interrupt occurs, flushes the camera buffer, captures a perfectly exposed QVGA JPEG, blasts it over the WebSocket, and waits for a relay actuation command.

### **⚙️ Technical Specifications**

**Hardware Requirements:**

* **Server Node:** ESP32-S3 (Must have Octal PSRAM for tensor allocations)  
* **Client Node:** AI-Thinker ESP32-CAM (Original ESP32, 4MB PSRAM)  
* **Camera Module:** OV3660 (or OV2640)  
* **Actuator:** JQC3F-05VDC-C (5V Relay Module)  
* **Input:** Tactile Push Button (Wired to GND, Active-Low)  
* **Power:** Dedicated 5V, 2A power supply (USB power is insufficient and will cause brownouts during Wi-Fi \+ Flash LED spikes).

**Software & Frameworks:**

* **Framework:** ESP-IDF v5.1.4  
* **ML Framework:** espressif/esp-dl v1.1.0  
* **Camera Driver:** espressif/esp32-camera ^2.0.0  
* **Face Detection Model:** HumanFaceDetectMSR01  
* **Face Recognition Model:** FaceRecognition112V1S8 (8-bit Quantized MobileFaceNet)

## **🚀 Features**

* **Ultra-Low Latency:** Image capture to relay actuation takes \< 650ms.  
* **Native Web Dashboard:** A responsive HTML/JS UI hosted in the S3's ROM. Features Enrollment Mode, Security Mode, Live Feed Viewer, and an Admin Panel.  
* **SPIFFS Database:** Persistent storage for 128-D embedding vectors and captured JPEG visitor logs.  
* **CSV Log Export:** Downloadable visitor logs tracking timestamp, event type, and recognized identities.  
* **Open-Drain Relay Logic:** Safely interfaces the 3.3V ESP32 GPIO with a 5V mechanical relay coil.

## **🛠️ Setup & Build Guide**

### **1\. The ML Brain (ESP32-S3)**

1. Navigate to the Brain firmware directory:  
   cd final\_firmware/01\_ml\_server\_brain

2. Set the target to ESP32-S3:  
   idf.py set-target esp32s3

3. Open menuconfig:  
   idf.py menuconfig

   * Navigate to Component config \-\> HTTP Server and increase **Max HTTP Request Header Length** to 2048 (to handle modern browser WebSocket handshakes).  
4. Build and flash:  
   idf.py build flash monitor

### **2\. The CAM Client (ESP32-CAM)**

1. Navigate to the Client firmware directory:  
   cd final\_firmware/02\_cam\_client

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
5. Press the physical button on the ESP32-CAM to trigger the pipeline\!