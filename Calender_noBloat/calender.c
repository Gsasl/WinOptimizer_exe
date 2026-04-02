/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║  PerfectCalendar  v1.3.0                                        ║
 * ║  Pure Win32 C  ·  Zero external dependencies  ·  ~230 KB       ║
 * ║  winget install Gsasl.PerfectCalendar                           ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  Build (MinGW-w64):                                             ║
 * ║    windres app.rc -O coff -o app.res                            ║
 * ║    gcc calender.c app.res -o PerfectCalendar.exe -mwindows \    ║
 * ║        -lcomctl32 -luxtheme -lshell32 -ladvapi32 -O2            ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  events.dat  (%APPDATA%\PerfectCalendar\events.dat)             ║
 * ║                                                                  ║
 * ║    YYYY-MM-DD|One-time event                                    ║
 * ║    ****-MM-DD|Yearly recurring   (e.g. ****-12-25|Christmas)    ║
 * ║    ****-**-DD|Monthly recurring  (e.g. ****-**-01|Rent due)     ║
 * ║                                                                  ║
 * ║  Lines beginning with '#' are comments and are ignored.         ║
 * ╚══════════════════════════════════════════════════════════════════╝
 */

/* ── Common-Controls v6 manifest (visual styles, SetWindowSubclass) ── */
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' "\
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "\
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

/* ════════════════════════════════════════════════════════════════════
   §1  CONSTANTS
   ════════════════════════════════════════════════════════════════════ */

#define IDI_APPICON         101
#define WM_USER_TRAY        (WM_USER + 1)

/* Timer IDs */
#define TID_CLOCK           1
#define TID_TRAY_RETRY      2
#define TID_TITLE_FLASH     3
#define TID_FADE            4

/* Hotkey slot */
#define HOTKEY_APP          1

/* Child control IDs */
#define IDC_CLOSE_BTN       1
#define IDC_MAX_BTN         2
#define IDC_AGENDA_LIST     3

/* Tray menu IDs — hotkey presets */
#define IDM_HK_CTRLSHIFT    3001
#define IDM_HK_CTRLALT      3002
#define IDM_HK_WINSHIFT     3003
#define IDM_HK_DISABLED     3004

/* Tray menu IDs — other */
#define IDM_EXIT            1001
#define IDM_STARTUP         1002
#define IDM_OPEN_EVENTS     1003
#define IDM_RELOAD_EVENTS   1004

/* Calendar right-click IDs */
#define IDM_COPY_ISO        2001
#define IDM_COPY_US         2002
#define IDM_COPY_LONG       2003
#define IDM_ADD_EVENT       2004
#define IDM_RELOAD_CTX      2005

/* Registry paths */
#define REG_APP             "Software\\PerfectCalendar"
#define REG_RUN             "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APP_NAME            "PerfectCalendar"
#define MUTEX_NAME          "PerfectCalendar_v13_Mutex"
#define WC_MAIN             "MinimalCalendarClass"

/* Layout constants */
#define UI_DRAG_WIDTH       255
#define UI_AGENDA_LBL_H     20
#define UI_AGENDA_LIST_H    120

/* ════════════════════════════════════════════════════════════════════
   §2  TYPES & GLOBAL STATE
   ════════════════════════════════════════════════════════════════════ */

#define MAX_EVENTS  512
#define MAX_DESC    200

typedef struct {
    char date[12];      /* "YYYY-MM-DD", "****-MM-DD", or "****-**-DD" */
    char desc[MAX_DESC];
    BOOL recurring;     /* TRUE when pattern contains '*'               */
} CalEvent;

/* ── Persisted settings (registry-backed) ── */
static int  g_fontSize      = 20;       /* 12–40                        */
static UINT g_hkMod         = MOD_CONTROL | MOD_SHIFT;
static UINT g_hkVK          = VK_SPACE;
static BYTE g_idleAlpha     = 160;      /* opacity when mouse away      */
static BOOL g_hkEnabled     = TRUE;

/* ── Runtime flags ── */
static BOOL g_hotkeyLive    = FALSE;    /* TRUE when RegisterHotKey OK  */
static BOOL g_mouseIn       = FALSE;
static BOOL g_maximized     = FALSE;
static BYTE g_curAlpha      = 255;
static RECT g_normalRect    = {0};
static UINT WM_TASKBARCREATED = 0;

/* ── Event store ── */
static CalEvent   g_ev[MAX_EVENTS];
static int        g_evCount    = 0;
static SYSTEMTIME g_selDate    = {0};
static char       g_evPath[MAX_PATH] = {0};

/* ── Window handles ── */
static HWND hMain        = NULL;
static HWND hCal         = NULL;
static HWND hTimeLbl     = NULL;
static HWND hUTCLbl      = NULL;
static HWND hCloseBtn    = NULL;
static HWND hMaxBtn      = NULL;
static HWND hAgendaLbl   = NULL;
static HWND hAgendaList  = NULL;
static HWND hAddEdit     = NULL;    /* Inline event-entry EDIT control  */

/* ── GDI objects ── */
static HFONT  hFntLarge  = NULL;
static HFONT  hFntNormal = NULL;
static HFONT  hFntSmall  = NULL;
static HBRUSH hBgBrush   = NULL;    /* RGB(32 ,32 ,32)  main bg         */
static HBRUSH hListBrush = NULL;    /* RGB(22 ,22 ,24)  list bg         */
static HBRUSH hSelBrush  = NULL;    /* RGB(0  ,90 ,175) selected item   */

static HINSTANCE  g_hInst  = NULL;
static NOTIFYICONDATA g_nid = {0};


/* ════════════════════════════════════════════════════════════════════
   §3  REGISTRY / SETTINGS
   ════════════════════════════════════════════════════════════════════ */

static void LoadSettings(void)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_APP, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    DWORD val, sz = sizeof(DWORD);
#define RD(name, field, lo, hi) \
    sz = sizeof(DWORD); \
    if (RegQueryValueExA(hKey, name, NULL, NULL, (BYTE*)&val, &sz) == ERROR_SUCCESS \
        && (int)val >= (lo) && (int)val <= (hi)) (field) = (typeof(field))val;

    RD("FontSize",      g_fontSize,  12,    40)
    RD("HotkeyMod",    g_hkMod,      0, 0xFFFF)
    RD("HotkeyVK",     g_hkVK,       0,  0xFE)
    RD("IdleAlpha",    g_idleAlpha,  60,   255)
    RD("HotkeyEnabled",g_hkEnabled,   0,     1)
