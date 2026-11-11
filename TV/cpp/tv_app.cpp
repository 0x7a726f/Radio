#include "tv_demod.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <vector>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// Window Dimensions
#define DEFAULT_WIN_WIDTH 1200
#define DEFAULT_WIN_HEIGHT 740
#define SIDEBAR_WIDTH 320
#define TIMER_ID_DSP 1
#define TIMER_INTERVAL_MS 33 // ~30 FPS

// Channels and Regional Database (100% English text, no emojis)
struct ChannelDef {
    const char* id;
    const char* name;
    const char* category;
    const char* multiplex;
    int uhf_ch_default;
    double freq_mhz_default;
    const char* stream_url;
    bool is_audio_only;
};

static const ChannelDef G_CHANNELS[] = {
    // Public National Television
    { "ert1", "ERT 1", "Public TV", "ERT MUX 1", 21, 474.0, "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT1/default/index.m3u8", false },
    { "ert2", "ERT 2", "Public TV", "ERT MUX 1", 21, 474.0, "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT2/default/index.m3u8", false },
    { "ert3", "ERT 3", "Public TV", "ERT MUX 1", 21, 474.0, "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT3/default/index.m3u8", false },
    { "ert_news", "ERT News", "Public TV", "ERT MUX 1", 21, 474.0, "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERTNews/default/index.m3u8", false },
    { "vouli", "Parliament TV", "Parliament TV", "ERT MUX 1", 21, 474.0, "https://ert-ucdn.broadpeak-aas.com/bpk-tv/VOULITV/default/index.m3u8", false },
    { "ert_cosmos", "ERT Cosmos", "Public TV", "ERT MUX 2", 28, 530.0, "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERTCosmos/default/index.m3u8", false },

    // Free-to-Air National Channels
    { "star", "Star Channel", "Commercial TV", "Digea MUX 2", 27, 522.0, "http://livestar.siliconweb.com/starvod/star4/star4.m3u8", false },
    { "naftemporiki", "Naftemporiki TV", "Business TV", "Regional MUX", 29, 538.0, "https://telmaco.ascdn.broadpeak.io/nafteboriki/default/index.m3u8", false },

    // Regional Broadcasters
    { "vergina", "Vergina TV", "Regional TV", "Regional MUX", 29, 538.0, "https://verginanews.gr:8443/hls_live/stream1.m3u8", false },
    { "astra", "Astra TV", "Regional TV", "Regional MUX", 30, 546.0, "https://server.gointernet.gr/live/livestream.m3u8", false },
    { "center", "Center TV", "Regional TV", "Regional MUX", 29, 538.0, "https://eu1.streams.gr:8081/centertv/index.m3u8", false },
    { "trt", "TRT Thessaly", "Regional TV", "Regional MUX", 30, 546.0, "https://av.hellasnet.tv/rst/trt/main_stream.m3u8", false },

    // Public Radio Channels
    { "proto", "ERT Radio 1 (News)", "Radio", "ERT MUX 1", 21, 474.0, "https://radiostreaming.ert.gr/ert-proto", true },
    { "deftero", "ERT Radio 2 (Music)", "Radio", "ERT MUX 1", 21, 474.0, "https://radiostreaming.ert.gr/ert-deftero", true },
    { "trito", "ERT Radio 3 (Culture)", "Radio", "ERT MUX 1", 21, 474.0, "https://radiostreaming.ert.gr/ert-trito", true },
    { "erasport", "ERT ERA Sport", "Radio", "ERT MUX 1", 21, 474.0, "https://radiostreaming.ert.gr/ert-erasport", true },
    { "kosmos", "ERT Kosmos", "Radio", "ERT MUX 1", 21, 474.0, "https://radiostreaming.ert.gr/ert-kosmos", true },
    { "voice_of_greece", "ERT Voice of Greece", "Radio", "ERT MUX 1", 21, 474.0, "https://radiostreaming.ert.gr/ert-voiceofgreece", true }
};
static const int G_NUM_CHANNELS = sizeof(G_CHANNELS) / sizeof(G_CHANNELS[0]);

struct RegionDef {
    const char* id;
    const char* name;
    int ert_mux_1_ch;
    int ert_mux_2_ch;
    int digea_1_ch;
    int digea_2_ch;
    int regional_ch;
};

static const RegionDef G_REGIONS[] = {
    { "attica", "Athens / Attica", 21, 28, 22, 27, 29 },
    { "thessaloniki", "Thessaloniki / Central Macedonia", 24, 28, 29, 31, 30 },
    { "patras", "Patras / Western Greece", 24, 31, 22, 27, 30 },
    { "thessaly", "Larissa / Thessaly", 21, 28, 24, 26, 30 },
    { "crete", "Heraklion / Crete", 21, 24, 23, 25, 29 },
    { "epirus", "Ioannina / Epirus", 21, 28, 22, 25, 29 }
};
static const int G_NUM_REGIONS = sizeof(G_REGIONS) / sizeof(G_REGIONS[0]);

