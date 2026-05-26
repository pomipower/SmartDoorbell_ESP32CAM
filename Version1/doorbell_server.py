from flask import Flask, request, send_file, jsonify
from flask_socketio import SocketIO
import cv2
import numpy as np
import face_recognition
import os
import csv
from datetime import datetime

app = Flask(__name__)
# Initialize WebSockets
socketio = SocketIO(app, cors_allowed_origins="*")

KNOWN_FACES_DIR = "known_faces"
LOG_FILE = "security_log.csv"
known_encodings = []
known_names = []

# --- INIT DATABASE & ML MODELS (Same as before) ---
if not os.path.exists(LOG_FILE):
    with open(LOG_FILE, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Timestamp", "Name", "Status", "Message"])

print("Loading known faces...")
for filename in os.listdir(KNOWN_FACES_DIR):
    if filename.endswith((".jpg", ".jpeg", ".png")):
        name = os.path.splitext(filename)[0].capitalize() 
        filepath = os.path.join(KNOWN_FACES_DIR, filename)
        image = face_recognition.load_image_file(filepath)
        encodings = face_recognition.face_encodings(image)
        if encodings:
            known_encodings.append(encodings[0])
            known_names.append(name)
            print(f"Loaded: {name}")

def log_event(name, status, message):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(LOG_FILE, mode='a', newline='') as file:
        writer = csv.writer(file)
        writer.writerow([timestamp, name, status, message])

# --- ENDPOINTS ---

@app.route('/clock_in', methods=['POST'])
def process_clock_in():
    raw_data = request.data
    if not raw_data:
        return jsonify({"status": "FAILED", "message": "No Image Received"}), 400
        
    nparr = np.frombuffer(raw_data, np.uint8)
    original_bgr = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

    # Face detection and auto-rotation
    active_rgb, active_bgr = None, None
    face_locations = []
    
    for rot_code in [(None), (cv2.ROTATE_90_CLOCKWISE), (cv2.ROTATE_180), (cv2.ROTATE_90_COUNTERCLOCKWISE)]:
        test_bgr = cv2.rotate(original_bgr, rot_code) if rot_code is not None else original_bgr.copy()
        test_rgb = cv2.cvtColor(test_bgr, cv2.COLOR_BGR2RGB)
        face_locations = face_recognition.face_locations(test_rgb)
        if len(face_locations) > 0:
            active_rgb, active_bgr = test_rgb, test_bgr
            break 
            
    if len(face_locations) == 0:
        active_rgb = cv2.cvtColor(original_bgr, cv2.COLOR_BGR2RGB)
        active_bgr = original_bgr.copy()

    # Defaults
    name = "UNKNOWN"
    status = "DENIED"
    message = "Intruder Alert"
    box_color = (0, 0, 255)

    if len(face_locations) > 0:
        encoding = face_recognition.face_encodings(active_rgb, face_locations)[0]
        matches = face_recognition.compare_faces(known_encodings, encoding, tolerance=0.5)
        face_distances = face_recognition.face_distance(known_encodings, encoding)
        
        if True in matches:
            best_match_index = np.argmin(face_distances)
            name = known_names[best_match_index]
            status = "SUCCESS"
            message = f"Welcome, {name}"
            box_color = (0, 255, 0) 

        # Draw bounding box
        top, right, bottom, left = face_locations[0]
        cv2.rectangle(active_bgr, (left, top), (right, bottom), box_color, 2)
        cv2.putText(active_bgr, name, (left, top - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.8, box_color, 2)

    # Save the processed image
    cv2.imwrite("debug_latest_photo.jpg", active_bgr)
    log_event(name, status, message)
    
    # 1. BROADCAST TO INDOOR DISPLAY VIA WEBSOCKET
    socketio.emit('doorbell_event', {'name': name, 'status': status, 'message': message})
    
    # 2. RESPOND TO ESP32-CAM VIA HTTP JSON (For Relay Control)
    print(f"Logged: {name} | {status}")
    return jsonify({"status": status, "name": name, "message": message}), 200

# Endpoint for the display to download the latest processed image
@app.route('/latest_image.jpg', methods=['GET'])
def get_latest_image():
    if os.path.exists("debug_latest_photo.jpg"):
        return send_file("debug_latest_photo.jpg", mimetype='image/jpeg')
    return "No image", 404

if __name__ == '__main__':
    print("Smart Doorbell Server Active on Port 5050...")
    # Use socketio.run instead of app.run for WebSocket support
    socketio.run(app, host='0.0.0.0', port=5050, allow_unsafe_werkzeug=True)