#undef RD
    RegCloseKey(hKey);
}

static void SaveSettings(void)
{
    HKEY hKey; DWORD disp;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_APP, 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disp) != ERROR_SUCCESS)
        return;
#define WR(name, val) { DWORD _v = (DWORD)(val); \
    RegSetValueExA(hKey, name, 0, REG_DWORD, (BYTE*)&_v, sizeof(DWORD)); }
    WR("FontSize",      g_fontSize)
    WR("HotkeyMod",    g_hkMod)
    WR("HotkeyVK",     g_hkVK)
    WR("IdleAlpha",    g_idleAlpha)
    WR("HotkeyEnabled",g_hkEnabled)
#undef WR
    RegCloseKey(hKey);
}


/* ════════════════════════════════════════════════════════════════════
   §4  EVENT FILE HELPERS
   ════════════════════════════════════════════════════════════════════ */

static void InitEventsPath(void)
{
    char ad[MAX_PATH] = {0};
    GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\PerfectCalendar", ad);
    CreateDirectoryA(dir, NULL);
    snprintf(g_evPath, sizeof(g_evPath), "%s\\events.dat", dir);

    if (GetFileAttributesA(g_evPath) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(g_evPath, "w");
        if (f) {
            fputs("# PerfectCalendar events\n", f);
            fputs("# YYYY-MM-DD|One-time event\n", f);
            fputs("# ****-MM-DD|Yearly recurring\n", f);
            fputs("# ****-**-DD|Monthly recurring\n", f);
            fputs("#\n# Examples:\n", f);
            fputs("# ****-12-25|Christmas\n", f);
            fputs("# ****-**-01|Rent due\n", f);
            fclose(f);
        }
    }
}

static void LoadEvents(void)
{
    g_evCount = 0;
    if (g_evPath[0] == '\0') return;
    FILE *f = fopen(g_evPath, "r");
    if (!f) return;
    char line[320];
    while (fgets(line, sizeof(line), f) && g_evCount < MAX_EVENTS) {
        char *p = line + strlen(line) - 1;
        while (p >= line && (*p == '\r' || *p == '\n')) *p-- = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        char *sep = strchr(line, '|');
        if (!sep || (sep - line) != 10) continue;
        *sep = '\0';
        strncpy(g_ev[g_evCount].date, line,   11); g_ev[g_evCount].date[11] = '\0';
        strncpy(g_ev[g_evCount].desc, sep + 1, MAX_DESC - 1);
        g_ev[g_evCount].desc[MAX_DESC-1] = '\0';
        g_ev[g_evCount].recurring = (strchr(g_ev[g_evCount].date, '*') != NULL);
        g_evCount++;
    }
    fclose(f);
}

static void AppendEvent(const char *date, const char *desc)
{
    FILE *f = fopen(g_evPath, "a");
    if (!f) return;
    fprintf(f, "%s|%s\n", date, desc);
    fclose(f);
}

/*
 * IsEventMatch() — wildcard-aware comparison.
 *
 * "****" matches any 4-digit year.
 * "**"   matches any 2-digit month or day.
 * Exact fields must match numerically.
 */
static BOOL IsEventMatch(const CalEvent *ev, int y, int m, int d)
{
    char py[5], pm[3], pd[3];
    memcpy(py, ev->date,     4); py[4] = '\0';
    memcpy(pm, ev->date + 5, 2); pm[2] = '\0';
    memcpy(pd, ev->date + 8, 2); pd[2] = '\0';
    BOOL yOk = (strcmp(py,"****")==0) || (atoi(py)==y);
    BOOL mOk = (strcmp(pm,"**")  ==0) || (atoi(pm)==m);
    BOOL dOk = (strcmp(pd,"**")  ==0) || (atoi(pd)==d);
    return yOk && mOk && dOk;
}

/*
 * GetDayStateMask() — bitmask for MCN_GETDAYSTATE.
 *
 * Bit (day-1) is set when ≥1 event matches that day.
 * This single function is the bridge between flat-file I/O
 * and the MonthCal's native bold-day rendering engine.
 */
static MONTHDAYSTATE GetDayStateMask(int year, int month)
{
    MONTHDAYSTATE mask = 0;
    for (int d = 1; d <= 31; d++)
        for (int i = 0; i < g_evCount; i++)
            if (IsEventMatch(&g_ev[i], year, month, d)) {
                mask |= (1u << (d - 1));
                break;
            }
    return mask;
}


/* ════════════════════════════════════════════════════════════════════
   §5  CLIPBOARD
   ════════════════════════════════════════════════════════════════════ */

static void CopyToClipboard(HWND hwnd, const char *text)
{
    SIZE_T len  = strlen(text) + 1;
    HGLOBAL hm  = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hm) return;
    memcpy(GlobalLock(hm), text, len);
    GlobalUnlock(hm);
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hm); /* clipboard owns hm now */
        CloseClipboard();
    } else GlobalFree(hm);
}


/* ════════════════════════════════════════════════════════════════════
   §6  HOTKEY MANAGEMENT
   ════════════════════════════════════════════════════════════════════ */

/*
 * FormatHotkeyLabel() — builds "Ctrl+Shift+Space" display string.
 */
static void FormatHotkeyLabel(UINT mod, UINT vk, char *buf, int sz)
{
    buf[0] = '\0';
    if (mod & MOD_WIN)     strncat(buf, "Win+",   (size_t)(sz-1));
    if (mod & MOD_CONTROL) strncat(buf, "Ctrl+",  (size_t)(sz-1));
    if (mod & MOD_ALT)     strncat(buf, "Alt+",   (size_t)(sz-1));
    if (mod & MOD_SHIFT)   strncat(buf, "Shift+", (size_t)(sz-1));
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    char kn[24] = {0};
    GetKeyNameTextA((LONG)(sc << 16), kn, sizeof(kn));
    strncat(buf, kn, (size_t)(sz-1));
}

/*
 * ShowBalloon() — non-blocking tray notification.
 * Used to surface hotkey conflicts without interrupting the user.
 */
