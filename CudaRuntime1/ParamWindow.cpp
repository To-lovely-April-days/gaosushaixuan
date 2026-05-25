// ============================================================================
// File   : ParamWindow.cpp
// Purpose: Parameter adjustment window (Win32 native GUI) + config.ini I/O
//
// All UI strings use English to avoid GBK/UTF-8 encoding issues with VS.
// You can read this as: Frame Activate / Pixel Ratio
// ============================================================================
#include "ParamWindow.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <thread>
#include <string>
#include <fstream>
#include <iostream>

#pragma comment(lib, "Comctl32.lib")

// Global params from Eject.cpp
extern int g_valveThresholdRatio;       // Pixel activation (0~100)
extern int g_frameActivateThreshold;    // Frame activation (0~50)

// ============================================================================
// Control IDs
// ============================================================================
#define IDC_SLIDER_FRAME      101
#define IDC_EDIT_FRAME        102
#define IDC_LABEL_FRAME_VAL   103

#define IDC_SLIDER_RATIO      201
#define IDC_EDIT_RATIO        202
#define IDC_LABEL_RATIO_VAL   203

#define IDC_BUTTON_RESET      301
#define IDC_BUTTON_HIDE       302

// ============================================================================
// Parameter ranges
// ============================================================================
#define FRAME_MIN   0
#define FRAME_MAX   50
#define RATIO_MIN   0
#define RATIO_MAX   100

#define DEFAULT_FRAME    5
#define DEFAULT_RATIO   75

// ============================================================================
// Module state
// ============================================================================
static HWND g_hMainWnd = NULL;
static HWND g_hSliderFrame = NULL;
static HWND g_hEditFrame = NULL;
static HWND g_hLabelFrame = NULL;
static HWND g_hSliderRatio = NULL;
static HWND g_hEditRatio = NULL;
static HWND g_hLabelRatio = NULL;

static std::thread g_winThread;
static bool g_winRunning = false;
static bool g_updatingControls = false;

// ============================================================================
// Sync controls from globals
// ============================================================================
static void SyncControlsFromGlobals()
{
    if (!g_hMainWnd) return;

    g_updatingControls = true;

    int frame = g_frameActivateThreshold;
    int ratio = g_valveThresholdRatio;

    if (frame < FRAME_MIN) frame = FRAME_MIN;
    if (frame > FRAME_MAX) frame = FRAME_MAX;
    if (ratio < RATIO_MIN) ratio = RATIO_MIN;
    if (ratio > RATIO_MAX) ratio = RATIO_MAX;

    // Frame activate
    SendMessage(g_hSliderFrame, TBM_SETPOS, TRUE, frame);
    char buf[32];
    sprintf_s(buf, "%d", frame);
    SetWindowTextA(g_hEditFrame, buf);
    char label[64];
    sprintf_s(label, "Current: %d (consecutive pixels)", frame);
    SetWindowTextA(g_hLabelFrame, label);

    // Pixel ratio
    SendMessage(g_hSliderRatio, TBM_SETPOS, TRUE, ratio);
    sprintf_s(buf, "%d", ratio);
    SetWindowTextA(g_hEditRatio, buf);
    sprintf_s(label, "Current: %d %%", ratio);
    SetWindowTextA(g_hLabelRatio, label);

    g_updatingControls = false;
}

