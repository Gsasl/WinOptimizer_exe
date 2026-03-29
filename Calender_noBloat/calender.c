#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

#define WM_USER_TRAY (WM_USER + 1)
#define IDI_APPICON 101 

NOTIFYICONDATA nid = {0};
HWND hMainWnd, hMonthCal, hTimeLabel, hCloseBtn, hMaxBtn;
HFONT hFontLarge = NULL, hFontNormal = NULL;
HBRUSH hbgBrush;
int currentFontSize = 20; 

BOOL isMaximized = FALSE;
RECT normalRect; 

// Global message ID for the Taskbar
UINT WM_TASKBARCREATED = 0; 

// --- BUG 2 FIX: Dedicated, robust function to safely inject the icon ---
void AddTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TRAY;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON)); 
    lstrcpyA(nid.szTip, "Modern Calendar");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void UpdateSizing(HWND hwnd) {
    if (hFontLarge) DeleteObject(hFontLarge);
    if (hFontNormal) DeleteObject(hFontNormal);

    hFontLarge = CreateFont(currentFontSize + 10, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Segoe UI"));
    hFontNormal = CreateFont(currentFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Segoe UI"));

    SendMessage(hTimeLabel, WM_SETFONT, (WPARAM)hFontLarge, TRUE);
    SendMessage(hCloseBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    SendMessage(hMaxBtn, WM_SETFONT, (WPARAM)hFontNormal, TRUE); 
    SendMessage(hMonthCal, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    RECT rc;
    MonthCal_GetMinReqRect(hMonthCal, &rc);
    int calW = rc.right;
    int calH = rc.bottom;
    int headerHeight = currentFontSize + 25; 
    
    int btnSize = currentFontSize + 20;

    if (!isMaximized) {
        SetWindowPos(hMonthCal, NULL, 10, headerHeight, calW, calH, SWP_NOZORDER);
        SetWindowPos(hMaxBtn, NULL, calW - (btnSize * 2) + 10, 10, btnSize, btnSize, SWP_NOZORDER);
        SetWindowPos(hCloseBtn, NULL, calW - btnSize + 15, 10, btnSize, btnSize, SWP_NOZORDER);
        SetWindowPos(hTimeLabel, NULL, 15, 10, 200, currentFontSize + 15, SWP_NOZORDER);
        SetWindowPos(hwnd, NULL, 0, 0, calW + 20, calH + headerHeight + 15, SWP_NOMOVE | SWP_NOZORDER);
    } else {
        RECT winRect;
        GetClientRect(hwnd, &winRect);
        SetWindowPos(hMaxBtn, NULL, winRect.right - (btnSize * 2) - 10, 10, btnSize, btnSize, SWP_NOZORDER);
        SetWindowPos(hCloseBtn, NULL, winRect.right - btnSize - 5, 10, btnSize, btnSize, SWP_NOZORDER);
        SetWindowPos(hTimeLabel, NULL, 15, 10, 200, currentFontSize + 15, SWP_NOZORDER);
        SetWindowPos(hMonthCal, NULL, 10, headerHeight, winRect.right - 20, winRect.bottom - headerHeight - 10, SWP_NOZORDER);
    }
}
// Checks if the app is currently set to run on startup
BOOL IsRunOnStartup() {
    HKEY hKey;
    LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (lRes == ERROR_SUCCESS) {
        DWORD type;
        lRes = RegQueryValueExA(hKey, "PerfectCalendar", NULL, &type, NULL, NULL);
        RegCloseKey(hKey);
        return (lRes == ERROR_SUCCESS);
    }
    return FALSE;
}

// Toggles the registry key on or off
void ToggleRunOnStartup() {
    HKEY hKey;
    LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_READ, &hKey);
    if (lRes == ERROR_SUCCESS) {
        if (IsRunOnStartup()) {
            // If it's on, delete the key to turn it off
            RegDeleteValueA(hKey, "PerfectCalendar");
        } else {
            // If it's off, get the exact path of the .exe and add it to the registry
            char szPath[MAX_PATH];
            GetModuleFileNameA(NULL, szPath, MAX_PATH);
            RegSetValueExA(hKey, "PerfectCalendar", 0, REG_SZ, (BYTE*)szPath, strlen(szPath) + 1);
        }
        RegCloseKey(hKey);
    }
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    
    // Safely re-inject the icon if the taskbar loads late (on boot) or crashes
    if (WM_TASKBARCREATED != 0 && uMsg == WM_TASKBARCREATED) {
        AddTrayIcon(hwnd);
        return 0;
    }

    switch (uMsg) {
        case WM_CREATE: {
            hbgBrush = CreateSolidBrush(RGB(32, 32, 32)); 

            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_DATE_CLASSES;
            InitCommonControlsEx(&icex);

            hTimeLabel = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
            hCloseBtn = CreateWindowEx(0, "STATIC", "X", WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER, 0, 0, 0, 0, hwnd, (HMENU)1, NULL, NULL);
            hMaxBtn = CreateWindowEx(0, "STATIC", "[ ]", WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER, 0, 0, 0, 0, hwnd, (HMENU)2, NULL, NULL);
            hMonthCal = CreateWindowEx(0, MONTHCAL_CLASS, "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
            
            SetWindowTheme(hMonthCal, L"", L""); 
            SendMessage(hMonthCal, MCM_SETCOLOR, MCSC_BACKGROUND, RGB(32, 32, 32));   
            SendMessage(hMonthCal, MCM_SETCOLOR, MCSC_MONTHBK, RGB(120, 130, 140));      
            SendMessage(hMonthCal, MCM_SETCOLOR, MCSC_TEXT, RGB(255, 255, 255));      
            SendMessage(hMonthCal, MCM_SETCOLOR, MCSC_TITLEBK, RGB(20, 20, 20));      
            SendMessage(hMonthCal, MCM_SETCOLOR, MCSC_TITLETEXT, RGB(0, 150, 255));   
            SendMessage(hMonthCal, MCM_SETCOLOR, MCSC_TRAILINGTEXT, RGB(210, 210, 210)); 

            UpdateSizing(hwnd);
            SetTimer(hwnd, 1, 1000, NULL);
            SendMessage(hwnd, WM_TIMER, 1, 0); 

            // Initial attempt to add the tray icon
            AddTrayIcon(hwnd);
            return 0;
        }

        case WM_USER_TRAY: {
            if (lParam == WM_LBUTTONUP) { 
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE); 
                } else {
                    ShowWindow(hwnd, SW_SHOW);
                    // Ensure it pops to the front when you click the tray icon
                    SetForegroundWindow(hwnd);
                    BringWindowToTop(hwnd);
                }
            } else if (lParam == WM_RBUTTONUP) { 
                // 1. Get exact mouse coordinates
                POINT pt;
                GetCursorPos(&pt);
                
                // 2. Create an empty menu
                HMENU hMenu = CreatePopupMenu();
                
                // 3. Add the "Run on Startup" option with a dynamic checkmark
                UINT uFlags = MF_STRING;
                if (IsRunOnStartup()) {
                    uFlags |= MF_CHECKED;
                } else {
                    uFlags |= MF_UNCHECKED;
                }
                AppendMenuA(hMenu, uFlags, 1002, "Run on Startup");
                
                // 4. Add a visual separator line
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                
                // 5. Add the Exit button
                AppendMenuA(hMenu, MF_STRING, 1001, "Exit");
                
                // 6. Fix Windows bug where menu gets stuck open
                SetForegroundWindow(hwnd); 
                
                // 7. Show the menu and capture the user's click
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                
                // 8. Execute the command
                if (cmd == 1001) {
                    PostQuitMessage(0); // Exit
                } else if (cmd == 1002) {
                    ToggleRunOnStartup(); // Toggle Registry
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (zDelta > 0 && currentFontSize < 40) {
                currentFontSize += 2;
                UpdateSizing(hwnd);
            } else if (zDelta < 0 && currentFontSize > 12) {
                currentFontSize -= 2;
                UpdateSizing(hwnd);
            }
            return 0;
        }

        case WM_TIMER: {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char timeStr[64];
            GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &st, NULL, timeStr, sizeof(timeStr));
            SetWindowTextA(hTimeLabel, timeStr);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(255, 255, 255)); 
            SetBkColor(hdcStatic, RGB(32, 32, 32));      
            return (INT_PTR)hbgBrush;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, hbgBrush);
            return 1;
        }

        case WM_NCLBUTTONDBLCLK: {
            SendMessage(hwnd, WM_COMMAND, 2, 0); 
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == 1) { 
                ShowWindow(hwnd, SW_HIDE);
            } 
            else if (id == 2) { 
                isMaximized = !isMaximized;
                if (isMaximized) {
                    GetWindowRect(hwnd, &normalRect); 
                    RECT workArea;
                    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0); 
                    SetWindowPos(hwnd, NULL, workArea.left, workArea.top, workArea.right - workArea.left, workArea.bottom - workArea.top, SWP_NOZORDER);
                    SetWindowText(hMaxBtn, "[_]");
                } else {
                    SetWindowPos(hwnd, NULL, normalRect.left, normalRect.top, normalRect.right - normalRect.left, normalRect.bottom - normalRect.top, SWP_NOZORDER);
                    SetWindowText(hMaxBtn, "[ ]");
                }
                UpdateSizing(hwnd);
            }
            return 0;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);
                if (pt.y < (currentFontSize + 25) && pt.x < 250) return HTCAPTION; 
            }
            return hit;
        }

        case WM_DESTROY: {
            Shell_NotifyIcon(NIM_DELETE, &nid); 
            DeleteObject(hbgBrush);
            if (hFontLarge) DeleteObject(hFontLarge);
            if (hFontNormal) DeleteObject(hFontNormal);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
    
    // Register this listener immediately before the window is even created
    WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");

    HANDLE hMutex = CreateMutex(NULL, TRUE, "ModernCalendarMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindow("MinimalCalendarClass", "Taskbar Calendar");
        if (hExisting) {
            ShowWindow(hExisting, SW_SHOW);
            SetForegroundWindow(hExisting);
        }
        return 0; 
    }

    const char CLASS_NAME[] = "MinimalCalendarClass";
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)); 
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // --- BUG 1 FIX: Removed WS_EX_TOPMOST. It is now a normal tool window. ---
    hMainWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        CLASS_NAME, "Taskbar Calendar",
        WS_POPUP, 
        screenWidth - 340, screenHeight - 420, 
        320, 380,
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hMainWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}