static void ShowBalloon(const char *title, const char *msg, DWORD icon)
{
    g_nid.uFlags |= NIF_INFO;
    strncpy(g_nid.szInfoTitle, title, sizeof(g_nid.szInfoTitle)-1);
    strncpy(g_nid.szInfo,      msg,   sizeof(g_nid.szInfo)-1);
    g_nid.dwInfoFlags = icon;
    g_nid.uTimeout    = 6000;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
    g_nid.uFlags &= ~NIF_INFO;
}

/*
 * TryRegisterHotKey() — safe registration with graceful conflict handling.
 *
 * MOD_NOREPEAT prevents WM_HOTKEY flooding when the user holds the keys.
 * If RegisterHotKey() fails (another app owns this combo), we set
 * g_hotkeyLive = FALSE and surface a balloon — the app keeps running
 * normally; only the hotkey is unavailable.
 *
 * This is the correct, production-grade pattern. Never call
 * RegisterHotKey without checking the return value.
 */
static BOOL TryRegisterHotKey(HWND hwnd)
{
    UnregisterHotKey(hwnd, HOTKEY_APP);
    g_hotkeyLive = FALSE;
    if (!g_hkEnabled || !g_hkVK) return FALSE;

    if (RegisterHotKey(hwnd, HOTKEY_APP, g_hkMod | MOD_NOREPEAT, g_hkVK)) {
        g_hotkeyLive = TRUE;
        return TRUE;
    }

    /* ── Conflict: another process owns this combo ── */
    char hkStr[48];
    FormatHotkeyLabel(g_hkMod, g_hkVK, hkStr, sizeof(hkStr));
    char msg[240];
    snprintf(msg, sizeof(msg),
        "%s is already held by another app.\n"
        "Change it via: Tray  \xBB  Set Hotkey", hkStr);
    ShowBalloon("Hotkey Conflict", msg, NIIF_WARNING);
    return FALSE;
}

static void UpdateTrayTip(void)
{
    char tip[128];
    if (g_hotkeyLive) {
        char hkStr[48];
        FormatHotkeyLabel(g_hkMod, g_hkVK, hkStr, sizeof(hkStr));
        snprintf(tip, sizeof(tip), "PerfectCalendar  [%s]", hkStr);
    } else if (g_hkEnabled)
        strncpy(tip, "PerfectCalendar  [hotkey conflict]", sizeof(tip)-1);
    else
        strncpy(tip, "PerfectCalendar", sizeof(tip)-1);
    strncpy(g_nid.szTip, tip, sizeof(g_nid.szTip)-1);
    g_nid.uFlags |= NIF_TIP;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static void ApplyHotkey(HWND hwnd, UINT mod, UINT vk)
{
    g_hkMod     = mod;
    g_hkVK      = vk;
    g_hkEnabled = (vk != 0);
    TryRegisterHotKey(hwnd);
    UpdateTrayTip();
    SaveSettings();
}


/* ════════════════════════════════════════════════════════════════════
   §7  TRAY & STARTUP REGISTRY
   ════════════════════════════════════════════════════════════════════ */

static BOOL AddTrayIcon(HWND hwnd)
{
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_USER_TRAY;
    g_nid.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCE(IDI_APPICON));
    strncpy(g_nid.szTip, "PerfectCalendar", sizeof(g_nid.szTip)-1);
    return Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static BOOL IsRunOnStartup(void)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;
    DWORD type;
    LONG r = RegQueryValueExA(hKey, APP_NAME, NULL, &type, NULL, NULL);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS);
}

static void ToggleStartup(void)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_RUN,
            0, KEY_SET_VALUE | KEY_READ, &hKey) != ERROR_SUCCESS) return;
    if (IsRunOnStartup()) RegDeleteValueA(hKey, APP_NAME);
    else {
        char p[MAX_PATH]; GetModuleFileNameA(NULL, p, MAX_PATH);
        RegSetValueExA(hKey, APP_NAME, 0, REG_SZ, (BYTE*)p, (DWORD)(strlen(p)+1));
    }
    RegCloseKey(hKey);
}


/* ════════════════════════════════════════════════════════════════════
   §8  AGENDA LIST
   ════════════════════════════════════════════════════════════════════ */

static void RefreshAgenda(void)
{
    if (!hAgendaList || !hAgendaLbl) return;
    SendMessageA(hAgendaList, LB_RESETCONTENT, 0, 0);

    char hdr[80];
    snprintf(hdr, sizeof(hdr), "Events  /  %04d-%02d-%02d",
             g_selDate.wYear, g_selDate.wMonth, g_selDate.wDay);
    SetWindowTextA(hAgendaLbl, hdr);

    int found = 0;
    for (int i = 0; i < g_evCount; i++) {
        if (IsEventMatch(&g_ev[i], g_selDate.wYear, g_selDate.wMonth, g_selDate.wDay)) {
            char entry[MAX_DESC + 6];
            snprintf(entry, sizeof(entry), "  \xB7  %s", g_ev[i].desc);
            LRESULT idx = SendMessageA(hAgendaList, LB_ADDSTRING, 0, (LPARAM)entry);
            /* itemData = 1 → recurring (drawn in accent blue) */
            SendMessageA(hAgendaList, LB_SETITEMDATA, (WPARAM)idx,
                         (LPARAM)(g_ev[i].recurring ? 1 : 0));
            found++;
        }
    }
    if (!found) {
        LRESULT idx = SendMessageA(hAgendaList, LB_ADDSTRING, 0,
                       (LPARAM)"  Right-click calendar to add an event\x85");
        SendMessageA(hAgendaList, LB_SETITEMDATA, (WPARAM)idx, 0);
    }
}


/* ════════════════════════════════════════════════════════════════════
   §9  INLINE ADD-EVENT  (Feature A — subclassed EDIT control)
   ════════════════════════════════════════════════════════════════════
 *
 * A hidden EDIT control is overlaid on the listbox when the user
 * chooses "Add Event". No extra window class, no DialogBox call.
 *
 * SetWindowSubclass() from comctl32 v6 is the safe, modern way to
 * subclass a control — it survives nesting and avoids ANSI/Unicode
 * mismatch bugs that plagued the legacy SetWindowLongPtr approach.
 */