// Channel frequency calculation
static double get_channel_freq(int ch_idx, int region_idx) {
    if (ch_idx < 0 || ch_idx >= G_NUM_CHANNELS) return 474.0;
    if (region_idx < 0 || region_idx >= G_NUM_REGIONS) return G_CHANNELS[ch_idx].freq_mhz_default;

    const ChannelDef& ch = G_CHANNELS[ch_idx];
    const RegionDef& reg = G_REGIONS[region_idx];
    int uhf = ch.uhf_ch_default;

    if (strstr(ch.multiplex, "ERT MUX 1")) uhf = reg.ert_mux_1_ch;
    else if (strstr(ch.multiplex, "ERT MUX 2")) uhf = reg.ert_mux_2_ch;
    else if (strstr(ch.multiplex, "Digea MUX 1")) uhf = reg.digea_1_ch;
    else if (strstr(ch.multiplex, "Digea MUX 2")) uhf = reg.digea_2_ch;
    else if (strstr(ch.multiplex, "Regional")) uhf = reg.regional_ch;

    return 470.0 + (uhf - 21) * 8.0 + 4.0;
}

// App State
struct AppState {
    int active_channel_idx;
    int active_region_idx;
    double active_freq_mhz;
    float tuner_gain;
    bool auto_gain;
    bool audio_playback;
    int demod_mode;

    // Scan results
    bool show_scanner_overlay;
    ScanChannelResult scan_results[32];
    int scan_count;

    int channel_item_height;

    AppState() {
        active_channel_idx = 0;
        active_region_idx = 0;
        active_freq_mhz = 474.0;
        tuner_gain = 0.0f;
        auto_gain = true;
        audio_playback = false;
        demod_mode = DEMOD_MODE_DVB_T;
        show_scanner_overlay = false;
        scan_count = 0;
        channel_item_height = 30;
    }
};

static AppState g_app;
static HWND g_hWnd = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontMono = NULL;

// Waterfall DIB buffer for fast blitting
static HBITMAP g_hWaterfallBmp = NULL;
static void* g_pWaterfallBits = NULL;
static const int WF_WIDTH = 512;
static const int WF_HEIGHT = 256;

static void create_waterfall_dib(HDC hdc) {
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = WF_WIDTH;
    bmi.bmiHeader.biHeight = -WF_HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (g_hWaterfallBmp) DeleteObject(g_hWaterfallBmp);
    g_hWaterfallBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &g_pWaterfallBits, NULL, 0);
}

// Tune to selected channel
static void tune_active_channel() {
    g_app.active_freq_mhz = get_channel_freq(g_app.active_channel_idx, g_app.active_region_idx);
    uint32_t freq_hz = (uint32_t)(g_app.active_freq_mhz * 1e6);
    tv_demod_set_frequency(freq_hz);
}

// Export M3U8 Playlist file portably into the application folder
static void export_m3u8_file() {
    char local_dir[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, local_dir, MAX_PATH);
    char* last_slash = strrchr(local_dir, '\\');
    if (last_slash) *last_slash = '\0';

    char m3u_path[MAX_PATH];
    sprintf_s(m3u_path, sizeof(m3u_path), "%s\\channels.m3u8", local_dir);

    FILE* f = NULL;
    fopen_s(&f, m3u_path, "w, ccs=UTF-8");
    if (!f) fopen_s(&f, "channels.m3u8", "w, ccs=UTF-8");
    if (!f) return;

    fprintf(f, "#EXTM3U\n");
    for (int i = 0; i < G_NUM_CHANNELS; i++) {
        const ChannelDef& ch = G_CHANNELS[i];
        double freq = get_channel_freq(i, g_app.active_region_idx);
        fprintf(f, "#EXTINF:-1 tvg-id=\"%s\" tvg-name=\"%s\" group-title=\"%s\" radio=\"%s\",%s (%.1f MHz)\n",
            ch.id, ch.name, ch.category, ch.is_audio_only ? "true" : "false", ch.name, freq);
        fprintf(f, "%s\n", ch.stream_url);
    }
    fclose(f);
    MessageBoxA(g_hWnd, "Exported channels.m3u8 successfully.\nYou can open this file in VLC, MPC-HC, or Kodi.", "Export", MB_OK | MB_ICONINFORMATION);
}

// Run C++ UHF TV Band Scan
static void run_scanner() {
    g_app.show_scanner_overlay = true;
    InvalidateRect(g_hWnd, NULL, FALSE);
    UpdateWindow(g_hWnd);

    g_app.scan_count = tv_demod_run_uhf_scan(g_app.scan_results, 32);
    tune_active_channel();
    InvalidateRect(g_hWnd, NULL, FALSE);
}

