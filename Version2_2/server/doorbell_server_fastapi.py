"""
Smart Doorbell — FastAPI Server (Stable QVGA Edition)
=====================================================
"""

import os
import csv
import time
import json
import asyncio
import threading
from datetime import datetime
import cv2
import numpy as np
import face_recognition
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel
import requests

app = FastAPI(title="Smart Doorbell V2.2")

KNOWN_FACES_DIR = "known_faces"
LOG_FILE = "security_log.csv"
CAPTURES_DIR = "captures"
EVENT_COOLDOWN_SEC = 10

os.makedirs(CAPTURES_DIR, exist_ok=True)
os.makedirs(KNOWN_FACES_DIR, exist_ok=True)

if not os.path.exists(LOG_FILE):
    with open(LOG_FILE, mode='w', newline='') as f:
        csv.writer(f).writerow(["Timestamp", "Name", "Status", "Message", "ImageFile"])

known_encodings = []
known_names = []

latest_event = {
    "event_id": 0, "name": "---", "status": "IDLE",
    "message": "System online. Waiting for Camera...",
    "timestamp": "---", "image_file": ""
}

live_annotation = {
    "box": None, "name": None, "color": (0, 255, 0), "expiry": 0
}
last_event_log_time = 0

camera_ip = None 
latest_frame_bgr = None 

print("=" * 50)
print("  Loading Known Faces...")
for filename in sorted(os.listdir(KNOWN_FACES_DIR)):
    if filename.lower().endswith((".jpg", ".jpeg", ".png")):
        name = os.path.splitext(filename)[0].replace("_", " ").title()
        path = os.path.join(KNOWN_FACES_DIR, filename)
        image = face_recognition.load_image_file(path)
        encs = face_recognition.face_encodings(image)
        if encs:
            known_encodings.append(encs[0])
            known_names.append(name)
            print(f"  ✓ Loaded: {name}")
print("=" * 50)

class ConnectionManager:
    def __init__(self):
        self.cameras: list[WebSocket] = []
        self.frontends: list[WebSocket] = []

    async def connect_camera(self, ws: WebSocket):
        await ws.accept()
        self.cameras.append(ws)

    async def connect_frontend(self, ws: WebSocket):
        await ws.accept()
        self.frontends.append(ws)

    def disconnect_camera(self, ws: WebSocket):
        if ws in self.cameras: self.cameras.remove(ws)

    def disconnect_frontend(self, ws: WebSocket):
        if ws in self.frontends: self.frontends.remove(ws)

    async def broadcast_video(self, frame_bytes: bytes):
        for connection in self.frontends:
            try:
                await connection.send_bytes(frame_bytes)
            except Exception:
                pass

    async def broadcast_state(self):
        for connection in self.frontends:
            try:
                await connection.send_json(latest_event)
            except Exception:
                pass

    async def send_command_to_camera(self, command: str):
        for connection in self.cameras:
            try:
                await connection.send_text(command)
            except Exception:
                pass

manager = ConnectionManager()

def log_event_csv(name, status, message, image_filename):
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(LOG_FILE, mode='a', newline='') as f:
        csv.writer(f).writerow([ts, name, status, message, image_filename])


# ── THREAD 1: THE NETWORK STREAM PARSER ─────────────────────────────────────
def stream_reader_loop():
    global camera_ip, latest_frame_bgr
    
    while True:
        if camera_ip is None:
            time.sleep(1)
            continue

        url = f"http://{camera_ip}:81/stream"
        print(f"🔗 Network Thread Connecting to: {url}")
        
        bytes_buffer = b''
        try:
            with requests.Session() as session:
                response = session.get(url, stream=True, timeout=10.0)
                if response.status_code != 200:
                    time.sleep(1)
                    continue
                
                for chunk in response.iter_content(chunk_size=8192):
                    bytes_buffer += chunk
                    
                    # ANTI-BLOAT MECHANISM: If the buffer grows larger than ~100KB,
                    # it means the network lagged and dumped old frames. Nuke it!
                    if len(bytes_buffer) > 102400:
                        bytes_buffer = b''
                        continue
                    
                    while True:
                        a = bytes_buffer.find(b'\xff\xd8')
                        b = bytes_buffer.find(b'\xff\xd9')
                        
                        if a != -1 and b != -1:
                            if a < b:
                                jpg = bytes_buffer[a:b+2]
                                bytes_buffer = bytes_buffer[b+2:]
                                
                                arr = np.frombuffer(jpg, dtype=np.uint8)
                                frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
                                
                                if frame is not None:
                                    latest_frame_bgr = frame 
                            else:
                                bytes_buffer = bytes_buffer[a:]
                        else:
                            break

        except Exception as e:
            print(f"⚠️ Network Thread Reconnecting: {e}")
            time.sleep(1)