// ============================================================================
// Window procedure
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // ===== Frame Activate group =====
        HWND hGroup1 = CreateWindowExA(0, "BUTTON", "Frame Activate (consecutive target pixels)",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 10, 380, 110, hwnd, NULL, NULL, NULL);
        SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hLabelFrame = CreateWindowExA(0, "STATIC", "Current: 5",
            WS_CHILD | WS_VISIBLE,
            25, 35, 250, 20, hwnd, (HMENU)IDC_LABEL_FRAME_VAL, NULL, NULL);
        SendMessage(g_hLabelFrame, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hSliderFrame = CreateWindowExA(0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
            25, 60, 280, 30, hwnd, (HMENU)IDC_SLIDER_FRAME, NULL, NULL);
        SendMessage(g_hSliderFrame, TBM_SETRANGE, TRUE, MAKELONG(FRAME_MIN, FRAME_MAX));
        SendMessage(g_hSliderFrame, TBM_SETTICFREQ, 5, 0);
        SendMessage(g_hSliderFrame, TBM_SETPOS, TRUE, DEFAULT_FRAME);

        g_hEditFrame = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "5",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            315, 60, 60, 22, hwnd, (HMENU)IDC_EDIT_FRAME, NULL, NULL);
        SendMessage(g_hEditFrame, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateWindowExA(0, "STATIC", "Range: 0~50  (0 = disabled)",
            WS_CHILD | WS_VISIBLE,
            25, 92, 280, 18, hwnd, NULL, NULL, NULL);

        // ===== Pixel Ratio group =====
        HWND hGroup2 = CreateWindowExA(0, "BUTTON", "Pixel Activate Ratio (valve trigger %)",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 130, 380, 110, hwnd, NULL, NULL, NULL);
        SendMessage(hGroup2, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hLabelRatio = CreateWindowExA(0, "STATIC", "Current: 75 %",
            WS_CHILD | WS_VISIBLE,
            25, 155, 250, 20, hwnd, (HMENU)IDC_LABEL_RATIO_VAL, NULL, NULL);
        SendMessage(g_hLabelRatio, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hSliderRatio = CreateWindowExA(0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
            25, 180, 280, 30, hwnd, (HMENU)IDC_SLIDER_RATIO, NULL, NULL);
        SendMessage(g_hSliderRatio, TBM_SETRANGE, TRUE, MAKELONG(RATIO_MIN, RATIO_MAX));
        SendMessage(g_hSliderRatio, TBM_SETTICFREQ, 10, 0);
        SendMessage(g_hSliderRatio, TBM_SETPOS, TRUE, DEFAULT_RATIO);

        g_hEditRatio = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "75",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            315, 180, 60, 22, hwnd, (HMENU)IDC_EDIT_RATIO, NULL, NULL);
        SendMessage(g_hEditRatio, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateWindowExA(0, "STATIC", "Range: 0~100  (0 = disabled)",
            WS_CHILD | WS_VISIBLE,
            25, 212, 280, 18, hwnd, NULL, NULL, NULL);

        // ===== Buttons =====
        HWND hBtnReset = CreateWindowExA(0, "BUTTON", "Reset Default",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            60, 255, 100, 30, hwnd, (HMENU)IDC_BUTTON_RESET, NULL, NULL);
        SendMessage(hBtnReset, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnHide = CreateWindowExA(0, "BUTTON", "Hide Window",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            240, 255, 100, 30, hwnd, (HMENU)IDC_BUTTON_HIDE, NULL, NULL);
        SendMessage(hBtnHide, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hMainWnd = hwnd;
        SyncControlsFromGlobals();

        return 0;
    }

    case WM_HSCROLL:
    {
        if (g_updatingControls) return 0;

        HWND hSlider = (HWND)lParam;

        if (hSlider == g_hSliderFrame) {
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            g_frameActivateThreshold = pos;

            g_updatingControls = true;
            char buf[32];
            sprintf_s(buf, "%d", pos);
            SetWindowTextA(g_hEditFrame, buf);
            char label[64];
            sprintf_s(label, "Current: %d (consecutive pixels)", pos);
            SetWindowTextA(g_hLabelFrame, label);
            g_updatingControls = false;
        }
        else if (hSlider == g_hSliderRatio) {
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            g_valveThresholdRatio = pos;

            g_updatingControls = true;
            char buf[32];
            sprintf_s(buf, "%d", pos);
            SetWindowTextA(g_hEditRatio, buf);
            char label[64];
            sprintf_s(label, "Current: %d %%", pos);
            SetWindowTextA(g_hLabelRatio, label);
            g_updatingControls = false;
        }
        return 0;
    }

    case WM_COMMAND:
    {
        WORD code = HIWORD(wParam);
        WORD id = LOWORD(wParam);

        if (code == EN_CHANGE && !g_updatingControls)
        {
            if (id == IDC_EDIT_FRAME) {
                char buf[32];
                GetWindowTextA(g_hEditFrame, buf, sizeof(buf));
                int v = atoi(buf);
                if (v < FRAME_MIN) v = FRAME_MIN;
                if (v > FRAME_MAX) v = FRAME_MAX;
                g_frameActivateThreshold = v;

                g_updatingControls = true;
                SendMessage(g_hSliderFrame, TBM_SETPOS, TRUE, v);
                char label[64];
                sprintf_s(label, "Current: %d (consecutive pixels)", v);
                SetWindowTextA(g_hLabelFrame, label);
                g_updatingControls = false;
            }
            else if (id == IDC_EDIT_RATIO) {
                char buf[32];
                GetWindowTextA(g_hEditRatio, buf, sizeof(buf));
                int v = atoi(buf);
                if (v < RATIO_MIN) v = RATIO_MIN;
                if (v > RATIO_MAX) v = RATIO_MAX;
                g_valveThresholdRatio = v;

                g_updatingControls = true;
                SendMessage(g_hSliderRatio, TBM_SETPOS, TRUE, v);
                char label[64];
                sprintf_s(label, "Current: %d %%", v);
                SetWindowTextA(g_hLabelRatio, label);
                g_updatingControls = false;
            }
        }

        if (code == BN_CLICKED) {
            if (id == IDC_BUTTON_RESET) {
                g_frameActivateThreshold = DEFAULT_FRAME;
                g_valveThresholdRatio = DEFAULT_RATIO;
                SyncControlsFromGlobals();
                std::cout << "[Param] Reset to default: frame=" << DEFAULT_FRAME
                    << "  ratio=" << DEFAULT_RATIO << "%" << std::endl;
            }
            else if (id == IDC_BUTTON_HIDE) {
                ShowWindow(hwnd, SW_HIDE);
            }
        }
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        g_hMainWnd = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Window thread
// ============================================================================
static void WindowThreadFunc()
{
    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "FastSortingParamWnd";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    if (!RegisterClassA(&wc)) {
        std::cerr << "[ParamWindow] RegisterClass failed" << std::endl;
        return;
    }

    HWND hwnd = CreateWindowExA(
        0, "FastSortingParamWnd", "FastSorting - Parameter Adjustment",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, 420, 340,
        NULL, NULL, GetModuleHandleA(NULL), NULL);

    if (!hwnd) {
        std::cerr << "[ParamWindow] CreateWindow failed" << std::endl;
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    std::cout << "[ParamWindow] Window shown" << std::endl;

    MSG msg;
    while (g_winRunning && GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

// ============================================================================
// Public API
// ============================================================================
void StartParamWindow()
{
    if (g_winRunning) return;
    g_winRunning = true;
    g_winThread = std::thread(WindowThreadFunc);
    g_winThread.detach();
}

void StopParamWindow()
{
    g_winRunning = false;
    if (g_hMainWnd) {
        PostMessageA(g_hMainWnd, WM_CLOSE, 0, 0);
    }
}

// ============================================================================
// config.ini read/write
// ============================================================================
static std::string GetIniPath()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string sp = path;
    size_t pos = sp.find_last_of("\\/");
    if (pos != std::string::npos) sp = sp.substr(0, pos + 1);
    sp += "config.ini";
    return sp;
}

void LoadConfigFromIni()
{
    std::string path = GetIniPath();
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[Config] " << path << " not found, use code defaults" << std::endl;
        return;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
            };
        trim(key); trim(val);

        try {
            int v = std::stoi(val);
            if (key == "frame_activate") {
                if (v < FRAME_MIN) v = FRAME_MIN;
                if (v > FRAME_MAX) v = FRAME_MAX;
                g_frameActivateThreshold = v;
            }
            else if (key == "valve_ratio") {
                if (v < RATIO_MIN) v = RATIO_MIN;
                if (v > RATIO_MAX) v = RATIO_MAX;
                g_valveThresholdRatio = v;
            }
        }
        catch (...) {}
    }
    f.close();

    std::cout << "[Config] Loaded from " << path << ": "
        << "frame=" << g_frameActivateThreshold
        << "  ratio=" << g_valveThresholdRatio << "%" << std::endl;
}

void SaveConfigToIni()
{
    std::string path = GetIniPath();
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[Config] Cannot write " << path << std::endl;
        return;
    }
    f << "# FastSorting parameter config\n";
    f << "# Auto-saved by program on shutdown\n";
    f << "[FastSorting]\n";
    f << "frame_activate=" << g_frameActivateThreshold << "\n";
    f << "valve_ratio=" << g_valveThresholdRatio << "\n";
    f.close();

    std::cout << "[Config] Saved to " << path << std::endl;
}