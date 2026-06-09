"""
Smart Doorbell — Admin Dashboard (Version 2.2 WebSocket Edition)
"""
import streamlit as st
import streamlit.components.v1 as components
import requests

SERVER_IP = "127.0.0.1" 
HTTP_URL = f"http://{SERVER_IP}:5050"
WS_URL = f"ws://{SERVER_IP}:5050/ws/frontend"

st.set_page_config(page_title="Smart Doorbell V2.2", page_icon="🔔", layout="wide")
st.markdown("<h1 style='color:#1E88E5;'>🔔 Smart Doorbell — Live Feed</h1>", unsafe_allow_html=True)

col_video, col_controls = st.columns([2, 1])

# ── VIDEO COLUMN (Renders ONCE, never refreshes) ───────────────────────
with col_video:
    st.markdown("### 🔴 Live Camera Stream")
    websocket_html = f"""
    <div style="background-color: #000; border-radius: 8px; overflow: hidden; border: 2px solid #333;">
        <img id="video_stream" style="width: 100%; height: auto; display: block;" src="https://via.placeholder.com/640x480.png?text=Waiting+for+Camera+Stream..." />
    </div>
    <script>
        var ws = new WebSocket("{WS_URL}");
        var img = document.getElementById("video_stream");
        
        ws.onmessage = function(event) {{
            if (event.data instanceof Blob) {{
                if (img.src && img.src.startsWith('blob:')) {{
                    URL.revokeObjectURL(img.src);
                }}
                var urlObject = URL.createObjectURL(event.data);
                img.src = urlObject;
            }}
        }};
        ws.onclose = function() {{
            img.src = "https://via.placeholder.com/640x480.png?text=Connection+Lost";
        }};
    </script>
    """
    # NOTE: You will get the terminal warning ONCE on boot instead of every second. Ignore it!
    components.html(websocket_html, height=500)


# ── STATUS COLUMN (Updates independently every 2 seconds) ───────────────
@st.fragment(run_every=2)
def show_status():
    st.markdown("### Door Status")
    try:
        r = requests.get(f"{HTTP_URL}/latest_event", timeout=2)
        if r.status_code == 200:
            event = r.json()
            status = event.get("status", "IDLE")
            name = event.get("name", "---")
            msg = event.get("message", "---")
            
            if status in ["SUCCESS", "MANUAL_ADMIT"]:
                st.success(f"✅ **{msg}**")
                st.info(f"Identified: {name}")
                
            elif status == "PENDING":
                st.warning(f"⚠️ **{msg}**")
                st.error("Unknown Visitor Detected!")
                c1, c2 = st.columns(2)
                with c1:
                    if st.button("✅ ADMIT", use_container_width=True):
                        requests.post(f"{HTTP_URL}/resolve_event", json={"action": "ADMIT"})
                with c2:
                    if st.button("🚫 DENY", use_container_width=True):
                        requests.post(f"{HTTP_URL}/resolve_event", json={"action": "DENY"})
                        
            elif status == "IDLE":
                st.info("System Online. Scanning video feed...")
            else:
                st.error(f"🚨 **{msg}**")
                
    except Exception as e:
        st.error(f"Cannot connect to server: {e}")

# Inject the fragment into the UI
with col_controls:
    show_status()