// Locate native media player executable on the system (never open a web browser)
static bool find_native_player(char* out_path, size_t max_len) {
    // 1. Check local TV folder (e.g. bundled portable VLC in TV\vlc)
    char local_dir[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, local_dir, MAX_PATH);
    char* last_slash = strrchr(local_dir, '\\');
    if (last_slash) *last_slash = '\0';

    char portable_vlc[MAX_PATH];
    sprintf_s(portable_vlc, sizeof(portable_vlc), "%s\\vlc\\vlc.exe", local_dir);
    if (GetFileAttributesA(portable_vlc) != INVALID_FILE_ATTRIBUTES) {
        strcpy_s(out_path, max_len, portable_vlc);
        return true;
    }

    // 2. Query registry for default m3u8 player (e.g. VLC.m3u8)
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "VLC.m3u8\\shell\\Open\\command", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char cmdBuf[512];
        DWORD bufSize = sizeof(cmdBuf);
        if (RegQueryValueExA(hKey, NULL, NULL, NULL, (LPBYTE)cmdBuf, &bufSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (cmdBuf[0] == '"') {
                char* endQuote = strchr(cmdBuf + 1, '"');
                if (endQuote) {
                    *endQuote = '\0';
                    if (GetFileAttributesA(cmdBuf + 1) != INVALID_FILE_ATTRIBUTES) {
                        strcpy_s(out_path, max_len, cmdBuf + 1);
                        return true;
                    }
                }
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    // 3. Query registry for VLC installation directory
    HKEY hVlcKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\VideoLAN\\VLC", 0, KEY_READ, &hVlcKey) == ERROR_SUCCESS) {
        char vlcDir[MAX_PATH];
        DWORD dirSize = sizeof(vlcDir);
        if (RegQueryValueExA(hVlcKey, "InstallDir", NULL, NULL, (LPBYTE)vlcDir, &dirSize) == ERROR_SUCCESS) {
            RegCloseKey(hVlcKey);
            char vlcPath[MAX_PATH];
            sprintf_s(vlcPath, sizeof(vlcPath), "%s\\vlc.exe", vlcDir);
            if (GetFileAttributesA(vlcPath) != INVALID_FILE_ATTRIBUTES) {
                strcpy_s(out_path, max_len, vlcPath);
                return true;
            }
        } else {
            RegCloseKey(hVlcKey);
        }
    }

    // 4. Standard Windows Program Files locations
    const char* candidates[] = {
        "C:\\Program Files\\VideoLAN\\VLC\\vlc.exe",
        "C:\\Program Files (x86)\\VideoLAN\\VLC\\vlc.exe",
        "C:\\Program Files\\Windows Media Player\\wmplayer.exe",
        "C:\\Program Files (x86)\\Windows Media Player\\wmplayer.exe"
    };
    for (int i = 0; i < 4; i++) {
        if (GetFileAttributesA(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
            strcpy_s(out_path, max_len, candidates[i]);
            return true;
        }
    }

    // 5. Search system PATH for vlc.exe, mpv.exe, or wmplayer.exe
    char pathBuf[MAX_PATH];
    if (SearchPathA(NULL, "vlc.exe", NULL, MAX_PATH, pathBuf, NULL) > 0) {
        strcpy_s(out_path, max_len, pathBuf);
        return true;
    }
    if (SearchPathA(NULL, "mpv.exe", NULL, MAX_PATH, pathBuf, NULL) > 0) {
        strcpy_s(out_path, max_len, pathBuf);
        return true;
    }
    if (SearchPathA(NULL, "wmplayer.exe", NULL, MAX_PATH, pathBuf, NULL) > 0) {
        strcpy_s(out_path, max_len, pathBuf);
        return true;
    }

    return false;
}

// Open stream URL strictly in a native media player (no web browser)
static void open_active_stream() {
    const ChannelDef& ch = G_CHANNELS[g_app.active_channel_idx];
    char playerPath[512];
    if (find_native_player(playerPath, sizeof(playerPath))) {
        char params[1024];
        if (strstr(playerPath, "vlc.exe") || strstr(playerPath, "VLC")) {
            sprintf_s(params, sizeof(params), "--one-instance --no-video-title-show \"%s\"", ch.stream_url);
        } else {
            sprintf_s(params, sizeof(params), "\"%s\"", ch.stream_url);
        }
        ShellExecuteA(NULL, "open", playerPath, params, NULL, SW_SHOWNORMAL);
    } else {
        char params[1024];
        sprintf_s(params, sizeof(params), "\"%s\"", ch.stream_url);
        ShellExecuteA(NULL, "open", "wmplayer.exe", params, NULL, SW_SHOWNORMAL);
    }
}

// Draw Clean Spectrum Plot
static void draw_spectrum_plot(HDC hdc, RECT rect, const float* psd, int n_bins, const DemodMetrics& m) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    // Dark neutral background
    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);

    // Frame border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 52));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);

    // Grid lines (-80 to -10 dBFS)
    HPEN gridPen = CreatePen(PS_DOT, 1, RGB(32, 32, 38));
    SelectObject(hdc, gridPen);
    SelectObject(hdc, g_hFontSmall);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(110, 115, 125));

    for (int db = -80; db <= -10; db += 10) {
        float norm_y = (float)(-db - 10) / 75.0f;
        int y = rect.bottom - (int)((1.0f - norm_y) * (h - 24)) - 12;
        MoveToEx(hdc, rect.left + 45, y, NULL);
        LineTo(hdc, rect.right - 10, y);

        char dbStr[16];
        sprintf_s(dbStr, "%d dB", db);
        RECT tr = { rect.left + 5, y - 7, rect.left + 42, y + 7 };
        DrawTextA(hdc, dbStr, -1, &tr, DT_RIGHT | DT_SINGLELINE);
    }

    double span_mhz = m.sample_rate_hz / 1e6;

    // Center frequency vertical marker
    int center_x = rect.left + 45 + (w - 55) / 2;
    HPEN centerPen = CreatePen(PS_DASH, 1, RGB(220, 80, 80));
    SelectObject(hdc, centerPen);
    MoveToEx(hdc, center_x, rect.top + 8, NULL);
    LineTo(hdc, center_x, rect.bottom - 8);
    DeleteObject(centerPen);

    // Noise floor marker line
    float nf_norm = (m.noise_floor_dbfs - (-85.0f)) / 75.0f;
    if (nf_norm < 0.0f) nf_norm = 0.0f;
    if (nf_norm > 1.0f) nf_norm = 1.0f;
    int nf_y = rect.bottom - (int)(nf_norm * (h - 24)) - 12;

    HPEN nfPen = CreatePen(PS_DOT, 1, RGB(120, 125, 135));
    SelectObject(hdc, nfPen);
    MoveToEx(hdc, rect.left + 45, nf_y, NULL);
    LineTo(hdc, rect.right - 10, nf_y);
    DeleteObject(nfPen);

    // Plot PSD trace (clean light blue)
    HPEN curvePen = CreatePen(PS_SOLID, 2, RGB(56, 160, 235));
    SelectObject(hdc, curvePen);

    int plot_left = rect.left + 45;
    int plot_width = w - 55;
    int plot_h = h - 24;

    bool first = true;
    for (int i = 0; i < n_bins; i += 2) {
        float val = psd[i];
        float norm = (val - (-85.0f)) / 75.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        int x = plot_left + (i * plot_width) / n_bins;
        int y = rect.bottom - (int)(norm * plot_h) - 12;

        if (first) {
            MoveToEx(hdc, x, y, NULL);
            first = false;
        } else {
            LineTo(hdc, x, y);
        }
    }

    // Header label
    SelectObject(hdc, g_hFontSmall);
    SetTextColor(hdc, RGB(160, 170, 185));
    char leg[128];
    sprintf_s(leg, "Center: %.3f MHz  |  Span: %.1f MHz  |  Noise Floor: %.1f dBFS",
        m.center_freq_hz / 1e6, span_mhz, m.noise_floor_dbfs);
    RECT lr = { rect.left + 50, rect.top + 6, rect.right - 10, rect.top + 22 };
    DrawTextA(hdc, leg, -1, &lr, DT_LEFT | DT_SINGLELINE);

    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
    DeleteObject(gridPen);
    DeleteObject(curvePen);
}

