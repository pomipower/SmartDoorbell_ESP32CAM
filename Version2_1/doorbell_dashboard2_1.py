"""
Smart Doorbell — Admin Dashboard (Version 2.1)
==============================================
Streamlit frontend. Run alongside doorbell_server.py.

    streamlit run doorbell_dashboard2_1.py

Changes in Version 2.1 (Interactive Update):
  • Added interactive ADMIT / DENY buttons in Live tab for unknown visitors
  • Buttons resolve pending events via server's /resolve_event endpoint
  • Live tab shows visitor image fetched from server's /latest_image endpoint
  • Audit log renders image thumbnails from captures/ directory
  • CSV includes ImageFile column; robust fallback for older logs
  • Admin tab shows the captures/ folder size and management tools
"""

import streamlit as st
import pandas as pd
from PIL import Image
import os
import time
import requests
import csv

# ── Server address (must match doorbell_server.py) ───────────────────────────
SERVER_URL   = "http://127.0.0.1:5050"   # localhost — dashboard runs on same PC
LOG_FILE     = "security_log.csv"
CAPTURES_DIR = "captures"
LATEST_IMAGE = "latest_capture.jpg"

# ─────────────────────────────────────────────────────────────────────────────
st.set_page_config(
    page_title="Smart Doorbell — Admin",
    page_icon="🔔",
    layout="wide"
)

st.markdown("""
<style>
    .main-header { font-family:'Segoe UI',sans-serif; color:#1E88E5; font-weight:bold; }
    .status-success { background:#1b5e20; color:white; padding:8px 16px;
                      border-radius:8px; font-size:1.1rem; }
    .status-denied  { background:#b71c1c; color:white; padding:8px 16px;
                      border-radius:8px; font-size:1.1rem; }
    .status-failed  { background:#0d47a1; color:white; padding:8px 16px;
                      border-radius:8px; font-size:1.1rem; }
    div[data-testid="metric-container"] { background:#1e1e1e; border-radius:8px;
                                          padding:10px; }
</style>
""", unsafe_allow_html=True)

st.markdown("<h1 class='main-header'>🔔 Smart Doorbell — Admin Panel</h1>",
            unsafe_allow_html=True)

# ─────────────────────────────────────────────────────────────────────────────
#  Data loading
# ─────────────────────────────────────────────────────────────────────────────

#@st.cache_data(ttl=1.5)
def load_log() -> pd.DataFrame:
    """Load CSV audit log, tolerating both 4-column (old) and 5-column (new) formats."""
    if not os.path.exists(LOG_FILE):
        return pd.DataFrame(columns=["Timestamp","Name","Status","Message","ImageFile"])
    df = pd.read_csv(LOG_FILE)
    if "ImageFile" not in df.columns:
        df["ImageFile"] = ""   # Back-compat with old check-in log
    return df


#@st.cache_data(ttl=1.5)
def fetch_latest_event() -> dict:
    """Poll the server for the latest doorbell event."""
    try:
        r = requests.get(f"{SERVER_URL}/latest_event", timeout=2)
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return {}


def image_thumb(filename: str, width: int = 90) -> Image.Image | None:
    """Load a capture thumbnail from disk. Returns None if not found."""
    if not filename or pd.isna(filename):
        return None
    path = os.path.join(CAPTURES_DIR, str(filename))
    if os.path.exists(path):
        try:
            return Image.open(path).copy()
        except Exception:
            pass
    return None


# ─────────────────────────────────────────────────────────────────────────────
#  Tabs
# ─────────────────────────────────────────────────────────────────────────────
tab_live, tab_log, tab_admin = st.tabs(
    ["🔴  Live Feed", "📋  Audit Log", "⚙️  Admin"]
)

df = load_log()
event = fetch_latest_event()

