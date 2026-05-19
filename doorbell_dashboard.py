import streamlit as st
import pandas as pd
from PIL import Image
import os
import time

# --- CONFIGURATION ---
st.set_page_config(page_title="Smart Doorbell Admin", page_icon="🚪", layout="wide")

# Custom CSS for a clean, modern, smart-home aesthetic
st.markdown("""
    <style>
    .main-header { font-family: 'Segoe UI', sans-serif; color: #2E86C1; font-weight: 800; }
    .metric-value { font-size: 2rem; font-weight: bold; color: #333; }
    .status-granted { color: #27AE60; font-weight: bold; }
    .status-denied { color: #E74C3C; font-weight: bold; }
    </style>
""", unsafe_allow_html=True)

st.markdown("<h1 class='main-header'>🚪 Smart Doorbell Command Center</h1>", unsafe_allow_html=True)

LOG_FILE = "security_log.csv"
IMAGE_PATH = "debug_latest_photo.jpg"

# Safely load the CSV database into a Pandas Dataframe
@st.cache_data(ttl=1) # Cache clears every 1 second to fetch fresh server data
def load_data():
    if os.path.exists(LOG_FILE):
        return pd.read_csv(LOG_FILE)
    return pd.DataFrame(columns=["Timestamp", "Name", "Status", "Message"])

df = load_data()

# Create 3 modern tabs for UI navigation
tab_live, tab_analytics, tab_admin = st.tabs(["🔴 Live Front Door", "📊 Visitor Analytics", "⚙️ System Settings"])

with tab_live:
    col1, col2 = st.columns([2, 1])
    
    with col1:
        st.markdown("### Latest Visitor")
        if os.path.exists(IMAGE_PATH):
            try:
                with Image.open(IMAGE_PATH) as img:
                    # Streamlit handles BGR to RGB conversion automatically if needed, 
                    # but OpenCV writes as BGR. We'll let Streamlit render it directly.
                    st.image(img.copy(), width='stretch', channels="BGR")
            except Exception as e:
                st.warning(f"Error loading image: {e}")
        else:
            st.info("Awaiting first doorbell ring...")

    with col2:
        st.markdown("### Instant Access Status")
        # Display an alert animation based on the very last entry in the database
        if not df.empty:
            last_entry = df.iloc[-1]
            if last_entry["Status"] == "SUCCESS":
                st.success(f"✅ Access Granted\n\n**{last_entry['Name']}** is at the door.\n\nThe door latch was engaged automatically.")
            elif last_entry["Status"] == "DENIED":
                st.error(f"🚨 Unknown Visitor\n\nAn unrecognized person rang the doorbell at {last_entry['Timestamp']}.\n\nThe door remains locked.")
            else:
                st.warning("⚠️ Scan failed. No face detected at the door.")
        else:
            st.write("No visitors recorded yet.")

with tab_analytics:
    st.markdown("### Access Telemetry")
    
    # Calculate live metrics directly from the dataframe
    total_rings = len(df)
    success_count = len(df[df["Status"] == "SUCCESS"])
    denied_count = len(df[df["Status"] == "DENIED"])
    success_rate = round((success_count / total_rings * 100), 1) if total_rings > 0 else 0

    # Display Metrics in a modern 4-column layout
    m1, m2, m3, m4 = st.columns(4)
    m1.metric("Total Doorbell Rings", total_rings)
    m2.metric("Authorized Entries", success_count)
    m3.metric("Unknown Visitors", denied_count, delta_color="inverse")
    m4.metric("Known Visitor Rate", f"{success_rate}%")

    st.markdown("---")
    st.markdown("### Detailed Visitor Log")
    # Display the full database table, newest records first
    st.dataframe(df.sort_values(by="Timestamp", ascending=False), width='stretch')

with tab_admin:
    st.markdown("### Database Management")
    st.warning("Warning: This action is permanent. It will delete all visitor history and the latest captured image.")
    
    # Button to physically delete the CSV file and the last photo
    if st.button("🗑️ Erase All Visitor Logs"):
        deleted = False
        if os.path.exists(LOG_FILE):
            os.remove(LOG_FILE)
            deleted = True
        if os.path.exists(IMAGE_PATH):
            os.remove(IMAGE_PATH)
            deleted = True
            
        if deleted:
            st.success("System wiped successfully. Awaiting new visitors.")
            time.sleep(1) # Brief pause so the user sees the success message
            st.rerun()
        else:
            st.info("System is already clean.")

# Auto-refresh loop
# Streamlit continuously reads the file system and updates the UI
time.sleep(1.5)
st.rerun()