// Draw Waterfall Spectrogram
static void draw_waterfall(HDC hdc, RECT rect) {
    if (!g_pWaterfallBits) create_waterfall_dib(hdc);

    tv_demod_get_waterfall_rgba((uint32_t*)g_pWaterfallBits, WF_WIDTH, WF_HEIGHT);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, g_hWaterfallBmp);

    int w = rect.right - rect.left - 55;
    int h = rect.bottom - rect.top;
    StretchBlt(hdc, rect.left + 45, rect.top, w, h, memDC, 0, 0, WF_WIDTH, WF_HEIGHT, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 52));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rect.left + 45, rect.top, rect.right - 10, rect.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
}

// Draw Scanner Overlay Bar Chart
static void draw_scanner_chart(HDC hdc, RECT rect) {
    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 22));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    SelectObject(hdc, g_hFontBold);
    SetTextColor(hdc, RGB(220, 220, 225));
    SetBkMode(hdc, TRANSPARENT);
    RECT titleRect = { rect.left + 20, rect.top + 15, rect.right - 20, rect.top + 40 };
    DrawTextA(hdc, "UHF Band Scan Results (Channels 21 - 48, 470 - 694 MHz) - Click to Close", -1, &titleRect, DT_LEFT);

    int n = g_app.scan_count;
    if (n == 0) return;

    int bar_area_w = rect.right - rect.left - 60;
    int bar_area_h = rect.bottom - rect.top - 100;
    int bar_bottom = rect.bottom - 45;
    int bar_w = bar_area_w / n;

    // Threshold line (-60 dBFS)
    float th_norm = (-60.0f - (-85.0f)) / 65.0f;
    int th_y = bar_bottom - (int)(th_norm * bar_area_h);

    HPEN thPen = CreatePen(PS_DASH, 1, RGB(180, 140, 60));
    HPEN oldPen = (HPEN)SelectObject(hdc, thPen);
    MoveToEx(hdc, rect.left + 45, th_y, NULL);
    LineTo(hdc, rect.right - 20, th_y);
    SelectObject(hdc, oldPen);
    DeleteObject(thPen);

    for (int i = 0; i < n; i++) {
        const ScanChannelResult& r = g_app.scan_results[i];
        float norm = (r.rssi_dbfs - (-85.0f)) / 65.0f;
        if (norm < 0.04f) norm = 0.04f;
        if (norm > 1.0f) norm = 1.0f;

        int bh = (int)(norm * bar_area_h);
        int bx = rect.left + 48 + i * bar_w;
        int by = bar_bottom - bh;

        COLORREF barColor = r.is_locked ? RGB(45, 160, 110) : RGB(50, 52, 60);
        HBRUSH barBrush = CreateSolidBrush(barColor);
        RECT br = { bx, by, bx + bar_w - 3, bar_bottom };
        FillRect(hdc, &br, barBrush);
        DeleteObject(barBrush);

        SelectObject(hdc, g_hFontSmall);
        SetTextColor(hdc, RGB(130, 135, 145));
        char chLbl[16];
        sprintf_s(chLbl, "%d", r.uhf_channel);
        RECT lr = { bx - 4, bar_bottom + 4, bx + bar_w, bar_bottom + 18 };
        DrawTextA(hdc, chLbl, -1, &lr, DT_CENTER);
    }
}

