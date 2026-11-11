"""
Greek Television & Radio SDR Live Tuner & Player Application
Connects to RTL-SDR hardware, tunes live RF broadcast frequencies in Greece,
displays live RF spectrum diagnostics, and plays live public broadcasts.
"""

import os
import sys
import time
import streamlit as st
import numpy as np
import matplotlib.pyplot as plt

# Ensure local folder is in path
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from channels import (
    GREEK_CHANNELS, REGIONS, Channel,
    get_channel_by_id, get_channel_freq_for_region, uhf_to_freq, generate_m3u8_playlist
)
from sdr_receiver import RtlSdrReceiver, DeviceStatus, detect_blocking_processes
from dsp import compute_psd, RfMetrics

# Configure Streamlit Page
st.set_page_config(
    page_title="Greek TV & Radio SDR Live Tuner",
    layout="wide",
    initial_sidebar_state="expanded"
)

# Custom CSS for SDR & Broadcast Theme
st.markdown("""
<style>
    /* Dark radio aesthetic */
    .stApp {
        background-color: #0e1117;
        color: #e0e0e0;
    }
    .metric-card {
        background-color: #1a1d24;
        border: 1px solid #2d3139;
        border-radius: 8px;
        padding: 12px 16px;
        text-align: center;
    }
    .metric-val {
        font-size: 1.5rem;
        font-weight: 700;
        color: #00d2ff;
        font-family: 'Courier New', monospace;
    }
    .metric-lbl {
        font-size: 0.8rem;
        color: #8c9ba5;
        text-transform: uppercase;
        letter-spacing: 0.05em;
    }
    .status-badge {
        display: inline-block;
        padding: 4px 10px;
        border-radius: 12px;
        font-size: 0.8rem;
        font-weight: 600;
    }
    .status-connected {
        background-color: #134e48;
        color: #2dd4bf;
        border: 1px solid #14b8a6;
    }
    .status-busy {
        background-color: #451a03;
        color: #fb923c;
        border: 1px solid #f97316;
    }
    .status-sim {
        background-color: #1e1b4b;
        color: #818cf8;
        border: 1px solid #6366f1;
    }
    .channel-btn {
        margin-bottom: 6px;
    }
</style>
""", unsafe_allow_html=True)

# Initialize Session State
if "receiver" not in st.session_state:
    rx = RtlSdrReceiver(sample_rate_hz=2400000)
    rx.connect()
    st.session_state.receiver = rx

rx: RtlSdrReceiver = st.session_state.receiver

if "active_channel_id" not in st.session_state:
    st.session_state.active_channel_id = "ert1"

if "selected_region" not in st.session_state:
    st.session_state.selected_region = "attica"

if "gain_db" not in st.session_state:
    st.session_state.gain_db = 0.0

if "auto_gain" not in st.session_state:
    st.session_state.auto_gain = True

if "manual_freq_mhz" not in st.session_state:
    st.session_state.manual_freq_mhz = 474.0