static LRESULT CALLBACK AddEditSubclass(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR dwRef)
{
    (void)uId; (void)dwRef;
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            char desc[MAX_DESC] = {0};
            GetWindowTextA(hwnd, desc, MAX_DESC - 1);
            /* Trim leading whitespace in-place */
            char *s = desc;
            while (*s == ' ' || *s == '\t') s++;
            size_t len = strlen(s);
            while (len && (s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = '\0';

            if (len > 0) {
                char date[12];
                snprintf(date, sizeof(date), "%04d-%02d-%02d",
                         g_selDate.wYear, g_selDate.wMonth, g_selDate.wDay);
                AppendEvent(date, s);
                LoadEvents();
                RefreshAgenda();
                /* Trigger MCN_GETDAYSTATE so the bold dot appears immediately */
                InvalidateRect(hCal, NULL, TRUE);
            }
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static void ShowInlineAddEdit(void)
{
    if (!hAddEdit) return;
    /* Snap the EDIT control to sit exactly over the listbox top */
    RECT rc; GetWindowRect(hAgendaList, &rc);
    MapWindowPoints(HWND_DESKTOP, hMain, (LPPOINT)&rc, 2);
    SetWindowPos(hAddEdit, HWND_TOP,
                 rc.left, rc.top,
                 rc.right - rc.left, g_fontSize + 16, SWP_SHOWWINDOW);
    SetWindowTextA(hAddEdit, "");
    SendMessageA(hAddEdit, EM_SETCUEBANNER, FALSE,
                 (LPARAM)L"Type event description, press Enter");
    SetFocus(hAddEdit);
}


/* ════════════════════════════════════════════════════════════════════
   §10  LAYOUT ENGINE
   ════════════════════════════════════════════════════════════════════ */

static void UpdateLayout(HWND hwnd)
{
    /* ── Leak-free font swap ── */
    HFONT oL = hFntLarge, oN = hFntNormal, oS = hFntSmall;
    int utcSz = (g_fontSize - 5 < 11) ? 11 : g_fontSize - 5;

    hFntLarge  = CreateFontA(g_fontSize + 10, 0,0,0, FW_SEMIBOLD, 0,0,0,
                              DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH|FF_DONTCARE, "Segoe UI");
    hFntNormal = CreateFontA(g_fontSize,      0,0,0, FW_NORMAL,   0,0,0,
                              DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH|FF_DONTCARE, "Segoe UI");
    hFntSmall  = CreateFontA(utcSz,           0,0,0, FW_NORMAL,   0,0,0,
                              DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH|FF_DONTCARE, "Segoe UI");

    SendMessageA(hTimeLbl,    WM_SETFONT, (WPARAM)hFntLarge,  TRUE);
    SendMessageA(hUTCLbl,     WM_SETFONT, (WPARAM)hFntSmall,  TRUE);
    SendMessageA(hCloseBtn,   WM_SETFONT, (WPARAM)hFntNormal, TRUE);
    SendMessageA(hMaxBtn,     WM_SETFONT, (WPARAM)hFntNormal, TRUE);
    SendMessageA(hCal,        WM_SETFONT, (WPARAM)hFntNormal, TRUE);
    SendMessageA(hAgendaLbl,  WM_SETFONT, (WPARAM)hFntSmall,  TRUE);
    SendMessageA(hAgendaList, WM_SETFONT, (WPARAM)hFntNormal, TRUE);
    SendMessageA(hAddEdit,    WM_SETFONT, (WPARAM)hFntNormal, TRUE);

    if (oL) DeleteObject(oL);
    if (oN) DeleteObject(oN);
    if (oS) DeleteObject(oS);

    /* ── Measure calendar minimum size ── */
    RECT calR = {0}; MonthCal_GetMinReqRect(hCal, &calR);
    int calW   = calR.right, calH = calR.bottom;
    int btnSz  = g_fontSize + 20;
    int localH = g_fontSize + 12;
    int utcH   = utcSz + 4;
    int hdrH   = localH + utcH + 10;

    if (!g_maximized) {
        int W    = calW + 20;
        int agY  = hdrH + calH + 6;
        int totH = agY + UI_AGENDA_LBL_H + UI_AGENDA_LIST_H + 14;

        SetWindowPos(hTimeLbl,    NULL, 15, 4,               200, localH,  SWP_NOZORDER);
        SetWindowPos(hUTCLbl,     NULL, 15, localH + 5,      200, utcH,    SWP_NOZORDER);
        SetWindowPos(hMaxBtn,     NULL, calW-(btnSz*2)+10, 8, btnSz, btnSz, SWP_NOZORDER);
        SetWindowPos(hCloseBtn,   NULL, calW-btnSz+15,     8, btnSz, btnSz, SWP_NOZORDER);
        SetWindowPos(hCal,        NULL, 10, hdrH,            calW,  calH,   SWP_NOZORDER);
        SetWindowPos(hAgendaLbl,  NULL, 10, agY,             W-20,  UI_AGENDA_LBL_H, SWP_NOZORDER);
        SetWindowPos(hAgendaList, NULL, 10, agY+UI_AGENDA_LBL_H+2, W-20, UI_AGENDA_LIST_H, SWP_NOZORDER);
        SetWindowPos(hwnd, NULL, 0,0, W, totH, SWP_NOMOVE|SWP_NOZORDER);
    } else {
        RECT wr = {0}; GetClientRect(hwnd, &wr);
        int calArea = (wr.bottom - hdrH) * 55 / 100;
        int agY     = hdrH + calArea;
        int agListH = wr.bottom - agY - UI_AGENDA_LBL_H - 10;
        if (agListH < 40) agListH = 40;

        SetWindowPos(hTimeLbl,    NULL, 15, 4,                  200, localH,  SWP_NOZORDER);
        SetWindowPos(hUTCLbl,     NULL, 15, localH + 5,         200, utcH,    SWP_NOZORDER);
        SetWindowPos(hMaxBtn,     NULL, wr.right-(btnSz*2)-10,8, btnSz, btnSz, SWP_NOZORDER);
        SetWindowPos(hCloseBtn,   NULL, wr.right-btnSz-5,     8, btnSz, btnSz, SWP_NOZORDER);
        SetWindowPos(hCal,        NULL, 10, hdrH, wr.right-20, calArea-5, SWP_NOZORDER);
        SetWindowPos(hAgendaLbl,  NULL, 10, agY,  wr.right-20, UI_AGENDA_LBL_H, SWP_NOZORDER);
        SetWindowPos(hAgendaList, NULL, 10, agY+UI_AGENDA_LBL_H+2, wr.right-20, agListH, SWP_NOZORDER);
    }
}


/* ════════════════════════════════════════════════════════════════════
   §11  MAIN WINDOW PROCEDURE
   ════════════════════════════════════════════════════════════════════ */

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    /* Re-add tray icon & re-register hotkey if Explorer restarts */
    if (WM_TASKBARCREATED && uMsg == WM_TASKBARCREATED) {
        AddTrayIcon(hwnd);
        TryRegisterHotKey(hwnd);
        UpdateTrayTip();
        return 0;
    }

    switch (uMsg) {

    /* ── WM_CREATE ──────────────────────────────────────────── */
    case WM_CREATE: {
        hBgBrush   = CreateSolidBrush(RGB(32, 32, 32));
        hListBrush = CreateSolidBrush(RGB(22, 22, 24));
        hSelBrush  = CreateSolidBrush(RGB(0,  90, 175));

        /* ICC_DATE_CLASSES → MONTHCAL_CLASS
           ICC_HOTKEY_CLASS → future extensibility */
        INITCOMMONCONTROLSEX icex = { sizeof(icex),
                                      ICC_DATE_CLASSES | ICC_HOTKEY_CLASS };
        InitCommonControlsEx(&icex);

        hTimeLbl  = CreateWindowExA(0,"STATIC","",
            WS_CHILD|WS_VISIBLE|SS_LEFT,    0,0,0,0,hwnd,NULL,g_hInst,NULL);
        hUTCLbl   = CreateWindowExA(0,"STATIC","",
            WS_CHILD|WS_VISIBLE|SS_LEFT,    0,0,0,0,hwnd,NULL,g_hInst,NULL);
        hCloseBtn = CreateWindowExA(0,"STATIC","X",
            WS_CHILD|WS_VISIBLE|SS_NOTIFY|SS_CENTER,
            0,0,0,0,hwnd,(HMENU)IDC_CLOSE_BTN,g_hInst,NULL);
        hMaxBtn   = CreateWindowExA(0,"STATIC","[ ]",
            WS_CHILD|WS_VISIBLE|SS_NOTIFY|SS_CENTER,
            0,0,0,0,hwnd,(HMENU)IDC_MAX_BTN,g_hInst,NULL);

        /*
         * MCS_DAYSTATE    — enables MCN_GETDAYSTATE (the bitmask API
         *                   that powers bold day rendering).
         *                   Without this flag the feature is silently disabled.
         * MCS_WEEKNUMBERS — [Feature D] ISO week-number column.
         *                   One flag. Zero code. Maximum utility.
         */
        hCal = CreateWindowExA(0, MONTHCAL_CLASS, "",
            WS_CHILD|WS_VISIBLE|MCS_DAYSTATE|MCS_WEEKNUMBERS,
            0,0,0,0,hwnd,NULL,g_hInst,NULL);

        hAgendaLbl  = CreateWindowExA(0,"STATIC","Events",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            0,0,0,0,hwnd,NULL,g_hInst,NULL);

        /*
         * LBS_OWNERDRAWFIXED + LBS_HASSTRINGS:
         *  · HASSTRINGS  — Windows stores text; LB_GETTEXT still works.
         *  · OWNERDRAW   — WM_DRAWITEM fires per item so we colour
         *                  recurring events (accent blue) vs one-time
         *                  events (white) without any external grid lib.
         */
        hAgendaList = CreateWindowExA(0,"LISTBOX","",
            WS_CHILD|WS_VISIBLE|LBS_NOTIFY|LBS_OWNERDRAWFIXED|
            LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,
            0,0,0,0,hwnd,(HMENU)IDC_AGENDA_LIST,g_hInst,NULL);

        /*
         * [Feature A] Inline EDIT control — hidden until needed.
         * Using SetWindowSubclass (comctl32 v6) instead of the legacy
         * SetWindowLongPtr / CallWindowProc pattern.
         */
        hAddEdit = CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
            WS_CHILD|ES_AUTOHSCROLL,
            0,0,0,0,hwnd,NULL,g_hInst,NULL);
        SetWindowSubclass(hAddEdit, AddEditSubclass, 0, 0);

        /* Strip visual themes so MCM_SETCOLOR values take full effect */
        SetWindowTheme(hCal, L"", L"");
        SendMessageA(hCal, MCM_SETCOLOR, MCSC_BACKGROUND,   RGB(32, 32, 32));
        SendMessageA(hCal, MCM_SETCOLOR, MCSC_MONTHBK,      RGB(115,125,138));
        SendMessageA(hCal, MCM_SETCOLOR, MCSC_TEXT,         RGB(255,255,255));
        SendMessageA(hCal, MCM_SETCOLOR, MCSC_TITLEBK,      RGB(18, 18, 20));
        SendMessageA(hCal, MCM_SETCOLOR, MCSC_TITLETEXT,    RGB(0, 150,255));
        SendMessageA(hCal, MCM_SETCOLOR, MCSC_TRAILINGTEXT, RGB(150,150,160));

        /* [Feature E] WS_EX_LAYERED was set at CreateWindowEx; init alpha */
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        g_curAlpha = 255;

        GetLocalTime(&g_selDate);
        InitEventsPath();
        LoadEvents();
        UpdateLayout(hwnd);
        RefreshAgenda();

        SetTimer(hwnd, TID_CLOCK, 1000, NULL);
        SendMessageA(hwnd, WM_TIMER, TID_CLOCK, 0);

        /* [Feature C] Global hotkey — MOD_NOREPEAT prevents key-repeat flood */
        if (g_hkEnabled) TryRegisterHotKey(hwnd);
        if (!AddTrayIcon(hwnd)) SetTimer(hwnd, TID_TRAY_RETRY, 2000, NULL);
        UpdateTrayTip();
        return 0;
    }

    /* ── WM_MEASUREITEM — owner-draw item height ────────────── */
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis->CtlID == IDC_AGENDA_LIST)
            mis->itemHeight = (UINT)(g_fontSize + 8);
        return TRUE;
    }

    /* ── WM_DRAWITEM — owner-draw agenda listbox ────────────── */
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->hwndItem != hAgendaList || dis->itemID == (UINT)-1) break;

        BOOL sel = (dis->itemState & ODS_SELECTED) != 0;
        BOOL rec = (dis->itemData == 1);   /* itemData set in RefreshAgenda */

        FillRect(dis->hDC, &dis->rcItem, sel ? hSelBrush : hListBrush);

        char txt[MAX_DESC + 10] = {0};
        SendMessageA(hAgendaList, LB_GETTEXT, dis->itemID, (LPARAM)txt);

        COLORREF col;
        if      (sel)                          col = RGB(255,255,255);
        else if (rec)                          col = RGB(90, 195,255);
        else if (strstr(txt, "Right-click"))   col = RGB(95, 95, 108);
        else                                   col = RGB(215,215,220);

        SetTextColor(dis->hDC, col);
        SetBkMode(dis->hDC, TRANSPARENT);
        SelectObject(dis->hDC, hFntNormal);
        RECT r = dis->rcItem; r.left += 6;
        DrawTextA(dis->hDC, txt, -1, &r,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if (dis->itemState & ODS_FOCUS)
            DrawFocusRect(dis->hDC, &dis->rcItem);
        return TRUE;
    }

    /* ── WM_HOTKEY — [Feature C] global summon ──────────────── */
    case WM_HOTKEY: {
        if (wParam != HOTKEY_APP || !g_hotkeyLive) break;

        if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
        } else {
            POINT pt; GetCursorPos(&pt);
            RECT wr, wa;
            GetWindowRect(hwnd, &wr);
            SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
            int W = wr.right-wr.left, H = wr.bottom-wr.top;
            int x = pt.x-W/2, y = pt.y-H/2;
            if (x < wa.left)       x = wa.left;
            if (y < wa.top)        y = wa.top;
            if (x+W > wa.right)    x = wa.right-W;
            if (y+H > wa.bottom)   y = wa.bottom-H;

            /* TOPMOST flash grabs foreground; released immediately after */
            SetWindowPos(hwnd, HWND_TOPMOST,   x, y, 0,0, SWP_NOSIZE);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0,0, SWP_NOMOVE|SWP_NOSIZE);
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
            g_curAlpha = 255;
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        return 0;
    }

    /* ── WM_NOTIFY — calendar notifications ─────────────────── */
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR*)lParam;
        if (hdr->hwndFrom != hCal) break;

        switch (hdr->code) {

        /*
         * [Feature 1] MCN_SELCHANGE
         * Fires on every date change (click, keyboard, navigation).
         * Updates g_selDate and refreshes the agenda listbox.
         * This + MCN_GETDAYSTATE together form the core showcase of
         * Win32 WM_NOTIFY handling for portfolio purposes.
         */
        case MCN_SELCHANGE: {
            NMSELCHANGE *psc = (NMSELCHANGE*)lParam;
            g_selDate = psc->stSelStart;
            RefreshAgenda();
            ShowWindow(hAddEdit, SW_HIDE);
            return 0;
        }

        /*
         * [Feature 2] MCN_GETDAYSTATE — the bitmask bridge.
         *
         * The calendar asks: "which days this month have events?"
         * We answer with a 32-bit MONTHDAYSTATE where bit (N-1)
         * set = day N is rendered BOLD.
         *
         * GetDayStateMask() handles all four recurring patterns via
         * IsEventMatch() — no extra rendering code anywhere.
         * This entire feature is ~25 lines of C and zero extra libraries.
         */
        case MCN_GETDAYSTATE: {
            NMDAYSTATE *pds = (NMDAYSTATE*)lParam;
            SYSTEMTIME st   = pds->stStart;
            for (int i = 0; i < pds->cDayState; i++) {
                pds->prgDayState[i] = GetDayStateMask(st.wYear, st.wMonth);
                if (++st.wMonth > 12) { st.wMonth = 1; st.wYear++; }
            }
            return 0;
        }

        /*
         * [Feature 4] NM_RCLICK — multi-format copy + add event.
         */
        case NM_RCLICK: {
            char iso[12], us[12], longfmt[40];
            snprintf(iso, sizeof(iso), "%04d-%02d-%02d",
                     g_selDate.wYear, g_selDate.wMonth, g_selDate.wDay);
            snprintf(us,  sizeof(us),  "%02d/%02d/%04d",
                     g_selDate.wMonth, g_selDate.wDay, g_selDate.wYear);
            char mname[20] = {0};
            SYSTEMTIME st = g_selDate;
            GetDateFormatA(LOCALE_USER_DEFAULT, 0, &st, "MMMM", mname, sizeof(mname));
            snprintf(longfmt, sizeof(longfmt), "%s %d, %04d",
                     mname, g_selDate.wDay, g_selDate.wYear);

            char li[50], lu[50], ll[56];
            snprintf(li, sizeof(li), "Copy  %s", iso);
            snprintf(lu, sizeof(lu), "Copy  %s", us);
            snprintf(ll, sizeof(ll), "Copy  %s", longfmt);

            POINT pt; GetCursorPos(&pt);
            HMENU hm = CreatePopupMenu();
            AppendMenuA(hm, MF_STRING, IDM_COPY_ISO,   li);
            AppendMenuA(hm, MF_STRING, IDM_COPY_US,    lu);
            AppendMenuA(hm, MF_STRING, IDM_COPY_LONG,  ll);
            AppendMenuA(hm, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hm, MF_STRING, IDM_ADD_EVENT,  "Add Event for This Date");
            AppendMenuA(hm, MF_STRING, IDM_RELOAD_CTX, "Reload Events");

            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hm, TPM_RETURNCMD|TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hm);

            const char *copied = NULL;
            if      (cmd == IDM_COPY_ISO)  copied = iso;
            else if (cmd == IDM_COPY_US)   copied = us;
            else if (cmd == IDM_COPY_LONG) copied = longfmt;
            if (copied) {
                CopyToClipboard(hwnd, copied);
                SetWindowTextA(hwnd, "Copied!");
                SetTimer(hwnd, TID_TITLE_FLASH, 1200, NULL);
            }
            else if (cmd == IDM_ADD_EVENT)  ShowInlineAddEdit();
            else if (cmd == IDM_RELOAD_CTX) {
                LoadEvents(); RefreshAgenda(); InvalidateRect(hCal, NULL, TRUE);
            }
            return 0;
        }
        } /* switch hdr->code */
        return 0;
    }

    /* ── WM_USER_TRAY — system tray ──────────────────────────── */
    case WM_USER_TRAY: {
        if (lParam == WM_LBUTTONUP) {
            if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
            else {
                SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
                g_curAlpha = 255;
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
                BringWindowToTop(hwnd);
            }
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);

            /*
             * [Feature C] Hotkey submenu — lets users pick a preset combo
             * or disable entirely, stored in registry, registered immediately.
             * Each item is check-marked to show the active selection.
             * If RegisterHotKey failed due to conflict, the label says so.
             */
            char hkLabel[80];
            if (g_hkEnabled && g_hkVK) {
                char hkStr[48];
                FormatHotkeyLabel(g_hkMod, g_hkVK, hkStr, sizeof(hkStr));
                snprintf(hkLabel, sizeof(hkLabel), "Active:  %s%s",
                         hkStr, g_hotkeyLive ? "" : "  [conflict!]");
            } else {
                strncpy(hkLabel, "Active:  (disabled)", sizeof(hkLabel)-1);
            }

            BOOL cCS  = (g_hkEnabled && g_hkVK == VK_SPACE &&
                         g_hkMod == (MOD_CONTROL|MOD_SHIFT));
            BOOL cCA  = (g_hkEnabled && g_hkVK == 'C' &&
                         g_hkMod == (MOD_CONTROL|MOD_ALT));
            BOOL cWS  = (g_hkEnabled && g_hkVK == 'C' &&
                         g_hkMod == (MOD_WIN|MOD_SHIFT));
            BOOL cDis = (!g_hkEnabled || !g_hkVK);

            HMENU hkSub = CreatePopupMenu();
            AppendMenuA(hkSub, MF_STRING|MF_GRAYED, 0, hkLabel);
            AppendMenuA(hkSub, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hkSub, MF_STRING|(cCS  ? MF_CHECKED : 0),
                        IDM_HK_CTRLSHIFT, "Ctrl + Shift + Space");
            AppendMenuA(hkSub, MF_STRING|(cCA  ? MF_CHECKED : 0),
                        IDM_HK_CTRLALT,   "Ctrl + Alt + C");
            AppendMenuA(hkSub, MF_STRING|(cWS  ? MF_CHECKED : 0),
                        IDM_HK_WINSHIFT,  "Win + Shift + C");
            AppendMenuA(hkSub, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hkSub, MF_STRING|(cDis ? MF_CHECKED : 0),
                        IDM_HK_DISABLED,  "Disabled");

            HMENU hm = CreatePopupMenu();
            AppendMenuA(hm, MF_STRING|(IsRunOnStartup() ? MF_CHECKED : 0),
                        IDM_STARTUP, "Run on Startup");
            AppendMenuA(hm, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hm, MF_STRING|MF_POPUP, (UINT_PTR)hkSub, "Set Hotkey");
            AppendMenuA(hm, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hm, MF_STRING, IDM_OPEN_EVENTS,   "Open Events File");
            AppendMenuA(hm, MF_STRING, IDM_RELOAD_EVENTS, "Reload Events");
            AppendMenuA(hm, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hm, MF_STRING, IDM_EXIT, "Exit");

            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hm, TPM_RETURNCMD|TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hm); /* recursive — also frees hkSub */

            switch (cmd) {
            case IDM_EXIT:           PostQuitMessage(0); break;
            case IDM_STARTUP:        ToggleStartup(); break;
            case IDM_OPEN_EVENTS:
                ShellExecuteA(NULL,"open",g_evPath,NULL,NULL,SW_SHOWNORMAL); break;
            case IDM_RELOAD_EVENTS:
                LoadEvents(); RefreshAgenda(); InvalidateRect(hCal,NULL,TRUE); break;
            case IDM_HK_CTRLSHIFT:
                ApplyHotkey(hwnd, MOD_CONTROL|MOD_SHIFT, VK_SPACE); break;
            case IDM_HK_CTRLALT:
                ApplyHotkey(hwnd, MOD_CONTROL|MOD_ALT, 'C'); break;
            case IDM_HK_WINSHIFT:
                ApplyHotkey(hwnd, MOD_WIN|MOD_SHIFT, 'C'); break;
            case IDM_HK_DISABLED:
                ApplyHotkey(hwnd, 0, 0); break;
            }
        }
        return 0;
    }

    /* ── WM_MOUSEWHEEL — font-size zoom ──────────────────────── */
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if      (delta > 0 && g_fontSize < 40) g_fontSize += 2;
        else if (delta < 0 && g_fontSize > 12) g_fontSize -= 2;
        UpdateLayout(hwnd);
        SaveSettings();
        return 0;
    }

    /* ── [Feature E] Mouse tracking ──────────────────────────── */
    case WM_MOUSEMOVE: {
        if (!g_mouseIn) {
            g_mouseIn = TRUE;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            KillTimer(hwnd, TID_FADE);
            g_curAlpha = 255;
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        g_mouseIn = FALSE;
        if (g_idleAlpha < 255)
            SetTimer(hwnd, TID_FADE, 30, NULL); /* smooth fade starts */
        return 0;
    }

    /* ── WM_TIMER ─────────────────────────────────────────────── */
    case WM_TIMER: {
        switch (wParam) {
        case TID_CLOCK: {
            /* [Feature 5] Dual clock — local + UTC */
            SYSTEMTIME local, utc;
            GetLocalTime(&local); GetSystemTime(&utc);
            char ls[32]; GetTimeFormatA(LOCALE_USER_DEFAULT,0,&local,NULL,ls,sizeof(ls));
            char us[24]; snprintf(us,sizeof(us),"UTC  %02d:%02d",utc.wHour,utc.wMinute);
            SetWindowTextA(hTimeLbl, ls);
            SetWindowTextA(hUTCLbl,  us);
            break;
        }
        case TID_TRAY_RETRY:
            if (AddTrayIcon(hwnd)) {
                KillTimer(hwnd, TID_TRAY_RETRY);
                UpdateTrayTip();
            }
            break;
        case TID_TITLE_FLASH:
            SetWindowTextA(hwnd, "Taskbar Calendar");
            KillTimer(hwnd, TID_TITLE_FLASH);
            break;
        case TID_FADE: {
            /*
             * [Feature E] Smooth fade to g_idleAlpha.
             * Steps of 14 every 30 ms ≈ ~400 ms total fade at max range.
             * Stopped immediately on WM_MOUSEMOVE.
             */
            if (g_curAlpha > g_idleAlpha) {
                BYTE step = 14;
                g_curAlpha = (g_curAlpha > g_idleAlpha + step)
                             ? g_curAlpha - step : g_idleAlpha;
                SetLayeredWindowAttributes(hwnd, 0, g_curAlpha, LWA_ALPHA);
            }
            if (g_curAlpha <= g_idleAlpha) KillTimer(hwnd, TID_FADE);
            break;
        }
        }
        return 0;
    }

    /* ── Colour messages — unified dark theme ─────────────────── */
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam; HWND hCtrl = (HWND)lParam;
        SetBkMode(hdc, OPAQUE); SetBkColor(hdc, RGB(32,32,32));
        if (hCtrl == hUTCLbl || hCtrl == hAgendaLbl)
            SetTextColor(hdc, RGB(130,148,168));
        else
            SetTextColor(hdc, RGB(255,255,255));
        return (INT_PTR)hBgBrush;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, hBgBrush);
        return 1;
    }

    /* ── WM_COMMAND — button clicks ──────────────────────────── */
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_CLOSE_BTN) {
            ShowWindow(hwnd, SW_HIDE);
        } else if (id == IDC_MAX_BTN) {
            g_maximized = !g_maximized;
            if (g_maximized) {
                GetWindowRect(hwnd, &g_normalRect);
                RECT wa; SystemParametersInfoA(SPI_GETWORKAREA,0,&wa,0);
                SetWindowPos(hwnd, NULL, wa.left, wa.top,
                    wa.right-wa.left, wa.bottom-wa.top, SWP_NOZORDER);
                SetWindowTextA(hMaxBtn, "[_]");
            } else {
                SetWindowPos(hwnd, NULL,
                    g_normalRect.left,  g_normalRect.top,
                    g_normalRect.right -g_normalRect.left,
                    g_normalRect.bottom-g_normalRect.top, SWP_NOZORDER);
                SetWindowTextA(hMaxBtn, "[ ]");
            }
            UpdateLayout(hwnd);
        }
        return 0;
    }

    /* ── WM_NCHITTEST — header drag zone ─────────────────────── */
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcA(hwnd, uMsg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            int utcSz = (g_fontSize-5 < 11) ? 11 : g_fontSize-5;
            int hdrH  = (g_fontSize+12) + (utcSz+4) + 10;
            if (pt.y < hdrH && pt.x < UI_DRAG_WIDTH) return HTCAPTION;
        }
        return hit;
    }

    /* ── WM_KEYDOWN — F5 = hot-reload events ─────────────────── */
    case WM_KEYDOWN:
        if (wParam == VK_F5) {
            LoadEvents(); RefreshAgenda(); InvalidateRect(hCal,NULL,TRUE);
        }
        return 0;

    /* ── WM_DESTROY — full deterministic cleanup ─────────────── */
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_APP);
        KillTimer(hwnd, TID_CLOCK);
        KillTimer(hwnd, TID_TRAY_RETRY);
        KillTimer(hwnd, TID_TITLE_FLASH);
        KillTimer(hwnd, TID_FADE);
        SaveSettings();
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        DeleteObject(hBgBrush);
        DeleteObject(hListBrush);
        DeleteObject(hSelBrush);
        if (hFntLarge)  DeleteObject(hFntLarge);
        if (hFntNormal) DeleteObject(hFntNormal);
        if (hFntSmall)  DeleteObject(hFntSmall);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}


