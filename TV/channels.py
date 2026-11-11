"""
Greek Television & Radio Channels and Transmitter Multiplexes Database
Provides frequencies, regional transmitters, and verified public broadcast streams.
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional

@dataclass
class Channel:
    id: str
    name: str
    greek_name: str
    category: str  # "Public TV", "Parliament TV", "Public Radio", "Free-to-Air TV", "Regional TV"
    description: str
    stream_url: str
    multiplex: str
    uhf_channel_default: int  # Default UHF channel number (e.g. 21 in Athens)
    freq_mhz_default: float   # Default Center frequency in MHz (e.g. 474.0 MHz)
    is_audio_only: bool = False
    badge_color: str = "#0066cc"

# UHF Channel calculation helper (European 8 MHz channel plan)
def uhf_to_freq(ch: int) -> float:
    """Calculate center frequency in MHz for European UHF TV channel 21-69 (8 MHz bandwidth)."""
    return 470.0 + (ch - 21) * 8.0 + 4.0

# Regional Transmitter Multiplex Mapping in Greece
REGIONS: Dict[str, Dict[str, any]] = {
    "attica": {
        "name": "Attica (Athens / Hymettus / Parnitha / Aegina)",
        "greek_name": "Αττική (Υμηττός / Πάρνηθα / Αίγινα)",
        "ert_mux_1_ch": 21,  # 474.0 MHz
        "ert_mux_2_ch": 28,  # 530.0 MHz
        "digea_1_ch": 22,    # 482.0 MHz
        "digea_2_ch": 27,    # 522.0 MHz
        "regional_ch": 29,   # 538.0 MHz
    },
    "thessaloniki": {
        "name": "Central Macedonia (Thessaloniki / Chortiatis / Filippeio)",
        "greek_name": "Κεντρική Μακεδονία (Χορτιάτης / Φιλίππειο)",
        "ert_mux_1_ch": 24,  # 498.0 MHz
        "ert_mux_2_ch": 28,  # 530.0 MHz
        "digea_1_ch": 29,    # 538.0 MHz
        "digea_2_ch": 31,    # 554.0 MHz
        "regional_ch": 30,   # 546.0 MHz
    },
    "patras": {
        "name": "Western Greece & Peloponnese (Patras / Panachaiko)",
        "greek_name": "Δυτική Ελλάδα (Πάτρα / Παναχαϊκό)",
        "ert_mux_1_ch": 24,  # 498.0 MHz
        "ert_mux_2_ch": 31,  # 554.0 MHz
        "digea_1_ch": 22,    # 482.0 MHz
        "digea_2_ch": 27,    # 522.0 MHz
        "regional_ch": 30,   # 546.0 MHz
    },
    "thessaly": {
        "name": "Thessaly (Larissa / Volos / Pelion / Dovroutsi)",
        "greek_name": "Θεσσαλία (Λάρισα / Βόλος / Πήλιο)",
        "ert_mux_1_ch": 21,  # 474.0 MHz
        "ert_mux_2_ch": 28,  # 530.0 MHz
        "digea_1_ch": 24,    # 498.0 MHz
        "digea_2_ch": 26,    # 514.0 MHz
        "regional_ch": 30,   # 546.0 MHz
    },
    "crete": {
        "name": "Crete (Heraklion / Chania / Rogdia / Malaxa)",
        "greek_name": "Κρήτη (Ηράκλειο / Χανιά / Ρογδιά)",
        "ert_mux_1_ch": 21,  # 474.0 MHz
        "ert_mux_2_ch": 24,  # 498.0 MHz
        "digea_1_ch": 23,    # 490.0 MHz
        "digea_2_ch": 25,    # 506.0 MHz
        "regional_ch": 29,   # 538.0 MHz
    },
    "epirus": {
        "name": "Epirus & Ionian (Ioannina / Corfu / Ligopsa)",
        "greek_name": "Ήπειρος & Ιόνιο (Ιωάννινα / Κέρκυρα)",
        "ert_mux_1_ch": 21,  # 474.0 MHz
        "ert_mux_2_ch": 28,  # 530.0 MHz
        "digea_1_ch": 22,    # 482.0 MHz
        "digea_2_ch": 25,    # 506.0 MHz
        "regional_ch": 29,   # 538.0 MHz
    },
}

# Master List of Greek Public & Free-to-Air Channels
GREEK_CHANNELS: List[Channel] = [
    # ERT MUX 1 (Public National Television & Parliament)
    Channel(
        id="ert1",
        name="ERT 1",
        greek_name="ΕΡΤ 1",
        category="Public TV",
        description="Flagship Hellenic Broadcasting Corporation public television channel. News, premium drama, current affairs, and cultural broadcasts.",
        stream_url="https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT1/default/index.m3u8",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        badge_color="#0d6efd",
    ),
    Channel(
        id="ert2",
        name="ERT 2",
        greek_name="ΕΡΤ 2",
        category="Public TV",
        description="Public culture, fine arts, educational programming, award-winning documentaries, children's content, and domestic sporting events.",
        stream_url="https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT2/default/index.m3u8",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        badge_color="#0dcaf0",
    ),
    Channel(
        id="ert3",
        name="ERT 3",
        greek_name="ΕΡΤ 3",
        category="Public TV",
        description="Public regional broadcaster based in Thessaloniki covering Northern Greece, regional communities, diaspora, culture, science, and ecology.",
        stream_url="https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT3/default/index.m3u8",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        badge_color="#198754",
    ),
    Channel(
        id="ert_news",
        name="ERT News",
        greek_name="ΕΡΤ News",
        category="Public TV",
        description="ERT's dedicated 24/7 continuous news network. Real-time domestic and international breaking news, debates, and analyses.",
        stream_url="https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERTNews/default/index.m3u8",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        badge_color="#dc3545",
    ),
    Channel(
        id="vouli",
        name="Vouli TV (Parliament)",
        greek_name="Βουλή Τηλεόραση",
        category="Parliament TV",
        description="Hellenic Parliament TV (Κανάλι της Βουλής). Live parliamentary plenary sessions, political debates, classical cinema, and academic lectures.",
        stream_url="https://ert-ucdn.broadpeak-aas.com/bpk-tv/VOULITV/default/index.m3u8",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        badge_color="#ffc107",
    ),
    Channel(
        id="ert_cosmos",
        name="ERT Cosmos",
        greek_name="ΕΡΤ Cosmos",
        category="Public TV",
        description="Global cultural and music network celebrating world culture, documentary filmmaking, arts, and Greek diaspora heritage.",
        stream_url="https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERTCosmos/default/index.m3u8",
        multiplex="ERT MUX 2",
        uhf_channel_default=28,
        freq_mhz_default=530.0,
        badge_color="#6f42c1",
    ),
    
    # Free-to-Air & Public Interest National Channels
    Channel(
        id="star",
        name="Star Channel",
        greek_name="Star Channel",
        category="Free-to-Air TV",
        description="Major Greek national commercial free-to-air broadcaster broadcasting entertainment, lifestyle, international series, and evening news.",
        stream_url="http://livestar.siliconweb.com/starvod/star4/star4.m3u8",
        multiplex="Digea MUX 2",
        uhf_channel_default=27,
        freq_mhz_default=522.0,
        badge_color="#fd7e14",
    ),
    Channel(
        id="naftemporiki",
        name="Naftemporiki TV",
        greek_name="Ναυτεμπορική TV",
        category="Free-to-Air TV",
        description="Leading Greek financial, maritime, economic, and business news television channel.",
        stream_url="https://telmaco.ascdn.broadpeak.io/nafteboriki/default/index.m3u8",
        multiplex="Regional MUX",
        uhf_channel_default=29,
        freq_mhz_default=538.0,
        badge_color="#20c997",
    ),
    
    # Regional Greek Public Interest Broadcasters
    Channel(
        id="vergina",
        name="Vergina TV",
        greek_name="Βεργίνα TV",
        category="Regional TV",
        description="Prominent regional channel broadcasting from Thessaloniki covering Central and Northern Greece.",
        stream_url="https://verginanews.gr:8443/hls_live/stream1.m3u8",
        multiplex="Regional MUX",
        uhf_channel_default=29,
        freq_mhz_default=538.0,
        badge_color="#6610f2",
    ),
    Channel(
        id="astra",
        name="Astra TV",
        greek_name="Άστρα TV",
        category="Regional TV",
        description="Regional television channel of Thessaly broadcasting local news, culture, and sports from Volos and Larissa.",
        stream_url="https://server.gointernet.gr/live/livestream.m3u8",
        multiplex="Regional MUX",
        uhf_channel_default=30,
        freq_mhz_default=546.0,
        badge_color="#0dcaf0",
    ),
    Channel(
        id="center",
        name="Center TV",
        greek_name="Center TV",
        category="Regional TV",
        description="Regional broadcaster in Eastern Macedonia and Thrace covering Kavala, Drama, Xanthi, and Alexandroupoli.",
        stream_url="https://eu1.streams.gr:8081/centertv/index.m3u8",
        multiplex="Regional MUX",
        uhf_channel_default=29,
        freq_mhz_default=538.0,
        badge_color="#198754",
    ),
    Channel(
        id="trt",
        name="TRT (Thessalia)",
        greek_name="TRT Θεσσαλία",
        category="Regional TV",
        description="Thessaly Radio Television covering Central Greece with regional investigative reports and municipal news.",
        stream_url="https://av.hellasnet.tv/rst/trt/main_stream.m3u8",
        multiplex="Regional MUX",
        uhf_channel_default=30,
        freq_mhz_default=546.0,
        badge_color="#d63384",
    ),

    # ERT Public Radio (Transmitted on same ERT DVB-T Multiplex)
    Channel(
        id="proto",
        name="ERT Proto Programma",
        greek_name="ΕΡΤ Πρώτο Πρόγραμμα",
        category="Public Radio",
        description="First Programme of Hellenic Public Radio. Comprehensive national news, public analysis, and political discussions.",
        stream_url="https://radiostreaming.ert.gr/ert-proto",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        is_audio_only=True,
        badge_color="#0d6efd",
    ),
    Channel(
        id="deftero",
        name="ERT Deftero Programma",
        greek_name="ΕΡΤ Δεύτερο Πρόγραμμα",
        category="Public Radio",
        description="Second Programme of Hellenic Public Radio. Dedicated to Greek contemporary song, traditional music, and cultural arts.",
        stream_url="https://radiostreaming.ert.gr/ert-deftero",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        is_audio_only=True,
        badge_color="#198754",
    ),
    Channel(
        id="trito",
        name="ERT Trito Programma",
        greek_name="ΕΡΤ Τρίτο Πρόγραμμα",
        category="Public Radio",
        description="Third Programme of Hellenic Public Radio. Prestigious classical music, opera, theatrical drama, and literary culture.",
        stream_url="https://radiostreaming.ert.gr/ert-trito",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        is_audio_only=True,
        badge_color="#ffc107",
    ),
    Channel(
        id="erasport",
        name="ERT ERA Sport",
        greek_name="ΕΡΤ ΕΡΑ Σπορ",
        category="Public Radio",
        description="National sports public radio station. Live sporting commentary, athletic analysis, and coverage of Greek leagues.",
        stream_url="https://radiostreaming.ert.gr/ert-erasport",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        is_audio_only=True,
        badge_color="#dc3545",
    ),
    Channel(
        id="kosmos",
        name="ERT Kosmos 93.6",
        greek_name="ΕΡΤ Kosmos 93.6",
        category="Public Radio",
        description="Public music station celebrating world music, reggae, jazz, afrobeat, indie, folk, and global roots.",
        stream_url="https://radiostreaming.ert.gr/ert-kosmos",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        is_audio_only=True,
        badge_color="#6f42c1",
    ),
    Channel(
        id="voice_of_greece",
        name="ERT Voice of Greece",
        greek_name="ΕΡΤ Η Φωνή της Ελλάδας",
        category="Public Radio",
        description="Shortwave and satellite international voice of Greece broadcasting to Greek diaspora communities worldwide.",
        stream_url="https://radiostreaming.ert.gr/ert-voiceofgreece",
        multiplex="ERT MUX 1",
        uhf_channel_default=21,
        freq_mhz_default=474.0,
        is_audio_only=True,
        badge_color="#0dcaf0",
    ),
]

def get_channel_by_id(channel_id: str) -> Optional[Channel]:
    for ch in GREEK_CHANNELS:
        if ch.id == channel_id:
            return ch
    return None

def get_channel_freq_for_region(channel: Channel, region_key: str) -> float:
    """Calculate the exact broadcast frequency (MHz) for a given channel in the selected Greek region."""
    if region_key not in REGIONS:
        return channel.freq_mhz_default
    
    r = REGIONS[region_key]
    mux = channel.multiplex.upper()
    if "ERT MUX 1" in mux:
        return uhf_to_freq(r["ert_mux_1_ch"])
    elif "ERT MUX 2" in mux:
        return uhf_to_freq(r["ert_mux_2_ch"])
    elif "DIGEA MUX 1" in mux:
        return uhf_to_freq(r["digea_1_ch"])
    elif "DIGEA MUX 2" in mux:
        return uhf_to_freq(r["digea_2_ch"])
    elif "REGIONAL" in mux:
        return uhf_to_freq(r["regional_ch"])
    return channel.freq_mhz_default

def generate_m3u8_playlist(region_key: str = "attica") -> str:
    """Generate an M3U8 playlist with frequencies and stream URLs."""
    lines = ["#EXTM3U"]
    for ch in GREEK_CHANNELS:
        freq = get_channel_freq_for_region(ch, region_key)
        lines.append(f'#EXTINF:-1 tvg-id="{ch.id}" tvg-name="{ch.name}" group-title="{ch.category}" radio="{"true" if ch.is_audio_only else "false"}",{ch.name} ({freq:.1f} MHz)')
        lines.append(ch.stream_url)
    return "\n".join(lines) + "\n"