# ── THREAD 2: THE AI & BROADCAST WORKER ────────────────────────────────────
def video_processing_loop(loop):
    global latest_frame_bgr, latest_event, live_annotation, last_event_log_time
    last_ai_time = 0

    while True:
        if latest_frame_bgr is None:
            time.sleep(0.05)
            continue

        frame_bgr = latest_frame_bgr.copy()
        current_time = time.time()
        
        # ── 1. Run AI logic ──
        if current_time - last_ai_time > 0.5:
            last_ai_time = current_time
            
            # NO SCALING DOWN! The QVGA image is perfectly sized for AI.
            rgb_frame = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
            face_locations = face_recognition.face_locations(rgb_frame, model="hog")
            
            if face_locations:
                face_encoding = face_recognition.face_encodings(rgb_frame, face_locations)[0]
                
                matches = face_recognition.compare_faces(known_encodings, face_encoding, tolerance=0.50)
                distances = face_recognition.face_distance(known_encodings, face_encoding)

                name = "UNKNOWN"
                status = "PENDING"
                msg = "Action Required"
                color = (0, 165, 255)
                cmd_to_camera = None

                if True in matches:
                    best_idx = int(np.argmin(distances))
                    name = known_names[best_idx]
                    status = "SUCCESS"
                    msg = f"Welcome, {name}!"
                    color = (0, 255, 0)
                    cmd_to_camera = "UNLOCK"

                live_annotation["box"] = face_locations[0]
                live_annotation["name"] = name
                live_annotation["color"] = color
                live_annotation["expiry"] = current_time + 1.0

                if current_time - last_event_log_time > EVENT_COOLDOWN_SEC:
                    last_event_log_time = current_time
                    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
                    img_name = f"{ts}_{name}.jpg"
                    
                    save_frame = frame_bgr.copy()
                    top, right, bottom, left = face_locations[0]
                    cv2.rectangle(save_frame, (left, top), (right, bottom), color, 2)
                    cv2.imwrite(os.path.join(CAPTURES_DIR, img_name), save_frame)

                    latest_event["event_id"] += 1
                    latest_event["name"] = name
                    latest_event["status"] = status
                    latest_event["message"] = msg
                    latest_event["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    latest_event["image_file"] = img_name

                    if cmd_to_camera:
                        asyncio.run_coroutine_threadsafe(manager.send_command_to_camera(cmd_to_camera), loop)
                    asyncio.run_coroutine_threadsafe(manager.broadcast_state(), loop)
                    print(f"[{status}] {name} detected. Event ID: {latest_event['event_id']}")

        # ── 2. Draw overlay if active ──
        if live_annotation["box"] and current_time < live_annotation["expiry"]:
            top, right, bottom, left = live_annotation["box"]
            c = live_annotation["color"]
            cv2.rectangle(frame_bgr, (left, top), (right, bottom), c, 3)
            cv2.putText(frame_bgr, live_annotation["name"], (left, top-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, c, 2)

        # ── 3. Blast smooth video to Dashboard ──
        _, buffer = cv2.imencode('.jpg', frame_bgr, [cv2.IMWRITE_JPEG_QUALITY, 80])
        asyncio.run_coroutine_threadsafe(manager.broadcast_video(buffer.tobytes()), loop)
        
        time.sleep(0.05)


@app.on_event("startup")
async def startup_event():
    loop = asyncio.get_running_loop()
    t1 = threading.Thread(target=stream_reader_loop, daemon=True)
    t1.start()
    t2 = threading.Thread(target=video_processing_loop, args=(loop,), daemon=True)
    t2.start()
    print("🧵 Dual-Thread Video Processing & Network Engine Started.")

# ── Endpoints ─────────────────────────────────────────────────────────────
@app.websocket("/ws/camera")
async def websocket_camera(websocket: WebSocket):
    global camera_ip
    await manager.connect_camera(websocket)
    print("📷 Camera Control WebSocket Connected.")
    try:
        while True:
            data = await websocket.receive_text()
            try:
                parsed = json.loads(data)
                if "ip" in parsed:
                    camera_ip = parsed["ip"]
                    print(f"🌐 Camera Registered IP: {camera_ip}")
            except:
                pass
    except WebSocketDisconnect:
        print("❌ Camera Control disconnected.")
        manager.disconnect_camera(websocket)

@app.websocket("/ws/frontend")
async def websocket_frontend(websocket: WebSocket):
    await manager.connect_frontend(websocket)
    print("📺 Frontend UI connected.")
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect_frontend(websocket)
        print("❌ Frontend UI disconnected.")

class ResolveAction(BaseModel):
    action: str

@app.post("/resolve_event")
async def resolve_event(req: ResolveAction):
    if latest_event["status"] == "PENDING":
        hw_cmd = "UNLOCK" if req.action == "ADMIT" else "DENY"
        latest_event["status"] = f"MANUAL_{req.action}"
        latest_event["message"] = f"Access {req.action.capitalize()} Manually"
        latest_event["event_id"] += 1 
        
        log_event_csv(latest_event["name"], latest_event["status"], 
                      latest_event["message"], latest_event["image_file"])

        await manager.send_command_to_camera(hw_cmd)
        await manager.broadcast_state()
        return {"status": "success", "command": hw_cmd}
    return {"status": "ignored", "reason": "No pending event."}

@app.get("/latest_event")
async def get_latest_event():
    return JSONResponse(content=latest_event)

@app.get("/get_image/{filename}")
async def get_image(filename: str):
    safe_name = os.path.basename(filename)
    full_path = os.path.join(CAPTURES_DIR, safe_name)
    if os.path.exists(full_path):
        return FileResponse(full_path, media_type="image/jpeg")
    return JSONResponse(status_code=404, content={"message": "Image not found"})