// Main Window Procedure
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;
        // Clean native typography
        g_hFontTitle = CreateFontA(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        g_hFontBold = CreateFontA(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        g_hFontNormal = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        g_hFontSmall = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        g_hFontMono = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");

        // Initialize C++ Demodulator Engine
        tv_demod_init(2400000);
        tv_demod_connect();
        tune_active_channel();

        // 30 FPS update timer
        SetTimer(hWnd, TIMER_ID_DSP, TIMER_INTERVAL_MS, NULL);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_ID_DSP) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            RECT mainPanel = { SIDEBAR_WIDTH, 0, clientRect.right, clientRect.bottom };
            InvalidateRect(hWnd, &mainPanel, FALSE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int win_w = clientRect.right;
        int win_h = clientRect.bottom;

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, win_w, win_h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        DemodMetrics m;
        tv_demod_get_metrics(&m);
        float psd[2048];
        tv_demod_get_psd(NULL, psd, 2048);

        // -------------------------------------------------------------
        // 1. SIDEBAR
        // -------------------------------------------------------------
        RECT sideRect = { 0, 0, SIDEBAR_WIDTH, win_h };
        HBRUSH sideBrush = CreateSolidBrush(RGB(24, 25, 28));
        FillRect(memDC, &sideRect, sideBrush);
        DeleteObject(sideBrush);

        // Dividing border
        HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(38, 40, 46));
        HPEN oldPen = (HPEN)SelectObject(memDC, sepPen);
        MoveToEx(memDC, SIDEBAR_WIDTH, 0, NULL);
        LineTo(memDC, SIDEBAR_WIDTH, win_h);
        SelectObject(memDC, oldPen);
        DeleteObject(sepPen);

        // App Title
        SelectObject(memDC, g_hFontTitle);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(230, 230, 235));
        RECT titleRect = { 16, 12, SIDEBAR_WIDTH - 16, 32 };
        DrawTextA(memDC, "TV SDR Receiver", -1, &titleRect, DT_LEFT | DT_SINGLELINE);

        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, RGB(120, 125, 135));
        RECT subRect = { 16, 33, SIDEBAR_WIDTH - 16, 48 };
        DrawTextA(memDC, "Terrestrial Broadcast Tuner", -1, &subRect, DT_LEFT | DT_SINGLELINE);

        // Device Status Box
        int status_y = 52;
        RECT badgeR = { 16, status_y, SIDEBAR_WIDTH - 16, status_y + 24 };
        HBRUSH badgeBrush = CreateSolidBrush(RGB(32, 34, 40));
        FillRect(memDC, &badgeR, badgeBrush);
        DeleteObject(badgeBrush);

        SelectObject(memDC, g_hFontSmall);
        COLORREF statusCol = m.is_hardware ? RGB(70, 190, 130) : RGB(140, 145, 160);
        SetTextColor(memDC, statusCol);
        char badgeTxt[128];
        if (m.is_hardware) sprintf_s(badgeTxt, "Hardware: Active (%s)", m.status_message);
        else sprintf_s(badgeTxt, "Fallback Mode (%s - Click to Reconnect)", m.sdr_status == SDR_STATUS_BUSY ? "Busy" : "Simulation");
        RECT bTextR = { 24, status_y + 4, SIDEBAR_WIDTH - 24, status_y + 20 };
        DrawTextA(memDC, badgeTxt, -1, &bTextR, DT_LEFT | DT_SINGLELINE);

        // Region Selector
        int reg_y = status_y + 30;
        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, RGB(140, 145, 155));
        RECT regTitleR = { 16, reg_y, SIDEBAR_WIDTH - 16, reg_y + 16 };
        DrawTextA(memDC, "Region (Click to change):", -1, &regTitleR, DT_LEFT | DT_SINGLELINE);

        RECT regBox = { 16, reg_y + 16, SIDEBAR_WIDTH - 16, reg_y + 42 };
        HBRUSH regBrush = CreateSolidBrush(RGB(32, 34, 40));
        FillRect(memDC, &regBox, regBrush);
        DeleteObject(regBrush);

        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, RGB(210, 215, 225));
        RECT regTextR = { 24, reg_y + 21, SIDEBAR_WIDTH - 30, reg_y + 39 };
        DrawTextA(memDC, G_REGIONS[g_app.active_region_idx].name, -1, &regTextR, DT_LEFT | DT_SINGLELINE);

        // Channel List Header
        int ch_list_start_y = reg_y + 48;
        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, RGB(140, 145, 155));
        RECT chTitleR = { 16, ch_list_start_y, SIDEBAR_WIDTH - 16, ch_list_start_y + 16 };
        DrawTextA(memDC, "Station", -1, &chTitleR, DT_LEFT | DT_SINGLELINE);
        RECT freqHeaderR = { SIDEBAR_WIDTH - 90, ch_list_start_y, SIDEBAR_WIDTH - 16, ch_list_start_y + 16 };
        DrawTextA(memDC, "Freq", -1, &freqHeaderR, DT_RIGHT | DT_SINGLELINE);

        // Channels Items
        int ch_y = ch_list_start_y + 18;
        int max_ch_y = win_h - 85;

        for (int i = 0; i < G_NUM_CHANNELS; i++) {
            if (ch_y + g_app.channel_item_height > max_ch_y) break;

            const ChannelDef& ch = G_CHANNELS[i];
            bool isActive = (i == g_app.active_channel_idx);
            double freq = get_channel_freq(i, g_app.active_region_idx);

            RECT itemR = { 16, ch_y, SIDEBAR_WIDTH - 16, ch_y + 26 };
            COLORREF itemBg = isActive ? RGB(38, 52, 72) : RGB(26, 28, 32);
            HBRUSH itemBrush = CreateSolidBrush(itemBg);
            FillRect(memDC, &itemR, itemBrush);
            DeleteObject(itemBrush);

            // Channel Name
            SelectObject(memDC, isActive ? g_hFontBold : g_hFontNormal);
            SetTextColor(memDC, isActive ? RGB(245, 245, 250) : RGB(190, 195, 205));
            RECT nameR = { 24, ch_y + 4, SIDEBAR_WIDTH - 95, ch_y + 22 };
            DrawTextA(memDC, ch.name, -1, &nameR, DT_LEFT | DT_SINGLELINE);

            // Frequency
            SelectObject(memDC, g_hFontSmall);
            SetTextColor(memDC, isActive ? RGB(160, 200, 240) : RGB(130, 135, 145));
            char fBuf[32];
            sprintf_s(fBuf, "%.1f MHz", freq);
            RECT freqR = { SIDEBAR_WIDTH - 90, ch_y + 5, SIDEBAR_WIDTH - 24, ch_y + 22 };
            DrawTextA(memDC, fBuf, -1, &freqR, DT_RIGHT | DT_SINGLELINE);

            ch_y += g_app.channel_item_height;
        }

        // Bottom Action Buttons
        int btn_y = win_h - 75;
        RECT btnStream = { 16, btn_y, SIDEBAR_WIDTH / 2 - 4, btn_y + 30 };
        HBRUSH streamBrush = CreateSolidBrush(RGB(40, 44, 52));
        FillRect(memDC, &btnStream, streamBrush);
        DeleteObject(streamBrush);

        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, RGB(220, 225, 235));
        DrawTextA(memDC, "Play Stream", -1, &btnStream, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT btnScan = { SIDEBAR_WIDTH / 2 + 4, btn_y, SIDEBAR_WIDTH - 16, btn_y + 30 };
        HBRUSH scanBrush = CreateSolidBrush(RGB(40, 44, 52));
        FillRect(memDC, &btnScan, scanBrush);
        DeleteObject(scanBrush);
        DrawTextA(memDC, "Scan Band", -1, &btnScan, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int btn_y2 = win_h - 38;
        RECT btnM3U8 = { 16, btn_y2, SIDEBAR_WIDTH - 16, btn_y2 + 28 };
        HBRUSH m3u8Brush = CreateSolidBrush(RGB(30, 32, 38));
        FillRect(memDC, &btnM3U8, m3u8Brush);
        DeleteObject(m3u8Brush);
        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, RGB(140, 145, 155));
        DrawTextA(memDC, "Export M3U Playlist", -1, &btnM3U8, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // -------------------------------------------------------------
        // 2. MAIN DISPLAY (Spectrum, Waterfall, Metrics)
        // -------------------------------------------------------------
        int main_x = SIDEBAR_WIDTH + 1;
        int main_w = win_w - main_x;

        RECT mainRect = { main_x, 0, win_w, win_h };
        HBRUSH mainBrush = CreateSolidBrush(RGB(14, 15, 18));
        FillRect(memDC, &mainRect, mainBrush);
        DeleteObject(mainBrush);

        if (g_app.show_scanner_overlay) {
            draw_scanner_chart(memDC, mainRect);
        } else {
            // Station Header Bar
            const ChannelDef& curCh = G_CHANNELS[g_app.active_channel_idx];
            RECT headRect = { main_x + 20, 12, win_w - 20, 34 };
            SelectObject(memDC, g_hFontTitle);
            SetTextColor(memDC, RGB(235, 235, 240));
            DrawTextA(memDC, curCh.name, -1, &headRect, DT_LEFT | DT_SINGLELINE);

            SelectObject(memDC, g_hFontSmall);
            SetTextColor(memDC, RGB(130, 135, 145));
            RECT metaRect = { main_x + 20, 34, win_w - 20, 50 };
            char metaTxt[128];
            sprintf_s(metaTxt, "%s  |  %s  |  Tuned LO: %.3f MHz", curCh.category, curCh.multiplex, g_app.active_freq_mhz);
            DrawTextA(memDC, metaTxt, -1, &metaRect, DT_LEFT | DT_SINGLELINE);

            // Metrics Cards Row (Simple, clean, uniform)
            int card_y = 54;
            int card_h = 44;
            int card_w = (main_w - 55) / 5;

            struct MetricCard {
                const char* title;
                const char* val;
                COLORREF color;
            };

            char fStr[32], rStr[32], sStr[32], lStr[32], qStr[32];
            sprintf_s(fStr, "%.3f MHz", m.center_freq_hz / 1e6);
            sprintf_s(rStr, "%.1f dBFS", m.rssi_dbfs);
            sprintf_s(sStr, "+%.1f dB", m.snr_db);
            sprintf_s(lStr, "%s", m.is_carrier_locked ? "Locked" : "Searching");
            sprintf_s(qStr, "%.0f%%", m.quality_pct);

            MetricCard cards[5] = {
                { "Frequency", fStr, RGB(210, 215, 225) },
                { "Signal", rStr, RGB(210, 215, 225) },
                { "SNR", sStr, m.snr_db > 10.0f ? RGB(70, 190, 130) : RGB(210, 160, 60) },
                { "Carrier Lock", lStr, m.is_carrier_locked ? RGB(70, 190, 130) : RGB(200, 80, 80) },
                { "Quality", qStr, RGB(210, 215, 225) }
            };

            for (int c = 0; c < 5; c++) {
                RECT cr = { main_x + 20 + c * (card_w + 4), card_y, main_x + 20 + c * (card_w + 4) + card_w, card_y + card_h };
                HBRUSH cBrush = CreateSolidBrush(RGB(24, 25, 30));
                FillRect(memDC, &cr, cBrush);
                DeleteObject(cBrush);

                SelectObject(memDC, g_hFontSmall);
                SetTextColor(memDC, RGB(130, 135, 145));
                RECT cTitleR = { cr.left + 8, cr.top + 4, cr.right - 8, cr.top + 18 };
                DrawTextA(memDC, cards[c].title, -1, &cTitleR, DT_LEFT | DT_SINGLELINE);

                SelectObject(memDC, g_hFontMono);
                SetTextColor(memDC, cards[c].color);
                RECT cValR = { cr.left + 8, cr.top + 20, cr.right - 8, cr.bottom - 4 };
                DrawTextA(memDC, cards[c].val, -1, &cValR, DT_LEFT | DT_SINGLELINE);
            }

            // Spectrum Graph Area
            int spec_top = card_y + card_h + 12;
            int spec_h = (win_h - spec_top - 55) * 55 / 100;
            RECT specRect = { main_x + 20, spec_top, win_w - 20, spec_top + spec_h };
            draw_spectrum_plot(memDC, specRect, psd, 2048, m);

            // Waterfall Area
            int wf_top = spec_top + spec_h + 8;
            int wf_h = win_h - wf_top - 46;
            RECT wfRect = { main_x + 20, wf_top, win_w - 20, wf_top + wf_h };
            draw_waterfall(memDC, wfRect);

            // Bottom Audio / Control Bar
            int bot_y = win_h - 38;
            RECT botRect = { main_x + 20, bot_y, win_w - 20, win_h - 8 };
            HBRUSH botBrush = CreateSolidBrush(RGB(22, 23, 27));
            FillRect(memDC, &botRect, botBrush);
            DeleteObject(botBrush);

            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, RGB(190, 195, 205));
            RECT demodLblR = { botRect.left + 10, botRect.top + 6, botRect.left + 120, botRect.bottom };
            DrawTextA(memDC, "Audio Output:", -1, &demodLblR, DT_LEFT | DT_SINGLELINE);

            // Audio Toggle Button
            RECT audBtn = { botRect.left + 115, botRect.top + 3, botRect.left + 225, botRect.bottom - 3 };
            COLORREF audBg = g_app.audio_playback ? RGB(45, 140, 90) : RGB(36, 38, 44);
            HBRUSH audBrush = CreateSolidBrush(audBg);
            FillRect(memDC, &audBtn, audBrush);
            DeleteObject(audBrush);

            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, RGB(240, 240, 245));
            const char* audTxt = g_app.audio_playback ? "Audio: Active" : "Audio: Muted";
            DrawTextA(memDC, audTxt, -1, &audBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, g_hFontSmall);
            SetTextColor(memDC, RGB(120, 125, 135));
            RECT statR = { botRect.left + 245, botRect.top + 7, botRect.right - 10, botRect.bottom };
            char statBuf[128];
            sprintf_s(statBuf, "Demodulator: %s  |  Output: 48 kHz PCM  |  Rate: 2.4 MSPS",
                g_app.demod_mode == DEMOD_MODE_WFM ? "WFM" : (g_app.demod_mode == DEMOD_MODE_AM ? "AM" : "DVB-T Carrier Analysis"));
            DrawTextA(memDC, statBuf, -1, &statR, DT_LEFT | DT_SINGLELINE);
        }

        BitBlt(hdc, 0, 0, win_w, win_h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int win_h = clientRect.bottom;

        if (g_app.show_scanner_overlay) {
            g_app.show_scanner_overlay = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // Sidebar clicks
        if (x < SIDEBAR_WIDTH) {
            // Status Box click -> Reconnect SDR
            int status_box_y = 52;
            if (x >= 16 && x <= SIDEBAR_WIDTH - 16 && y >= status_box_y && y <= status_box_y + 24) {
                tv_demod_connect();
                tune_active_channel();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Region Box click
            int reg_box_y = 52 + 30 + 16;
            if (x >= 16 && x <= SIDEBAR_WIDTH - 16 && y >= reg_box_y && y <= reg_box_y + 26) {
                g_app.active_region_idx = (g_app.active_region_idx + 1) % G_NUM_REGIONS;
                tune_active_channel();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Channel Item click
            int ch_start_y = reg_box_y + 26 + 18;
            int max_ch_y = win_h - 85;

            if (y >= ch_start_y && y < max_ch_y) {
                int clicked_idx = (y - ch_start_y) / g_app.channel_item_height;
                if (clicked_idx >= 0 && clicked_idx < G_NUM_CHANNELS) {
                    g_app.active_channel_idx = clicked_idx;
                    if (G_CHANNELS[clicked_idx].is_audio_only) {
                        g_app.demod_mode = DEMOD_MODE_WFM;
                        tv_demod_set_mode(DEMOD_MODE_WFM);
                    } else {
                        g_app.demod_mode = DEMOD_MODE_DVB_T;
                        tv_demod_set_mode(DEMOD_MODE_DVB_T);
                    }
                    tune_active_channel();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }

            // Action Buttons
            int btn_y = win_h - 75;
            if (y >= btn_y && y <= btn_y + 30) {
                if (x >= 16 && x <= SIDEBAR_WIDTH / 2 - 4) {
                    open_active_stream();
                    return 0;
                } else if (x >= SIDEBAR_WIDTH / 2 + 4 && x <= SIDEBAR_WIDTH - 16) {
                    run_scanner();
                    return 0;
                }
            }

            int btn_y2 = win_h - 38;
            if (y >= btn_y2 && y <= btn_y2 + 28 && x >= 16 && x <= SIDEBAR_WIDTH - 16) {
                export_m3u8_file();
                return 0;
            }
        } else {
            // Main Display Audio Toggle
            int bot_y = win_h - 38;
            RECT audBtn = { SIDEBAR_WIDTH + 20 + 115, bot_y + 3, SIDEBAR_WIDTH + 20 + 225, win_h - 11 };
            if (x >= audBtn.left && x <= audBtn.right && y >= audBtn.top && y <= audBtn.bottom) {
                g_app.audio_playback = !g_app.audio_playback;
                tv_demod_enable_audio_playback(g_app.audio_playback ? 1 : 0);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == 'R' || wParam == 'r') {
            tv_demod_connect();
            tune_active_channel();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_SIZE: {
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, TIMER_ID_DSP);
        tv_demod_shutdown();
        if (g_hWaterfallBmp) DeleteObject(g_hWaterfallBmp);
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        if (g_hFontMono) DeleteObject(g_hFontMono);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// Application Entry Point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "GreekTvRadioSdrClass";

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExA(
        0,
        "GreekTvRadioSdrClass",
        "Greek TV SDR Tuner",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        MessageBoxA(NULL, "Failed to create application window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
