from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
import json

app = FastAPI(title="Smart Doorbell V2 Server")

# Allow CORS for local frontend execution
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

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
        if self.esp32_connection == websocket: # CRITICAL: Only delete if it's the matching socket
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

# ── ESP32 HARDWARE ENDPOINT ──
@app.websocket("/ws/esp32")
async def websocket_esp32(websocket: WebSocket):
    await manager.connect_esp32(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            try:
                payload = json.loads(data)
                print(f"📷 [ESP32 -> DASHBOARD]: Status = {payload.get('status')}")
                await manager.broadcast_to_dashboards(payload)
            except json.JSONDecodeError:
                print("⚠️ Malformed JSON from ESP32")
    except WebSocketDisconnect:
        await manager.disconnect_esp32(websocket) # CRITICAL: Pass the specific websocket here!

# ── FRONTEND DASHBOARD ENDPOINT ──
@app.websocket("/ws/dashboard")
async def websocket_dashboard(websocket: WebSocket):
    await manager.connect_dashboard(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            command = json.loads(data)
            print(f"⚡ [DASHBOARD -> ESP32]: {command}")
            
            success = await manager.send_to_esp32(command)
            if not success:
                await websocket.send_json({"type": "error", "message": "ESP32 Offline"})
                
    except WebSocketDisconnect:
        manager.disconnect_dashboard(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)