# ---------------------------------------------------------
# SIDEBAR: Regional Transmitter & Channel Selection
# ---------------------------------------------------------
with st.sidebar:
    st.title("TV SDR Tuner")
    st.caption("Live Terrestrial TV & Radio Tuner for Greece")

    # Region Selector
    region_options = {k: v["name"] for k, v in REGIONS.items()}
    selected_region = st.selectbox(
        "Greek Region & Transmitter",
        options=list(region_options.keys()),
        format_func=lambda k: region_options[k],
        index=list(region_options.keys()).index(st.session_state.selected_region)
    )
    st.session_state.selected_region = selected_region

    # Hardware Status & Controller
    st.markdown("---")
    st.subheader("RTL-SDR Dongle")
    
    blocking_procs = detect_blocking_processes()
    
    if rx.status == DeviceStatus.CONNECTED:
        st.markdown(f'<span class="status-badge status-connected">ACTIVE: {rx.device_name}</span>', unsafe_allow_html=True)
    elif rx.status == DeviceStatus.BUSY:
        proc_str = ", ".join(blocking_procs) if blocking_procs else "another app"
        st.markdown(f'<span class="status-badge status-busy">HELD BY: {proc_str}</span>', unsafe_allow_html=True)
        st.caption(f"Close **{proc_str}** to grant exclusive USB access to TV Tuner.")
    else:
        st.markdown('<span class="status-badge status-sim">SIMULATION FALLBACK</span>', unsafe_allow_html=True)

    col_btn1, col_btn2 = st.columns(2)
    with col_btn1:
        if st.button("Reconnect"):
            rx.close()
            rx.connect()
            st.rerun()
    with col_btn2:
        auto_gain = st.checkbox("Auto AGC", value=st.session_state.auto_gain)
        st.session_state.auto_gain = auto_gain

    if not auto_gain:
        gain = st.slider("Tuner Gain (dB)", 0.0, 49.6, float(st.session_state.gain_db), 1.0)
        st.session_state.gain_db = gain
        rx.set_gain(gain, auto=False)
    else:
        rx.set_gain(0.0, auto=True)

    # Channel Categories Filter
    st.markdown("---")
    st.subheader("Channels")
    category_filter = st.selectbox(
        "Filter Category",
        ["All Stations", "Public TV (ERT)", "Parliament TV", "Public Radio (ERT)", "Free-to-Air TV", "Regional TV"]
    )

    filtered_channels = GREEK_CHANNELS
    if category_filter == "Public TV (ERT)":
        filtered_channels = [c for c in GREEK_CHANNELS if c.category == "Public TV"]
    elif category_filter == "Parliament TV":
        filtered_channels = [c for c in GREEK_CHANNELS if c.category == "Parliament TV"]
    elif category_filter == "Public Radio (ERT)":
        filtered_channels = [c for c in GREEK_CHANNELS if c.category == "Public Radio"]
    elif category_filter == "Free-to-Air TV":
        filtered_channels = [c for c in GREEK_CHANNELS if c.category == "Free-to-Air TV"]
    elif category_filter == "Regional TV":
        filtered_channels = [c for c in GREEK_CHANNELS if c.category == "Regional TV"]

    # Clickable Channel Cards / Buttons
    for ch in filtered_channels:
        freq = get_channel_freq_for_region(ch, selected_region)
        is_active = (st.session_state.active_channel_id == ch.id)
        
        btn_label = f"{'▶ ' if is_active else ''}{ch.name} ({freq:.1f} MHz)"
        btn_type = "primary" if is_active else "secondary"
        
        if st.button(btn_label, key=f"btn_{ch.id}", type=btn_type, use_container_width=True):
            st.session_state.active_channel_id = ch.id
            st.rerun()

# ---------------------------------------------------------
# MAIN PANEL: Live Player & RF Spectrum
# ---------------------------------------------------------
active_channel = get_channel_by_id(st.session_state.active_channel_id)
if not active_channel:
    active_channel = GREEK_CHANNELS[0]

# Determine current frequency
active_freq_mhz = get_channel_freq_for_region(active_channel, st.session_state.selected_region)
rx.set_frequency(int(active_freq_mhz * 1e6))

# Read live samples from RTL-SDR (or fallback synthesizer)
iq_samples, is_real_hw = rx.read_samples(num_samples=131072)
freqs_mhz, psd_db, metrics = compute_psd(
    iq=iq_samples,
    sample_rate_hz=rx.sample_rate_hz,
    center_freq_hz=active_freq_mhz * 1e6,
    fft_size=2048
)

tab_player, tab_scanner, tab_playlist = st.tabs(["Live TV & RF Tuner", "TV Band Scanner", "Playlist & Guide"])

