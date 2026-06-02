"""
Smart Doorbell — Backend Server (Version 2.1)
=============================================
Flask server running on your PC/laptop.

Endpoints
---------
POST /doorbell_ring        ← ESP32-CAM posts raw JPEG here
GET  /latest_event         ← Indoor display polls this every 2s (JSON)
GET  /latest_image         ← Indoor display fetches JPEG after new event
GET  /get_image/<filename> ← Streamlit dashboard fetches thumbnails
POST /resolve_event        ← Display/Dashboard posts ADMIT/DENY actions here
GET  /doorbell_status      ← ESP32-CAM polls this for 60s when action is pending

File outputs
------------
captures/YYYYMMDD_HHMMSS_NAME.jpg   timestamped archive of every ring
latest_capture.jpg                  always overwritten with the newest frame
security_log.csv                    audit trail (Timestamp, Name, Status, Message, ImageFile)
"""

from flask import Flask, request, jsonify, send_file
import cv2
import numpy as np
import face_recognition
import os
import csv
from datetime import datetime
import threading

app = Flask(__name__)

# ── Paths ────────────────────────────────────────────────────────────────────
KNOWN_FACES_DIR  = "known_faces"
LOG_FILE         = "security_log.csv"
CAPTURES_DIR     = "captures"
LATEST_IMAGE_PATH = "latest_capture.jpg"

# ── In-memory latest event state (thread-safe via a lock) ────────────────────
_event_lock = threading.Lock()
latest_event = {
    "event_id" : 0,          # Increments on every ring; display compares to detect new events
    "name"     : "---",
    "status"   : "IDLE",
    "message"  : "System online. Awaiting visitor.",
    "timestamp": "---",
    "image_file": ""
}

# ── Startup: create dirs + CSV ────────────────────────────────────────────────
os.makedirs(CAPTURES_DIR, exist_ok=True)
os.makedirs(KNOWN_FACES_DIR, exist_ok=True)

if not os.path.exists(LOG_FILE):
    with open(LOG_FILE, mode='w', newline='') as f:
        csv.writer(f).writerow(["Timestamp", "Name", "Status", "Message", "ImageFile"])

# ── Load known faces from known_faces/ directory ──────────────────────────────
known_encodings = []
known_names     = []

print("=" * 50)
print("  Smart Doorbell Server — Loading known faces")
print("=" * 50)

for filename in sorted(os.listdir(KNOWN_FACES_DIR)):
    if filename.lower().endswith((".jpg", ".jpeg", ".png")):
        name = os.path.splitext(filename)[0].replace("_", " ").title()
        filepath = os.path.join(KNOWN_FACES_DIR, filename)
        image = face_recognition.load_image_file(filepath)
        encodings = face_recognition.face_encodings(image)
        if encodings:
            known_encodings.append(encodings[0])
            known_names.append(name)
            print(f"  ✓  Loaded: {name}")
        else:
            print(f"  ✗  WARNING: No face found in {filename} — skipping")

print(f"\n  Total known persons: {len(known_names)}")
print("=" * 50)

# ─────────────────────────────────────────────────────────────────────────────
#  Helpers
# ─────────────────────────────────────────────────────────────────────────────

