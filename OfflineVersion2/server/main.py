import os
import json
import base64
from datetime import datetime
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

# Setup History Directory
HISTORY_DIR = "history"
HISTORY_FILE = os.path.join(HISTORY_DIR, "history.json")
if not os.path.exists(HISTORY_DIR):
    os.makedirs(HISTORY_DIR)
if not os.path.exists(HISTORY_FILE):
    with open(HISTORY_FILE, "w") as f:
        json.dump([], f)

app = FastAPI(title="Smart Doorbell V2 Server")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Serve the history folder so dashboards can fetch the raw images
app.mount("/history", StaticFiles(directory=HISTORY_DIR), name="history")

def load_history():
    with open(HISTORY_FILE, "r") as f:
        return json.load(f)

def save_history(data):
    with open(HISTORY_FILE, "w") as f:
        json.dump(data, f)

def clear_history_files():
    for filename in os.listdir(HISTORY_DIR):
        if filename.endswith(".jpg"):
            os.remove(os.path.join(HISTORY_DIR, filename))
    save_history([])

class ConnectionManager:
    def __init__(self):
        self.esp32_connection: WebSocket = None
        self.dashboard_connections: list[WebSocket] = []

    async def connect_esp32(self, websocket: WebSocket):
        await websocket.accept()
        self.esp32_connection = websocket
        print("🟢 ESP32 Hardware Connected!")

    async def connect_dashboard(self, websocket: WebSocket):
        await websocket.accept()
        self.dashboard_connections.append(websocket)
        print(f"🖥️ Dashboard Connected! Total: {len(self.dashboard_connections)}")

    def disconnect_dashboard(self, websocket: WebSocket):
        if websocket in self.dashboard_connections:
            self.dashboard_connections.remove(websocket)
            print("🖥️ Dashboard Disconnected.")

    async def disconnect_esp32(self, websocket: WebSocket):
        if self.esp32_connection == websocket: 
            self.esp32_connection = None
            print("🔴 ESP32 Hardware Disconnected!")

    async def broadcast_to_dashboards(self, message: dict):
        dead_connections = []
        for connection in self.dashboard_connections:
            try:
                await connection.send_json(message)
            except:
                dead_connections.append(connection)
        for dead in dead_connections:
            self.disconnect_dashboard(dead)

    async def send_to_esp32(self, command: dict):
        if self.esp32_connection:
            await self.esp32_connection.send_json(command)
            return True
        return False

manager = ConnectionManager()

@app.websocket("/ws/esp32")
async def websocket_esp32(websocket: WebSocket):
    await manager.connect_esp32(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            try:
                payload = json.loads(data)
                status = payload.get('status')
                print(f"📷 [ESP32 -> DASHBOARD]: Status = {status}")
                
                # If payload has an image, process it for history
                if payload.get("image") and status in ["MATCH", "INTRUDER", "ENROLLED"]:
                    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                    img_filename = f"img_{timestamp}.jpg"
                    img_path = os.path.join(HISTORY_DIR, img_filename)
                    
                    # Decode and save the Base64 image
                    with open(img_path, "wb") as fh:
                        fh.write(base64.b64decode(payload["image"]))
                    
                    # Append to history JSON
                    history = load_history()
                    event_record = {
                        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                        "raw_id": timestamp, # Used to uniquely identify this event for admit/deny
                        "status": status,
                        "id": payload.get("id"),
                        "similarity": payload.get("similarity"),
                        "image_url": f"/history/{img_filename}",
                        "action": "Pending" if status == "INTRUDER" else "Auto-Admitted" if status == "MATCH" else "N/A"
                    }
                    history.append(event_record)
                    save_history(history)
                    
                    # Add event_record to payload so frontend knows what event ID this is
                    payload["event_record"] = event_record

                await manager.broadcast_to_dashboards(payload)
            except json.JSONDecodeError:
                print("⚠️ Malformed JSON from ESP32")
    except WebSocketDisconnect:
        await manager.disconnect_esp32(websocket)

@app.websocket("/ws/dashboard")
async def websocket_dashboard(websocket: WebSocket):
    await manager.connect_dashboard(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            command = json.loads(data)
            cmd_type = command.get("cmd")
            print(f"⚡ [DASHBOARD -> SERVER/ESP32]: {command}")
            
            # Intercept Server-Only Commands
            if cmd_type == "get_history":
                await websocket.send_json({"type": "history_update", "data": load_history()})
            
            elif cmd_type == "clear_history":
                clear_history_files()
                await manager.broadcast_to_dashboards({"type": "history_update", "data": []})
            
            elif cmd_type == "resolve_intruder":
                # Update history action
                history = load_history()
                for record in history:
                    if record.get("raw_id") == command.get("raw_id"):
                        record["action"] = "Admitted" if command.get("action") == "admit" else "Denied"
                        break
                save_history(history)
                
                # Forward Unlock to ESP32 if admitted
                if command.get("action") == "admit":
                    await manager.send_to_esp32({"cmd": "unlock_door"})
                
                # Broadcast updated history to grey out buttons on all open dashboards/displays
                await manager.broadcast_to_dashboards({"type": "history_update", "data": history})
            
            else:
                # Standard commands (set_mode, unlock_door, delete_user) go directly to ESP32
                success = await manager.send_to_esp32(command)
                if not success:
                    await websocket.send_json({"type": "error", "message": "ESP32 Offline"})
                
    except WebSocketDisconnect:
        manager.disconnect_dashboard(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)