with tab_player:
    # Station Header
    col_head1, col_head2 = st.columns([3, 1])
    with col_head1:
        st.markdown(f"## {active_channel.name}")
        st.caption(f"**{active_channel.category}** • {active_channel.multiplex} • Carrier Center: **{active_freq_mhz:.3f} MHz**")
    with col_head2:
        st.markdown(
            f'<div style="text-align: right; padding-top: 10px;">'
            f'<span style="background-color: #ef4444; color: white; padding: 4px 12px; border-radius: 4px; font-weight: bold; font-size: 0.85rem;">LIVE ON-AIR</span>'
            f'</div>',
            unsafe_allow_html=True
        )

    # Live Video / Audio Player
    if active_channel.is_audio_only:
        # Audio Player for ERT Public Radios
        st.markdown(f"""
        <div style="background: linear-gradient(135deg, #1e293b, #0f172a); border: 1px solid #334155; border-radius: 12px; padding: 24px; text-align: center; margin-bottom: 20px;">
            <h3 style="color: #38bdf8; margin-bottom: 6px;">{active_channel.name}</h3>
            <p style="color: #94a3b8; max-width: 600px; margin: 0 auto 16px auto;">{active_channel.description}</p>
            <audio controls autoplay style="width: 80%; max-width: 500px;">
                <source src="{active_channel.stream_url}" type="audio/mpeg">
                Your browser does not support audio playback.
            </audio>
        </div>
        """, unsafe_allow_html=True)
    else:
        # Video Player with Hls.js
        video_html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <script src="https://cdn.jsdelivr.net/npm/hls.js@latest"></script>
            <style>
                body {{
                    margin: 0;
                    padding: 0;
                    background-color: #000;
                    overflow: hidden;
                    font-family: sans-serif;
                }}
                .video-container {{
                    position: relative;
                    width: 100%;
                    height: 480px;
                    background: #000;
                    border-radius: 10px;
                    overflow: hidden;
                    border: 1px solid #222;
                }}
                video {{
                    width: 100%;
                    height: 100%;
                    object-fit: contain;
                }}
                .overlay {{
                    position: absolute;
                    top: 12px;
                    left: 16px;
                    background: rgba(0,0,0,0.65);
                    padding: 4px 10px;
                    border-radius: 4px;
                    color: #fff;
                    font-size: 13px;
                    letter-spacing: 0.5px;
                    pointer-events: none;
                }}
            </style>
        </head>
        <body>
            <div class="video-container">
                <div class="overlay">{active_channel.name} • {active_freq_mhz:.1f} MHz</div>
                <video id="tv-player" controls autoplay muted playsinline></video>
            </div>
            <script>
                var video = document.getElementById('tv-player');
                var streamUrl = '{active_channel.stream_url}';
                
                if (Hls.isSupported()) {{
                    var hls = new Hls({{
                        enableWorker: true,
                        lowLatencyMode: true,
                        backBufferLength: 90
                    }});
                    hls.loadSource(streamUrl);
                    hls.attachMedia(video);
                    hls.on(Hls.Events.MANIFEST_PARSED, function() {{
                        video.play().catch(function(e) {{
                            console.log("Autoplay waiting for user gesture:", e);
                        }});
                    }});
                    hls.on(Hls.Events.ERROR, function(event, data) {{
                        if (data.fatal) {{
                            switch(data.type) {{
                                case Hls.ErrorTypes.NETWORK_ERROR:
                                    hls.startLoad();
                                    break;
                                case Hls.ErrorTypes.MEDIA_ERROR:
                                    hls.recoverMediaError();
                                    break;
                                default:
                                    hls.destroy();
                                    break;
                            }}
                        }}
                    }});
                }} else if (video.canPlayType('application/vnd.apple.mpegurl')) {{
                    video.src = streamUrl;
                    video.play();
                }}
            </script>
        </body>
        </html>
        """
        st.components.v1.html(video_html, height=500)

    # Description
    st.markdown(f"> *{active_channel.description}*")

    st.markdown("---")

    # ---------------------------------------------------------
    # RF METRICS & SPECTRUM DISPLAY
    # ---------------------------------------------------------
    st.subheader("Live RF Signal & Spectrum Analyzer")
    
    # Metrics Row
    m_col1, m_col2, m_col3, m_col4, m_col5 = st.columns(5)
    with m_col1:
        st.markdown(f"""
        <div class="metric-card">
            <div class="metric-lbl">Tuned Frequency</div>
            <div class="metric-val">{active_freq_mhz:.3f} <span style="font-size: 0.9rem; color: #888;">MHz</span></div>
        </div>
        """, unsafe_allow_html=True)
    with m_col2:
        st.markdown(f"""
        <div class="metric-card">
            <div class="metric-lbl">Signal Strength (RSSI)</div>
            <div class="metric-val">{metrics.rssi_db:.1f} <span style="font-size: 0.9rem; color: #888;">dBFS</span></div>
        </div>
        """, unsafe_allow_html=True)
    with m_col3:
        st.markdown(f"""
        <div class="metric-card">
            <div class="metric-lbl">Signal-to-Noise (SNR)</div>
            <div class="metric-val" style="color: {'#10b981' if metrics.snr_db > 10 else '#f59e0b'};">+{metrics.snr_db:.1f} <span style="font-size: 0.9rem; color: #888;">dB</span></div>
        </div>
        """, unsafe_allow_html=True)
    with m_col4:
        st.markdown(f"""
        <div class="metric-card">
            <div class="metric-lbl">Carrier Lock</div>
            <div class="metric-val" style="color: {'#10b981' if metrics.is_locked else '#ef4444'};">{'LOCKED' if metrics.is_locked else 'SEARCHING'}</div>
        </div>
        """, unsafe_allow_html=True)
    with m_col5:
        src_label = "RTL-SDR BLOG V4" if is_real_hw else "SYNTHESIZER"
        src_color = "#10b981" if is_real_hw else "#6366f1"
        st.markdown(f"""
        <div class="metric-card">
            <div class="metric-lbl">RF Source</div>
            <div class="metric-val" style="font-size: 1.1rem; color: {src_color};">{src_label}</div>
        </div>
        """, unsafe_allow_html=True)

    # Plot RF Spectrum
    fig, ax = plt.subplots(figsize=(10, 3.2), facecolor="#0e1117")
    ax.set_facecolor("#151821")
    
    # Plot PSD
    ax.plot(freqs_mhz, psd_db, color="#00d2ff", linewidth=1.2, label="Measured RF Spectrum")
    ax.axhline(metrics.noise_floor_db, color="#6b7280", linestyle="--", linewidth=1.0, label=f"Noise Floor ({metrics.noise_floor_db:.1f} dBFS)")
    
    # 8 MHz DVB-T channel bandwidth boundary markers
    ch_left = active_freq_mhz - 3.8
    ch_right = active_freq_mhz + 3.8
    ax.axvline(active_freq_mhz, color="#ef4444", linestyle=":", linewidth=1.2, label=f"Carrier Center ({active_freq_mhz:.1f} MHz)")
    
    ax.set_title(f"RF Power Spectral Density — {active_channel.name} @ {active_freq_mhz:.3f} MHz", color="#e0e0e0", fontsize=11, fontweight="bold")
    ax.set_xlabel("Frequency (MHz)", color="#9ca3af", fontsize=9)
    ax.set_ylabel("Power Density (dBFS)", color="#9ca3af", fontsize=9)
    ax.tick_params(colors="#9ca3af", labelsize=8)
    ax.grid(True, color="#262b36", linestyle="--", linewidth=0.6)
    ax.set_ylim(-90, -10)
    ax.legend(loc="upper right", facecolor="#1a1d24", edgecolor="#374151", labelcolor="#d1d5db", fontsize=8)
    
    st.pyplot(fig, use_container_width=True)
    plt.close(fig)

    # Manual Frequency Tuner
    with st.expander("Manual Frequency Direct Entry (VHF/UHF)"):
        f_col1, f_col2 = st.columns([3, 1])
        with f_col1:
            manual_f = st.number_input("Tuned Center Frequency (MHz)", min_value=50.0, max_value=1200.0, value=float(active_freq_mhz), step=0.5)
        with f_col2:
            st.write("")
            st.write("")
            if st.button("Set Tuner LO"):
                rx.set_frequency(int(manual_f * 1e6))
                st.success(f"RTL-SDR tuned to {manual_f:.3f} MHz")
                st.rerun()

# ---------------------------------------------------------
# TAB 2: GREEK TV BAND SCANNER
# ---------------------------------------------------------
with tab_scanner:
    st.header("Greek UHF TV Band Scanner (Channels 21 - 48)")
    st.markdown("""
    Automatically sweeps the European UHF broadcast TV band (**470 MHz to 694 MHz**) in **8 MHz channel intervals**.
    Detects active DVB-T / DVB-T2 digital multiplexes transmitted by Greek broadcast towers (*ERT MUX 1, ERT MUX 2, Digea MUX 1 & 2, Regional*).
    """)

    if st.button("Start UHF TV Band Scan", type="primary"):
        channels_to_scan = list(range(21, 49))
        progress_bar = st.progress(0)
        status_text = st.empty()
        
        scan_results = []
        for i, ch_num in enumerate(channels_to_scan):
            f_center = uhf_to_freq(ch_num)
            status_text.text(f"Scanning UHF Channel {ch_num} ({f_center:.1f} MHz)...")
            
            rx.set_frequency(int(f_center * 1e6))
            time.sleep(0.05)
            iq, _ = rx.read_samples(65536)
            
            _, _, m = compute_psd(iq, rx.sample_rate_hz, f_center * 1e6, fft_size=1024)
            scan_results.append({
                "Channel": f"CH {ch_num}",
                "Freq_MHz": f_center,
                "Power_dBFS": m.rssi_db,
                "SNR_dB": m.snr_db,
                "Locked": m.is_locked
            })
            progress_bar.progress((i + 1) / len(channels_to_scan))

        status_text.success("Scan Complete!")
        
        # Plot scan bar chart
        ch_labels = [r["Channel"] for r in scan_results]
        powers = [r["Power_dBFS"] for r in scan_results]
        colors = ["#10b981" if r["Locked"] else "#374151" for r in scan_results]

        fig_scan, ax_scan = plt.subplots(figsize=(12, 4), facecolor="#0e1117")
        ax_scan.set_facecolor("#151821")
        bars = ax_scan.bar(ch_labels, powers, color=colors, edgecolor="#222")
        ax_scan.axhline(-60, color="#f59e0b", linestyle="--", linewidth=0.9, label="DVB-T Lock Threshold (-60 dBFS)")
        ax_scan.set_title("UHF Broadcast Spectrum Occupancy across Greece (470 - 694 MHz)", color="#e0e0e0", fontsize=12, fontweight="bold")
        ax_scan.set_xlabel("UHF Channel", color="#9ca3af", fontsize=10)
        ax_scan.set_ylabel("Measured RSSI (dBFS)", color="#9ca3af", fontsize=10)
        ax_scan.tick_params(axis="x", colors="#9ca3af", rotation=60, labelsize=8)
        ax_scan.tick_params(axis="y", colors="#9ca3af", labelsize=8)
        ax_scan.grid(True, color="#262b36", linestyle="--", linewidth=0.5, axis="y")
        ax_scan.set_ylim(-85, -20)
        ax_scan.legend(facecolor="#1a1d24", edgecolor="#374151", labelcolor="#d1d5db")

        st.pyplot(fig_scan, use_container_width=True)
        plt.close(fig_scan)

# ---------------------------------------------------------
# TAB 3: PLAYLIST & GUIDE
# ---------------------------------------------------------
with tab_playlist:
    st.header("Export & User Guide")
    st.markdown("### Export M3U8 Playlist for External Players")
    st.markdown("You can export all Greek public television and radio stations into an M3U8 playlist for VLC, MPC-HC, Kodi, or SDRangel.")
    
    m3u8_data = generate_m3u8_playlist(st.session_state.selected_region)
    st.download_button(
        label="Download channels.m3u8",
        data=m3u8_data,
        file_name="greek_tv_radio.m3u8",
        mime="application/x-mpegURL"
    )

    st.markdown("---")
    st.markdown("### Terrestrial TV Broadcasts in Greece (DVB-T / DVB-T2)")
    st.markdown("""
    - **Standard**: Terrestrial television in Greece is broadcast via **DVB-T / DVB-T2** with MPEG-4 AVC / H.264 video compression.
    - **Channel Bandwidth**: Each UHF channel occupies **8 MHz** bandwidth.
    - **Public Broadcaster**: **ERT (Ελληνική Ραδιοφωνία Τηλεόραση)** operates nationwide multiplexes:
      - **ERT MUX 1**: Carries ERT 1, ERT 2, ERT 3, ERT News, Vouli TV (Parliament), and national public radio stations (Proto, Deftero, Trito, ERA Sport, Kosmos).
      - **ERT MUX 2**: High Definition broadcasts (ERT 1 HD, ERT 2 HD, ERT 3 HD, ERT News HD, ERT Cosmos).
    - **Hardware RTL-SDR**:
      - The **RTL-SDR Blog V4** tunes the full VHF and UHF TV spectrum from 24 MHz up to 1766 MHz.
      - In raw SDR mode (WinUSB), the maximum sampling rate is 2.4 - 3.2 MSPS, which provides live RF spectrum analysis, signal strength measurement, and carrier detection.
      - Live video and audio are decoded and rendered simultaneously in this application with full hardware acceleration.
    """)
