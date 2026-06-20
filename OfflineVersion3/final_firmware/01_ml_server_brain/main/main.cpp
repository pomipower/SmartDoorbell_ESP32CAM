#include <stdio.h>
#include <string.h>
#include <vector>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s8.hpp"

static const char *TAG = "S3_BRAIN_HUB";

// ========================================================
// 1. STATE & DATABASE
// ========================================================
enum AppMode { MODE_SECURITY, MODE_ENROLL };
AppMode current_mode = MODE_SECURITY;

struct UserEmbedding {
    char name[32];
    float feature[128];
};

std::vector<UserEmbedding> face_db;
float pending_feature[128]; 
uint8_t* last_jpeg_buf = NULL; // Holds the most recent image for saving
size_t last_jpeg_len = 0;

void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/fr",
        .partition_label = "fr",
        .max_files = 10,
        .format_if_mount_failed = true
    };
    esp_vfs_spiffs_register(&conf);
    
    FILE* f = fopen("/fr/faces.db", "rb");
    if (f) {
        UserEmbedding temp;
        while (fread(&temp, sizeof(UserEmbedding), 1, f) == 1) {
            face_db.push_back(temp);
            ESP_LOGI(TAG, "Loaded Enrolled User: %s", temp.name);
        }
        fclose(f);
    }
}

void save_to_spiffs(const char* name, float* embedding) {
    UserEmbedding new_user;
    strncpy(new_user.name, name, 31);
    memcpy(new_user.feature, embedding, sizeof(float) * 128);
    face_db.push_back(new_user);

    FILE* f = fopen("/fr/faces.db", "ab");
    if (f) {
        fwrite(&new_user, sizeof(UserEmbedding), 1, f);
        fclose(f);
    }

    // Save the photo to SPIFFS so the UI can display it
    if (last_jpeg_buf) {
        char path[64];
        snprintf(path, sizeof(path), "/fr/usr_%s.jpg", name);
        FILE* img_f = fopen(path, "wb");
        if (img_f) {
            fwrite(last_jpeg_buf, 1, last_jpeg_len, img_f);
            fclose(img_f);
        }
    }
    ESP_LOGI(TAG, "Successfully enrolled: %s", name);
}