def log_event(name: str, status: str, message: str, image_filename: str):
    """Append one row to the CSV audit log, ensuring headers exist."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Check if the file exists BEFORE opening it
    file_exists = os.path.isfile(LOG_FILE) 
    
    with open(LOG_FILE, mode='a', newline='') as f:
        writer = csv.writer(f)
        # If Streamlit deleted it, write the headers first
        if not file_exists:
            writer.writerow(["Timestamp", "Name", "Status", "Message", "ImageFile"])
            
        writer.writerow([timestamp, name, status, message, image_filename])


def update_latest_event(name: str, status: str, message: str, image_filename: str):
    """Update the shared in-memory event dict. Thread-safe."""
    global latest_event
    with _event_lock:
        latest_event = {
            "event_id"  : latest_event["event_id"] + 1,
            "name"      : name,
            "status"    : status,
            "message"   : message,
            "timestamp" : datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "image_file": image_filename
        }

# ─────────────────────────────────────────────────────────────────────────────
#  Routes
# ─────────────────────────────────────────────────────────────────────────────

@app.route('/doorbell_ring', methods=['POST'])
def doorbell_ring():
    """
    Main endpoint. Receives raw JPEG from ESP32-CAM, runs face recognition,
    saves annotated image, updates state, returns LCD-formatted text response.

    Response format (plain text, 2 lines separated by \\n):
        SUCCESS  →  "Welcome, NAME!\\nIn: HH:MM AM"
        DENIED   →  "ACCESS DENIED\\nUnknown Visitor"
        FAILED   →  "SCAN FAILED\\nNo Face Detected"
    """
    raw_bytes = request.data
    if not raw_bytes:
        return "Err\nNo Image Data", 400

    # Decode JPEG → OpenCV BGR array
    nparr = np.frombuffer(raw_bytes, dtype=np.uint8)
    original_bgr = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
    if original_bgr is None:
        return "Err\nJPEG Decode Fail", 400

    # ── Auto-rotation: try all 4 orientations to find a face ─────────────────
    # Handles cameras mounted in non-standard orientations.
    rotations = [
        None,
        cv2.ROTATE_90_CLOCKWISE,
        cv2.ROTATE_180,
        cv2.ROTATE_90_COUNTERCLOCKWISE
    ]

    face_locations = []
    active_bgr = active_rgb = None

    for rot_code in rotations:
        candidate_bgr = cv2.rotate(original_bgr, rot_code) if rot_code is not None \
                        else original_bgr.copy()
        candidate_rgb = cv2.cvtColor(candidate_bgr, cv2.COLOR_BGR2RGB)
        face_locations = face_recognition.face_locations(candidate_rgb, model="hog")
        if face_locations:
            active_bgr = candidate_bgr
            active_rgb = candidate_rgb
            break

    # Fallback to original orientation if no face found in any rotation
    if not face_locations:
        active_bgr = original_bgr.copy()
        active_rgb = cv2.cvtColor(original_bgr, cv2.COLOR_BGR2RGB)

    # ── Recognition ──────────────────────────────────────────────────────────
    # ── Default state for Unknown Faces ──────────────────────────────────────
    name      = "UNKNOWN"
    lcd_line1 = "ACTION REQUIRED"
    lcd_line2 = "Unknown Visitor"
    status    = "PENDING"
    box_color = (0, 165, 255)   # Orange bounding box for pending

    if face_locations:
        face_encoding = face_recognition.face_encodings(active_rgb, face_locations)[0]
        matches       = face_recognition.compare_faces(known_encodings, face_encoding, tolerance=0.50)
        distances     = face_recognition.face_distance(known_encodings, face_encoding)

        if True in matches:
            best_idx  = int(np.argmin(distances))
            name      = known_names[best_idx]
            time_str  = datetime.now().strftime("%I:%M %p")
            lcd_line1 = f"Welcome, {name[:7]}!"
            lcd_line2 = f"In: {time_str}"
            status    = "SUCCESS"
            box_color = (0, 255, 0)   # BGR green

        # Annotate image with bounding box + name label
        top, right, bottom, left = face_locations[0]
        cv2.rectangle(active_bgr, (left, top), (right, bottom), box_color, 2)
        cv2.putText(
            active_bgr, name, (left, top - 10),
            cv2.FONT_HERSHEY_SIMPLEX, 0.85, box_color, 2, cv2.LINE_AA
        )

    else:
        # No face found in any orientation
        lcd_line1 = "SCAN FAILED"
        lcd_line2 = "No Face Detected"
        status    = "FAILED"

    # ── Save images ──────────────────────────────────────────────────────────
    ts             = datetime.now().strftime("%Y%m%d_%H%M%S")
    image_filename = f"{ts}_{name}.jpg"
    archive_path   = os.path.join(CAPTURES_DIR, image_filename)

    cv2.imwrite(archive_path, active_bgr)          # Timestamped archive copy
    cv2.imwrite(LATEST_IMAGE_PATH, active_bgr)     # Always-current "latest" copy

    # ── Persist to CSV + update in-memory event ───────────────────────────────
    log_event(name, status, lcd_line1, image_filename)
    update_latest_event(name, status, lcd_line1, image_filename)

    # ── Console log ──────────────────────────────────────────────────────────
    symbol = "✓" if status == "SUCCESS" else "✗"
    print(f"  [{symbol}] {status:<8} | {name:<20} | {datetime.now().strftime('%H:%M:%S')}")

    return f"{lcd_line1}\n{lcd_line2}", 200


@app.route('/latest_event', methods=['GET'])
def get_latest_event():
    """
    Returns JSON describing the most recent doorbell ring event.
    Polled every ~2 seconds by the indoor display unit.

    Response JSON:
    {
        "event_id"  : <int>,    // Monotonically increasing; display tracks this
        "name"      : <str>,
        "status"    : "SUCCESS" | "DENIED" | "FAILED" | "IDLE",
        "message"   : <str>,    // First LCD line (e.g. "Welcome, John!")
        "timestamp" : <str>,
        "image_file": <str>     // Filename in captures/ (for dashboard)
    }
    """
    with _event_lock:
        snapshot = dict(latest_event)
    return jsonify(snapshot)


@app.route('/latest_image', methods=['GET'])
def get_latest_image():
    """
    Serves the most recently captured annotated JPEG.
    Called by the indoor display after detecting a new event_id.
    """
    if os.path.exists(LATEST_IMAGE_PATH):
        return send_file(
            LATEST_IMAGE_PATH,
            mimetype='image/jpeg',
            max_age=0           # No caching — always serve fresh
        )
    return "No captures yet", 404


@app.route('/get_image/<filename>', methods=['GET'])
def get_image_by_name(filename):
    """
    Serves a specific archived capture by filename.
    Used by the Streamlit dashboard to show thumbnails in the audit log.
    """
    # Sanitise: prevent path traversal attacks
    safe_name = os.path.basename(filename)
    full_path = os.path.join(CAPTURES_DIR, safe_name)
    if os.path.exists(full_path):
        return send_file(full_path, mimetype='image/jpeg')
    return "Image not found", 404


@app.route('/resolve_event', methods=['POST'])
def resolve_event():
    """Triggered by the Display or Dashboard buttons."""
    action = request.json.get("action")
    with _event_lock:
        if latest_event["status"] == "PENDING":
            # Update state based on button clicked
            latest_event["status"] = "MANUAL_ADMIT" if action == "ADMIT" else "MANUAL_DENY"
            latest_event["message"] = "Access Granted Manually" if action == "ADMIT" else "Access Denied Manually"
            
            # CRITICAL FIX: Bump the event ID so the C++ display knows to redraw!
            latest_event["event_id"] += 1 
            
            # Append interaction to the CSV log
            log_event(latest_event["name"], latest_event["status"], latest_event["message"], latest_event["image_file"])
    return "OK", 200


@app.route('/doorbell_status', methods=['GET'])
def doorbell_status():
    """Camera polls this endpoint while waiting for an admin decision."""
    with _event_lock:
        return latest_event["status"], 200


# ─────────────────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    print(f"\n  Server listening on  http://0.0.0.0:5050")
    print(f"  Access from other devices on your network:")
    import socket
    hostname = socket.gethostname()
    try:
        local_ip = socket.gethostbyname(hostname)
        print(f"  → http://{local_ip}:5050\n")
    except Exception:
        print(f"  → Check your IP with ipconfig / ip a\n")
    print("=" * 50)

    app.run(host='0.0.0.0', port=5050, threaded=True)