/* ════════════════════════════════════════════════════════════════════
   §12  ENTRY POINT
   ════════════════════════════════════════════════════════════════════ */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR pCmd, int nShow)
{
    (void)hPrev; (void)pCmd; (void)nShow;
    g_hInst = hInst;

    /* HiDPI — prevents scaling blur on 4K / 125% / 150% monitors */
    SetProcessDPIAware();

    /* Listen for Explorer restarts so we can re-add the tray icon */
    WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");

    /* ── Single-instance guard ── */
    HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hEx = FindWindowA(WC_MAIN, "Taskbar Calendar");
        if (hEx) { ShowWindow(hEx, SW_SHOW); SetForegroundWindow(hEx); }
        CloseHandle(hMutex);
        return 0;
    }

    /* Load registry settings before any window is created so that
       the first UpdateLayout call uses the user's saved font size. */
    LoadSettings();

    WNDCLASSA wc  = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WC_MAIN;
    wc.hIcon         = LoadIconA(hInst, MAKEINTRESOURCE(IDI_APPICON));
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    /* Place in bottom-right of work area (taskbar-friendly default) */
    RECT wa = {0}; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);

    /*
     * WS_EX_TOOLWINDOW — no taskbar button, excluded from Alt+Tab.
     * WS_EX_LAYERED    — required for SetLayeredWindowAttributes.
     *
     * NOTE: ShowWindow is intentionally NOT called here.
     * The app boots invisibly to the system tray.
     * If the user has "Run on Startup" enabled, they will NOT see a
     * calendar window forcibly popped on their desktop at every boot.
     * The user summons it via the tray icon or the global hotkey.
     */
    hMain = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WC_MAIN, "Taskbar Calendar",
        WS_POPUP,
        wa.right - 380, wa.bottom - 620,
        360, 590,
        NULL, NULL, hInst, NULL);

    if (!hMain) {
        MessageBoxA(NULL, "Failed to create main window.", APP_NAME, MB_ICONERROR);
        ReleaseMutex(hMutex); CloseHandle(hMutex);
        return 1;
    }

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    ReleaseMutex(hMutex); CloseHandle(hMutex);
    return (int)msg.wParam;
}