// ========================================================
// 2. THE WEB DASHBOARD (HTML/CSS/JS)
// ========================================================
const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <link rel="icon" href="data:,">
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Doorbell Hub</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #121212; color: #ffffff; margin: 0; padding: 0; }
        .header { background-color: #1e1e1e; padding: 20px; text-align: center; border-bottom: 2px solid #333; }
        .tabs { display: flex; justify-content: center; background-color: #1a1a1a; }
        .tab { padding: 15px 30px; cursor: pointer; border-bottom: 3px solid transparent; font-weight: bold; transition: 0.3s; }
        .tab:hover { background-color: #2a2a2a; }
        .tab.active { border-bottom: 3px solid #4CAF50; color: #4CAF50; }
        .content { display: none; padding: 20px; max-width: 600px; margin: auto; text-align: center; }
        .content.active { display: block; }
        
        .toggle-container { margin: 20px 0; background: #222; padding: 15px; border-radius: 8px; }
        .switch { position: relative; display: inline-block; width: 60px; height: 34px; vertical-align: middle; margin-left: 10px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #4CAF50; transition: .4s; border-radius: 34px; }
        .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
        input:checked + .slider { background-color: #2196F3; }
        input:checked + .slider:before { transform: translateX(26px); }

        .cam-feed { width: 100%; max-width: 320px; height: 240px; background-color: #000; border: 2px solid #4CAF50; border-radius: 8px; margin: 20px auto; object-fit: cover; }
        .status-box { font-size: 1.2em; padding: 15px; margin: 20px 0; background: #1e1e1e; border-radius: 8px; border-left: 5px solid #4CAF50; }
        
        button { background-color: #4CAF50; color: white; border: none; padding: 10px 20px; font-size: 1em; cursor: pointer; border-radius: 5px; margin: 5px; transition: 0.3s; }
        button:hover { background-color: #45a049; }
        .btn-danger { background-color: #f44336; } .btn-danger:hover { background-color: #da190b; }
        
        #controls { display: none; margin-top: 15px; }

        /* PHASE 2 ADMIN STYLES */
        .user-card { display: inline-block; width: 110px; background: #222; border-radius: 8px; margin-right: 10px; text-align: center; padding: 10px; border: 1px solid #333; }
        .user-card img { width: 90px; height: 90px; border-radius: 5px; object-fit: cover; }
        .user-scroll { overflow-x: auto; white-space: nowrap; padding-bottom: 10px; margin-bottom: 20px; text-align: left;}
        .gallery { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 10px; margin-bottom: 20px;}
        .gallery img { width: 100%; aspect-ratio: 4/3; object-fit: cover; border-radius: 5px; border: 1px solid #444; }
        .log-table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 0.9em; }
        .log-table th, .log-table td { border: 1px solid #444; padding: 8px; text-align: left; }
        .log-table th { background-color: #333; }
        h3 { text-align: left; border-bottom: 1px solid #333; padding-bottom: 5px; }
    </style>
</head>
<body>

    <div class="header">
        <h2>Smart Doorbell Hub</h2>
        <p id="ws-status" style="color: #ff9800;">Connecting...</p>
    </div>

    <div class="tabs">
        <div class="tab active" onclick="switchTab('dash')">Dashboard</div>
        <div class="tab" onclick="switchTab('admin')">Admin Panel</div>
    </div>

    <div id="dash" class="content active">
        <div class="toggle-container">
            <strong>Mode:</strong> <span id="modeLabel">SECURITY</span>
            <label class="switch">
                <input type="checkbox" id="modeToggle" onchange="toggleMode()">
                <span class="slider"></span>
            </label>
        </div>

        <img id="camImage" class="cam-feed" src="" alt="Waiting for Camera...">
        
        <div id="statusBox" class="status-box">System Idle. Waiting for trigger...</div>
        
        <div id="controls"></div>

        <hr style="border-color: #333; margin: 30px 0;">
        <button onclick="manualUnlock()" style="width: 100%; padding: 15px; font-size: 1.2em;">🔓 MANUAL UNLOCK</button>
    </div>

    <div id="admin" class="content">
        <h3>Enrolled Users</h3>
        <div class="user-scroll" id="userList">
            </div>

        <h3>Recent Captures (Past 12)</h3>
        <div class="gallery" id="gallery">
            </div>

        <h3>Visitor Logs</h3>
        <button onclick="window.open('/logs.csv')" style="float: left; margin-bottom: 10px;">📥 Export CSV</button>
        <div style="max-height: 200px; overflow-y: auto; clear: both;">
            <table class="log-table">
                <thead><tr><th>Time</th><th>Event</th><th>Person</th></tr></thead>
                <tbody id="logBody"></tbody>
            </table>
        </div>

        

        <hr style="border-color: #333; margin: 30px 0;">
        <p>Use this to clear history without deleting enrolled users.</p>
        <button class="btn-danger" style="width: 100%; padding: 15px; margin-bottom: 10px;" onclick="wipeLogs()">🗑️ DELETE ALL LOGS & CAPTURES</button>
        
        <p>Use this to wipe all enrolled faces from the system.</p>
        <button class="btn-danger" style="width: 100%; padding: 15px;" onclick="wipeDB()">⚠️ DELETE ALL ENROLLMENTS</button>
    </div>

    <script>
        let ws;
        let pastCaptures = [];

        function initWS() {
            ws = new WebSocket('ws://' + location.host + '/ws');
            ws.binaryType = 'blob';
            
            ws.onopen = () => {
                document.getElementById('ws-status').innerHTML = '<span style="color:#4CAF50;">Live</span>';
                fetchUsers();
            };
            ws.onclose = () => { document.getElementById('ws-status').innerHTML = '<span style="color:#f44336;">Disconnected. Reconnecting...</span>'; setTimeout(initWS, 2000); };
            
            ws.onmessage = function(e) {
                if (typeof e.data === 'string') {
                    handleCommand(e.data);
                } else {
                    let imgUrl = URL.createObjectURL(e.data);
                    document.getElementById('camImage').src = imgUrl;
                    
                    // Add to gallery
                    pastCaptures.unshift(imgUrl);
                    if(pastCaptures.length > 12) pastCaptures.pop();
                    updateGallery();
                }
            };
        }

        function handleCommand(cmd) {
            let status = document.getElementById('statusBox');
            let controls = document.getElementById('controls');
            let dateStr = new Date().toLocaleTimeString();

            if (cmd === "UI_MSG:NEW_FACE") {
                status.innerHTML = `[${dateStr}] New Face Detected!`;
                controls.innerHTML = `<button onclick="acceptFace()">Accept</button> <button class="btn-danger" onclick="resetControls()">Retake</button>`;
                controls.style.display = "block";
            } else if (cmd.startsWith("UI_MSG:DUPLICATE:")) {
                status.innerHTML = `[${dateStr}] Already Enrolled as <b>${cmd.split(":")[2]}</b>`;
                resetControls();
            } else if (cmd.startsWith("UI_MSG:MATCH:")) {
                let name = cmd.split(":")[2];
                status.innerHTML = `[${dateStr}] ✅ Welcome, <b>${name}</b>! Door Unlocked.`;
                logEvent("MATCH", name);
                resetControls();
            } else if (cmd === "UI_MSG:UNKNOWN") {
                status.innerHTML = `[${dateStr}] ❌ UNKNOWN PERSON DETECTED!`;
                controls.innerHTML = `<button onclick="manualAdmit()">Admit</button> <button class="btn-danger" onclick="denyAccess()">Deny</button>`;
                controls.style.display = "block";
            } else if (cmd === "UI_MSG:NO_FACE") {
                status.innerHTML = `[${dateStr}] No Face Detected in image.`;
                resetControls();
            } else if (cmd === "UI_MSG:ENROLL_SUCCESS") {
                status.innerHTML = `[${dateStr}] ✅ Enrollment Successful! Waiting for next...`;
                fetchUsers();
                resetControls();
            } else if (cmd === "UI_MSG:LOGS_WIPED") {
                pastCaptures = [];
                updateGallery();
                document.getElementById('logBody').innerHTML = '';
                alert("Logs and captures successfully cleared.");   
            } else if (cmd === "UI_MSG:DB_WIPED") {
                fetchUsers();
                alert("Database successfully wiped.");
            } else if (cmd.startsWith("UI_MSG:USERS:")) {
                renderUsers(cmd.substring(13));
            } else if (cmd === "UI_MSG:USER_DELETED") {
                fetchUsers();
            }
        }

        // --- DASHBOARD LOGIC ---
        function toggleMode() {
            let isEnroll = document.getElementById('modeToggle').checked;
            document.getElementById('modeLabel').innerText = isEnroll ? "ENROLLMENT" : "SECURITY";
            document.getElementById('statusBox').innerText = isEnroll ? "Waiting for new enrollment..." : "System Idle. Waiting for trigger...";
            resetControls();
            ws.send(isEnroll ? "CMD:MODE_ENROLL" : "CMD:MODE_SECURITY");
        }
        function acceptFace() {
            let name = prompt("Enter name for this user:");
            if(name && name.trim() !== "") ws.send("CMD:SAVE_NAME:" + name.trim());
        }
        function manualUnlock() {
            ws.send("CMD:UNLOCK");
            logEvent("MANUAL UNLOCK", "Admin");
            document.getElementById('statusBox').innerHTML = "🔓 Door manually unlocked!";
            resetControls();
        }
        function manualAdmit() {
            ws.send("CMD:UNLOCK");
            logEvent("MANUAL ADMIT", "Unknown");
            document.getElementById('statusBox').innerHTML = "🔓 Unknown Person Admitted manually.";
            resetControls();
        }
        function denyAccess() {
            logEvent("DENIED", "Unknown");
            document.getElementById('statusBox').innerHTML = "❌ Access Denied.";
            resetControls();
        }
        function resetControls() { document.getElementById('controls').style.display = "none"; }
        function switchTab(tabId) {
            document.querySelectorAll('.content').forEach(c => c.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.getElementById(tabId).classList.add('active');
            event.target.classList.add('active');
        }

        // --- ADMIN LOGIC ---
        function fetchUsers() { ws.send("CMD:GET_USERS"); }
        function renderUsers(userStr) {
            let list = document.getElementById('userList');
            if(!userStr || userStr === "") { list.innerHTML = "<p>No enrolled users.</p>"; return; }
            let users = userStr.split(',');
            let html = "";
            users.forEach(u => {
                if(u.trim() !== "") {
                    html += `<div class="user-card">
                                <img src="/img?u=${u}" alt="${u}" onerror="this.src='data:image/svg+xml;utf8,<svg xmlns=http://www.w3.org/2000/svg width=100 height=100><rect width=100 height=100 fill=%23444/></svg>'">
                                <p style="margin: 5px 0;">${u}</p>
                                <button class="btn-danger" style="padding: 5px; width: 100%; font-size: 0.9em;" onclick="deleteUser('${u}')">Delete</button>
                             </div>`;
                }
            });
            list.innerHTML = html;
        }
        function deleteUser(name) {
            if(confirm("Delete " + name + " from database?")) ws.send("CMD:DELETE_USER:" + name);
        }
        function updateGallery() {
            let gal = document.getElementById('gallery');
            gal.innerHTML = pastCaptures.map(src => `<img src="${src}">`).join('');
        }
        function logEvent(evt, person) {
            let d = new Date();
            let ts = d.toLocaleDateString() + " " + d.toLocaleTimeString();
            ws.send(`CMD:LOG:${ts},${evt},${person}`);
            let row = `<tr><td>${ts}</td><td>${evt}</td><td>${person}</td></tr>`;
            document.getElementById('logBody').innerHTML = row + document.getElementById('logBody').innerHTML;
        }
        function wipeDB() {
            if(confirm("Are you sure? This will delete all faces permanently!")) ws.send("CMD:WIPE_DB");
        }
        function wipeLogs() {
            if(confirm("Are you sure you want to clear all logs and recent images?")) {
                ws.send("CMD:WIPE_LOGS");
            }
        }

        window.onload = initWS;
    </script>
</body>
</html>
)rawliteral";

// ========================================================
// 3. ML UTILS & BROADCAST SYSTEM
// ========================================================
httpd_handle_t global_server = NULL;
QueueHandle_t ml_queue;

struct MLJob { uint8_t* jpeg_buf; size_t jpeg_len; };

void broadcast_ws_msg(httpd_ws_type_t type, const uint8_t *data, size_t len) {
    if(!global_server) return;
    size_t max_clients = 10;
    int client_fds[10];
    if (httpd_get_client_list(global_server, &max_clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < max_clients; i++) {
            httpd_ws_frame_t ws_pkt = {};
            ws_pkt.payload = (uint8_t*)data; 
            ws_pkt.len = len; 
            ws_pkt.type = type;
            httpd_ws_send_frame_async(global_server, client_fds[i], &ws_pkt);
        }
    }
}

float calculate_cosine_similarity(float* a, float* b, int len) {
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for(int i = 0; i < len; i++) {
        dot += a[i] * b[i]; 
        normA += a[i] * a[i]; 
        normB += b[i] * b[i];
    }
    if(normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (sqrt(normA) * sqrt(normB));
}

void align_crop_face_nn(uint8_t* src, int src_w, int src_h, const std::vector<int>& box, uint8_t* dest, int dest_w, int dest_h) {
    int x1 = box[0], y1 = box[1], x2 = box[2], y2 = box[3];
    if(x1 < 0) x1 = 0; 
    if(y1 < 0) y1 = 0;
    if(x2 >= src_w) x2 = src_w - 1; 
    if(y2 >= src_h) y2 = src_h - 1;
    
    int crop_w = x2 - x1 + 1; 
    int crop_h = y2 - y1 + 1;
    if(crop_w <= 0 || crop_h <= 0) return;
    
    for (int y = 0; y < dest_h; y++) {
        for (int x = 0; x < dest_w; x++) {
            int src_x = x1 + (x * crop_w) / dest_w; 
            int src_y = y1 + (y * crop_h) / dest_h;
            if (src_x >= src_w) src_x = src_w - 1; 
            if (src_y >= src_h) src_y = src_h - 1;
            int dest_idx = (y * dest_w + x) * 3; 
            int src_idx = (src_y * src_w + src_x) * 3;
            dest[dest_idx] = src[src_idx]; 
            dest[dest_idx + 1] = src[src_idx + 1]; 
            dest[dest_idx + 2] = src[src_idx + 2];
        }
    }
}

// ========================================================
// 4. THE ML PIPELINE (CORE 1)
// ========================================================
void ml_inference_task(void *pvParameters) {
    ESP_LOGI(TAG, "ML Core Online.");
    HumanFaceDetectMSR01 detector(0.3F, 0.5F, 10, 1.0F); 
    FaceRecognition112V1S8 recognizer;

    MLJob job;
    while (1) {
        if (xQueueReceive(ml_queue, &job, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();
            broadcast_ws_msg(HTTPD_WS_TYPE_BINARY, job.jpeg_buf, job.jpeg_len);

            // Save to memory for potential enrollment
            if (last_jpeg_buf) heap_caps_free(last_jpeg_buf);
            last_jpeg_buf = (uint8_t*)heap_caps_malloc(job.jpeg_len, MALLOC_CAP_SPIRAM);
            if(last_jpeg_buf) {
                memcpy(last_jpeg_buf, job.jpeg_buf, job.jpeg_len);
                last_jpeg_len = job.jpeg_len;
            }

            int img_w = 320; 
            int img_h = 240; 
            uint8_t *rgb_buf = (uint8_t *)heap_caps_malloc(img_w * img_h * 3, MALLOC_CAP_SPIRAM);
            bool dec_res = false;
            
            if (rgb_buf) dec_res = fmt2rgb888(job.jpeg_buf, job.jpeg_len, PIXFORMAT_JPEG, rgb_buf);
            free(job.jpeg_buf); 
            
            if (!dec_res) {
                if(rgb_buf) free(rgb_buf);
                continue;
            }

            std::list<dl::detect::result_t> &detect_results = detector.infer(rgb_buf, {img_h, img_w, 3});

            if (detect_results.size() > 0) {
                uint8_t* aligned_face = (uint8_t*)heap_caps_malloc(112 * 112 * 3, MALLOC_CAP_SPIRAM);
                align_crop_face_nn(rgb_buf, img_w, img_h, detect_results.front().box, aligned_face, 112, 112);

                int8_t* input_int8 = (int8_t*)heap_caps_malloc(112 * 112 * 3, MALLOC_CAP_SPIRAM);
                for(int i = 0; i < 112 * 112 * 3; i++) input_int8[i] = (int8_t)((int)aligned_face[i] - 128);

                dl::Tensor<int8_t> input_tensor;
                input_tensor.set_element(input_int8).set_shape({112, 112, 3}).set_auto_free(false);
                
                auto& feature = recognizer.forward(input_tensor);
                int8_t* feature_ptr_int8 = (int8_t*)feature.get_element_ptr();

                float feature_ptr[128];
                for(int i = 0; i < 128; i++) feature_ptr[i] = (float)feature_ptr_int8[i];

                if (current_mode == MODE_ENROLL) {
                    float highest_similarity = 0.0f; 
                    char best_match[32] = "";
                    for (const auto& user : face_db) {
                        float similarity = calculate_cosine_similarity(feature_ptr, (float*)user.feature, 128);
                        if (similarity > highest_similarity) { 
                            highest_similarity = similarity; 
                            strcpy(best_match, user.name); 
                        }
                    }

                    if (highest_similarity > 0.80f) {
                        char msg[64]; 
                        snprintf(msg, sizeof(msg), "UI_MSG:DUPLICATE:%s", best_match);
                        ESP_LOGW(TAG, "Confidence: %.2f", highest_similarity);
                        broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
                    } else {
                        memcpy(pending_feature, feature_ptr, sizeof(float) * 128);
                        const char* msg = "UI_MSG:NEW_FACE";
                        broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
                    }
                } 
                else if (current_mode == MODE_SECURITY) {
                    if (face_db.empty()) {
                        ESP_LOGW(TAG, "Database Empty! Unknown User Detected");
                        const char* msg = "UI_MSG:UNKNOWN";
                        broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
                    } else {
                        float highest_similarity = 0.0f; 
                        char best_match[32] = "";
                        for (const auto& user : face_db) {
                            float similarity = calculate_cosine_similarity(feature_ptr, (float*)user.feature, 128);
                            if (similarity > highest_similarity) { 
                                highest_similarity = similarity; 
                                strcpy(best_match, user.name); 
                            }
                        }

                        if (highest_similarity > 0.80f) {
                            char msg[64];
                            snprintf(msg, sizeof(msg), "UI_MSG:MATCH:%s", best_match);
                            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
                            ESP_LOGW(TAG, "Confidence: %.2f", highest_similarity);
                            const char* unlock_cmd = "UNLOCK";
                            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)unlock_cmd, strlen(unlock_cmd));
                        } else {
                            // Debug log for failed matches to see how close they were
                            ESP_LOGW(TAG, "Best match was %s, but score was only %.2f (Threshold: 0.80)", best_match, highest_similarity);
                            
                            const char* msg = "UI_MSG:UNKNOWN";
                            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
                        }
                    }
                }

                free(aligned_face);
                free(input_int8);
            } else {
                const char* msg = "UI_MSG:NO_FACE";
                broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
            }
            free(rgb_buf);

            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "ML Pipeline ran in %lld ms", (end_time - start_time)/1000);
        }
    }
}

// ========================================================
// 5. WEB SERVER ROUTING
// ========================================================
esp_err_t http_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

// Serves the JPEGs of enrolled users for the Admin Tab cards
esp_err_t img_get_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char name[32];
        if (httpd_query_key_value(buf, "u", name, sizeof(name)) == ESP_OK) {
            char path[64]; 
            snprintf(path, sizeof(path), "/fr/usr_%s.jpg", name);
            FILE* f = fopen(path, "rb");
            if (f) {
                httpd_resp_set_type(req, "image/jpeg");
                char chunk[1024]; 
                size_t read_bytes;
                while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                    httpd_resp_send_chunk(req, chunk, read_bytes);
                }
                fclose(f);
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

// Downloads the CSV Logs
esp_err_t logs_get_handler(httpd_req_t *req) {
    FILE* f = fopen("/fr/logs.csv", "rb");
    if (!f) {
        httpd_resp_send(req, "Time,Event,Person\n", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"visitor_logs.csv\"");
    char chunk[1024]; 
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) return ret;

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        char* text = (char*)ws_pkt.payload;
        
        if (strncmp(text, "CMD:MODE_ENROLL", 15) == 0) {
            current_mode = MODE_ENROLL;
        } else if (strncmp(text, "CMD:MODE_SECURITY", 17) == 0) {
            current_mode = MODE_SECURITY;
        } else if (strncmp(text, "CMD:SAVE_NAME:", 14) == 0) {
            char* name = text + 14;
            save_to_spiffs(name, pending_feature);
            const char* msg = "UI_MSG:ENROLL_SUCCESS";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        } else if (strncmp(text, "CMD:GET_USERS", 13) == 0) {
            char users[512] = "UI_MSG:USERS:";
            for (const auto& user : face_db) { 
                strncat(users, user.name, sizeof(users) - strlen(users) - 1); 
                strncat(users, ",", sizeof(users) - strlen(users) - 1); 
            }
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)users, strlen(users));
        } else if (strncmp(text, "CMD:DELETE_USER:", 16) == 0) {
            char* name = text + 16;
            for (auto it = face_db.begin(); it != face_db.end(); ++it) {
                if (strcmp(it->name, name) == 0) { 
                    face_db.erase(it); 
                    break; 
                }
            }
            FILE* f = fopen("/fr/faces.db", "wb");
            if (f) { 
                for (const auto& user : face_db) fwrite(&user, sizeof(UserEmbedding), 1, f); 
                fclose(f); 
            }
            char path[64]; snprintf(path, sizeof(path), "/fr/usr_%s.jpg", name); 
            remove(path);
            const char* msg = "UI_MSG:USER_DELETED";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        } else if (strncmp(text, "CMD:LOG:", 8) == 0) {
            FILE* f = fopen("/fr/logs.csv", "ab");
            if (f) { fprintf(f, "%s\n", text + 8); fclose(f); }
        } else if (strncmp(text, "CMD:WIPE_LOGS", 13) == 0) {
            remove("/fr/logs.csv");
            const char* msg = "UI_MSG:LOGS_WIPED";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
            ESP_LOGI(TAG, "Visitor Logs Cleared!");
        } else if (strncmp(text, "CMD:WIPE_DB", 11) == 0) {
            for (const auto& user : face_db) { 
                char path[64]; 
                snprintf(path, sizeof(path), "/fr/usr_%s.jpg", user.name); 
                remove(path); 
            }
            face_db.clear();
            remove("/fr/faces.db");
            const char* msg = "UI_MSG:DB_WIPED";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        } else if (strncmp(text, "CMD:UNLOCK", 10) == 0) {
            const char* msg = "UNLOCK";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        }

        free(buf);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
        uint8_t* jpeg_buf = (uint8_t*)heap_caps_malloc(ws_pkt.len, MALLOC_CAP_SPIRAM);
        ws_pkt.payload = jpeg_buf;
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        MLJob job; 
        job.jpeg_buf = jpeg_buf; 
        job.jpeg_len = ws_pkt.len;
        if (xQueueSend(ml_queue, &job, 0) != pdTRUE) free(jpeg_buf);
    }
    return ESP_OK;
}

httpd_handle_t start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7; 

    httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = http_get_handler, .user_ctx = NULL };
    httpd_uri_t img_get = { .uri = "/img", .method = HTTP_GET, .handler = img_get_handler, .user_ctx = NULL };
    httpd_uri_t logs_get = { .uri = "/logs.csv", .method = HTTP_GET, .handler = logs_get_handler, .user_ctx = NULL };
    httpd_uri_t ws_uri = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = NULL, .is_websocket = true };

    if (httpd_start(&global_server, &config) == ESP_OK) {
        httpd_register_uri_handler(global_server, &uri_get);
        httpd_register_uri_handler(global_server, &img_get);
        httpd_register_uri_handler(global_server, &logs_get);
        httpd_register_uri_handler(global_server, &ws_uri);
        return global_server;
    }
    return NULL;
}

void wifi_init_softap() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, "ESP32_ML_BRAIN");
    strcpy((char*)wifi_config.ap.password, "12345678");
    wifi_config.ap.ssid_len = strlen("ESP32_ML_BRAIN");
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "--- Booting Hub ---");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); 
        ret = nvs_flash_init();
    }
    init_spiffs();
    wifi_init_softap();

    ml_queue = xQueueCreate(3, sizeof(MLJob));
    xTaskCreatePinnedToCore(ml_inference_task, "ML_Task", 1024 * 32, NULL, 5, NULL, 1);
    start_webserver();
}