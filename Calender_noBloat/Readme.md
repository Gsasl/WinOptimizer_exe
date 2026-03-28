# PerfectCalendar 📅 (Zero-Bloat Win32)

A lightning-fast, ultra-lightweight Windows taskbar calendar written entirely in pure C using the native Win32 API. 

Modern operating systems have slowly replaced fast, native utilities with bloated web-wrappers and heavy UI frameworks. PerfectCalendar is a direct response to that trend. It provides a clean, dark-mode native experience without the telemetry, cloud-syncing, or background resource hogging.
<img width="1383" height="845" alt="Screenshot 2026-03-28 220714" src="https://github.com/user-attachments/assets/9fd170eb-c510-4268-8edf-fc8650b1e28a" />
<img width="1156" height="864" alt="Screenshot 2026-03-28 192243" src="https://github.com/user-attachments/assets/42c2861d-1f82-4985-ae3d-36ca99633306" />



## 📊 The "Anti-Bloat" Comparison

| Metric | Modern Windows UI Apps | PerfectCalendar (Pure C) |
| :--- | :--- | :--- |
| **Idle RAM Usage** | 50 MB - 150 MB+ | **~2 MB** |
| **Disk Space** | 10 MB - 50 MB+ | **~200 KB** |
| **Dependencies** | WebView2, WinUI3, .NET | **None** (Native `comctl32.dll`) |
| **Background Processes** | Telemetry, Sync engines | **Zero** (Pure idle loop) |
| **Launch Speed** | Noticeable delay | **Instantaneous** |

## ✨ Features
* **Native System Tray Integration:** Runs silently in the background and pins directly to your taskbar.
* **Dynamic Scaling:** Hover and scroll the mouse wheel to smoothly zoom/resize the UI fonts on the fly.
* **Dark Mode Native:** Uses custom `MCM_SETCOLOR` messages to override the blinding Windows 95 default calendar colors.
* **Singleton Architecture:** Uses a `CreateMutex` lock to ensure only one instance can ever run at a time, preventing accidental duplicate background processes.

## 🛠️ Under the Hood: Engineering Considerations
Building directly on the metal of the Win32 API requires carefully navigating OS-level quirks. Here are a few ways this app was optimized for stability:

* **Stutter-Free Full-Screen Gaming:** Standard background apps often fight for screen focus (`WS_EX_TOPMOST`), causing 1-2 second frame drops when alt-tabbing out of heavy DirectX/Vulkan games. This app uses an asynchronous `PostMessage` strategy during `WM_ACTIVATE` to instantly and smoothly yield focus back to the OS.
* **Explorer Crash Resilience:** If the Windows Desktop Window Manager (`explorer.exe`) crashes, it wipes the System Tray. PerfectCalendar actively listens for the system-wide `TaskbarCreated` broadcast message to instantly rebuild and re-inject its icon without requiring a restart.
* **Strict GDI Memory Management:** To allow infinite, smooth mouse-wheel zooming without memory leaks, old `HFONT` Graphic Device Interface objects are systematically destroyed (`DeleteObject`) before new scaling fonts are painted to the device context.

## 🚀 Installation & Build

**Download the Pre-Compiled Binary:**
1. Grab `PerfectCalendar.exe` from the **Releases** tab.
2. Press `Win + R`, type `shell:startup`, and place a shortcut to the `.exe` inside to have it boot silently with Windows.

**Compile it Yourself (GCC / MinGW):**
```bash
# 1. Compile the icon resource file
windres app.rc -O coff -o app.res

# 2. Compile the executable
gcc calender.c app.res -o PerfectCalendar.exe -mwindows -lcomctl32 -luxtheme -lgdi32