# ══════════════════════════════════════════════════════════════
#  TAB 1 — LIVE FEED
# ══════════════════════════════════════════════════════════════
with tab_live:
    col_img, col_status = st.columns([2, 1])

    # ── Latest visitor image ──────────────────────────────────────
    with col_img:
        st.markdown("### Latest Visitor")
        if os.path.exists(LATEST_IMAGE):
            try:
                with Image.open(LATEST_IMAGE) as img:
                    st.image(img.copy(), width='stretch', channels="BGR")
            except Exception:
                st.info("Image corrupted or still being written — refreshing...")
        else:
            st.info("No captures yet. Waiting for first doorbell press...")

    # ── Status panel ─────────────────────────────────────────────
    with col_status:
        st.markdown("### Door Status")

        if event and event.get("status") != "IDLE":
            status  = event.get("status", "")
            name    = event.get("name", "Unknown")
            message = event.get("message", "")
            ts      = event.get("timestamp", "")

            if status in ["SUCCESS", "MANUAL_ADMIT"]:
                st.markdown(f"<div class='status-success'>✅ {message}</div>", unsafe_allow_html=True)
                st.success(f"Visitor: **{name}**")
                
            elif status == "PENDING":
                st.markdown(f"<div class='status-failed' style='background:#f57c00;'>⚠️ {message}</div>", unsafe_allow_html=True)
                st.warning("Action Required!")
                
                # Render interactive buttons side-by-side
                c1, c2 = st.columns(2)
                with c1:
                    if st.button("✅ ADMIT", use_container_width=True):
                        requests.post(f"{SERVER_URL}/resolve_event", json={"action": "ADMIT"})
                        time.sleep(0.5)
                        st.rerun()
                with c2:
                    if st.button("🚫 DENY", use_container_width=True):
                        requests.post(f"{SERVER_URL}/resolve_event", json={"action": "DENY"})
                        time.sleep(0.5)
                        st.rerun()
                        
            elif status in ["DENIED", "MANUAL_DENY"]:
                st.markdown(f"<div class='status-denied'>🚨 ACCESS DENIED</div>", unsafe_allow_html=True)
                st.error(f"Unknown visitor at **{ts}**")

            st.caption(f"Last ring: {ts}")

        elif not df.empty:
            # Fallback: use CSV last row if server event not available
            last = df.iloc[-1]
            if last["Status"] == "SUCCESS":
                st.success(f"✅ {last['Message']} ({last['Name']})")
            elif last["Status"] == "DENIED":
                st.error(f"🚨 INTRUDER: {last['Timestamp']}")
            else:
                st.warning("⚠️ Scan failed — no face detected")
        else:
            st.info("No events recorded yet.")

        st.divider()
        st.markdown("### Quick Metrics")
        total   = len(df)
        success = len(df[df["Status"] == "SUCCESS"])
        denied  = len(df[df["Status"] == "DENIED"])

        st.metric("Total Rings",    total)
        st.metric("Authorised",     success)
        st.metric("Denied/Failed",  total - success, delta_color="inverse")


# ══════════════════════════════════════════════════════════════
#  TAB 2 — AUDIT LOG
# ══════════════════════════════════════════════════════════════
with tab_log:
    st.markdown("### System Telemetry")

    # Metrics row
    total       = len(df)
    success_cnt = len(df[df["Status"] == "SUCCESS"])
    denied_cnt  = len(df[df["Status"] == "DENIED"])
    failed_cnt  = len(df[df["Status"] == "FAILED"])
    rate        = round(success_cnt / total * 100, 1) if total > 0 else 0

    m1, m2, m3, m4 = st.columns(4)
    m1.metric("Total Rings",        total)
    m2.metric("Authorised Access",  success_cnt)
    m3.metric("Denied",             denied_cnt, delta_color="inverse")
    m4.metric("Auth Rate",          f"{rate}%")

    st.divider()

    # ── Full log table ────────────────────────────────────────────
    st.markdown("### Audit Trail")

    # Display table without ImageFile column (it's shown as thumbnails below)
    display_df = df.sort_values("Timestamp", ascending=False).drop(
        columns=["ImageFile"], errors="ignore"
    )
    st.dataframe(display_df, width="stretch")

    st.divider()

    # ── Thumbnail gallery — last 12 captures ──────────────────────
    st.markdown("### Recent Captures (last 12)")

    recent = df.sort_values("Timestamp", ascending=False).head(12)

    # Render 4 thumbnails per row
    cols_per_row = 4
    rows = [recent.iloc[i:i+cols_per_row] for i in range(0, len(recent), cols_per_row)]

    for row_df in rows:
        cols = st.columns(cols_per_row)
        for col, (_, entry) in zip(cols, row_df.iterrows()):
            with col:
                thumb = image_thumb(entry.get("ImageFile", ""))
                if thumb:
                    # Colour border based on status
                    border_colour = {
                        "SUCCESS": "#1b5e20",
                        "DENIED" : "#b71c1c",
                        "FAILED" : "#0d47a1"
                    }.get(entry["Status"], "#333")

                    st.markdown(
                        f"<div style='border:3px solid {border_colour};"
                        f"border-radius:6px;overflow:hidden;margin-bottom:4px'>",
                        unsafe_allow_html=True
                    )
                    st.image(thumb, width="stretch")
                    st.markdown("</div>", unsafe_allow_html=True)
                else:
                    st.markdown("*(no image)*")

                # Caption
                status_emoji = {"SUCCESS":"✅","DENIED":"🚫","FAILED":"⚠️"}.get(
                    entry["Status"], "❓"
                )
                st.caption(
                    f"{status_emoji} **{entry['Name']}**\n\n"
                    f"{entry['Timestamp']}"
                )


# ══════════════════════════════════════════════════════════════
#  TAB 3 — ADMIN
# ══════════════════════════════════════════════════════════════
with tab_admin:
    st.markdown("### Database Management")

    # ── Captures folder stats ─────────────────────────────────────
    if os.path.exists(CAPTURES_DIR):
        files  = [f for f in os.listdir(CAPTURES_DIR) if f.endswith(".jpg")]
        sizes  = [os.path.getsize(os.path.join(CAPTURES_DIR, f)) for f in files]
        total_mb = sum(sizes) / 1_048_576
        st.info(
            f"📁 `captures/`  —  **{len(files)} images**  |  "
            f"**{total_mb:.1f} MB** on disk"
        )
    else:
        st.info("No captures directory found yet.")

    st.divider()

    # ── Delete log ────────────────────────────────────────────────
    st.warning("⚠️ **Danger Zone** — these actions cannot be undone.")

    col_a, col_b = st.columns(2)

    with col_a:
        if st.button("🗑️  Erase Audit Log (CSV only)"):
            if os.path.exists(LOG_FILE):
                #os.remove(LOG_FILE)
                with open(LOG_FILE, mode='w', newline='') as f:
                    csv.writer(f).writerow(["Timestamp", "Name", "Status", "Message", "ImageFile"])
                st.success("CSV log wiped. A fresh one will be created on the next ring.")
                time.sleep(1)
                st.rerun()
            else:
                st.info("No log file exists.")

    with col_b:
        if st.button("🗑️  Erase ALL Captures + Log"):
            removed = 0
            if os.path.exists(CAPTURES_DIR):
                for f in os.listdir(CAPTURES_DIR):
                    os.remove(os.path.join(CAPTURES_DIR, f))
                    removed += 1
            if os.path.exists(LOG_FILE):
                with open(LOG_FILE, mode='w', newline='') as f:
                    csv.writer(f).writerow(["Timestamp", "Name", "Status", "Message", "ImageFile"])
            if os.path.exists(LATEST_IMAGE):
                os.remove(LATEST_IMAGE)
            st.success(f"Wiped {removed} capture(s) and the log.")
            time.sleep(1)
            st.rerun()

    st.divider()
    st.markdown("### Server Connection")
    try:
        r = requests.get(f"{SERVER_URL}/latest_event", timeout=2)
        if r.status_code == 200:
            st.success(f"✅ Server reachable at `{SERVER_URL}`")
        else:
            st.error(f"Server returned HTTP {r.status_code}")
    except Exception as e:
        st.error(f"❌ Cannot reach server: {e}")

# ─────────────────────────────────────────────────────────────────────────────
#  Auto-refresh (placed last so it loops the full page)
# ─────────────────────────────────────────────────────────────────────────────
time.sleep(2)
st.rerun()
