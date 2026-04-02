# PerfectCalendar 📅
### A zero-bloat Win32 taskbar calendar in pure C

<p align="left">
  <img alt="Platform" src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078d4?logo=windows">
  <img alt="Language" src="https://img.shields.io/badge/Language-Pure%20C%20%28Win32%29-f34b7d">
  <img alt="Binary size" src="https://img.shields.io/badge/Binary-~230%20KB-22c55e">
  <img alt="RAM" src="https://img.shields.io/badge/Idle%20RAM-%7E2%20MB-22c55e">
  <img alt="Winget" src="https://img.shields.io/badge/winget-Gsasl.PerfectCalendar-0078d4?logo=windows-terminal">
  <img alt="License" src="https://img.shields.io/badge/License-MIT-brightgreen">
</p>

> Modern app stores keep selling you 80 MB Electron wrappers to show a calendar.  
> This does the same job in **230 kilobytes** with **no runtime, no installer, no phone-home.**

<img width="1383" height="845" alt="Screenshot 2026-03-28 220714" src="https://github.com/user-attachments/assets/9fd170eb-c510-4268-8edf-fc8650b1e28a" />
<img width="1156" height="864" alt="Screenshot 2026-03-28 192243" src="https://github.com/user-attachments/assets/42c2861d-1f82-4985-ae3d-36ca99633306" />

---

## Install in one line

```powershell
winget install Gsasl.PerfectCalendar
```

Or grab `PerfectCalendar.exe` from the [Releases](../../releases) tab and run it. That's it — no installer wizard, no UAC prompt, no `C:\Program Files` folder.

---

## What it does

PerfectCalendar lives silently in your system tray. Press **Ctrl+Shift+Space** from any app and it teleports to your cursor. Select a date and your events appear instantly. Close it and it disappears without leaving a taskbar button.

**For power users:**
- Global hotkey summons the calendar to wherever your cursor is
- Flat-file event storage you actually own (`events.dat` — plain text, open in Notepad)
- Bold dots on dates that have events, rendered by the OS with zero custom drawing code
- Multi-format date copy (ISO 8601, US, long form) via right-click
- ISO week-number column
- UTC clock alongside your local time
- Mouse-wheel font zoom, idle fade transparency

**For everyone:**
- Dark mode, no configuration needed
- Runs at startup without popping up on your screen — tray-only, as it should be
- 4K / HiDPI aware out of the box
- Survives Explorer crashes; re-injects tray icon automatically

---

## The numbers that matter

| | Modern calendar apps | PerfectCalendar |
|---|---|---|
| **Install size** | 10 – 80 MB | **~230 KB** |
| **Idle RAM** | 50 – 150 MB | **~2 MB** |
| **Dependencies** | .NET / WebView2 / WinUI3 | **None** (OS `comctl32.dll`) |
| **Background processes** | Telemetry, sync daemons | **Zero** |
| **Startup impact** | Measured in seconds | **Unmeasurable** |
| **Your data** | Cloud account required | **Local flat file you own** |

---

## Events file

Events are stored in `%APPDATA%\PerfectCalendar\events.dat`. Open it in any text editor:

```
# One-time event
2026-09-01|First day of semester

# Yearly recurring (birthday, anniversary)
****-07-04|Independence Day
****-12-25|Christmas

# Monthly recurring (rent, meetings)
****-**-01|Rent due
****-**-15|Sprint review
```

Days with events appear **bold** on the calendar automatically. Recurring events are colour-coded blue in the agenda list. Right-click any date to add a new event inline — no dialog box, just type and press Enter.

**Hot-reload at any time:** press `F5` or use the tray menu → Reload Events.

---

## Hotkey

Default is **Ctrl+Shift+Space**. Change it anytime via the tray icon → **Set Hotkey**:

| Preset | Combination |
|---|---|
| Default | Ctrl + Shift + Space |
| Dev-friendly | Ctrl + Alt + C |
| Windows-key | Win + Shift + C |
| Off | Disabled |

If another app already holds your chosen combo, PerfectCalendar shows a tray notification and keeps running normally — it never crashes or fights for the shortcut.

---

## Transparency

When your mouse leaves the window, the calendar fades to a configurable opacity (default 63%). Touch it and it snaps back to full opacity instantly. No animated nonsense — just a practical visual cue that tells you the window is idle.

---

## Build it yourself

You need [MinGW-w64](https://www.mingw-w64.org/) (or MSVC). Nothing else.

```bash
# 1. Compile icon resource
windres app.rc -O coff -o app.res

# 2. Compile executable  (~230 KB stripped)
gcc calender.c app.res -o PerfectCalendar.exe \
    -mwindows -lcomctl32 -luxtheme -lshell32 -ladvapi32 -O2 -s
```

The output binary has zero DLL dependencies beyond what every Windows installation already ships with.

---

## For developers: the architecture

This project is a practical reference for several non-trivial Win32 patterns that aren't well-documented together anywhere.

### Flat-file events + bitmask rendering (the "wow" feature)

The combination of flat-file I/O, bitmask manipulation, and `WM_NOTIFY` handling is the core showcase. Here is how the bold-day rendering works end to end — without a single external library:

```
events.dat (plain text)
       │
       ▼  fopen / fgets / sscanf
  g_ev[]  (in-memory CalEvent array)
       │
       ▼  IsEventMatch() — wildcard comparison
  GetDayStateMask(year, month)
       │  returns MONTHDAYSTATE (32-bit integer)
       │  bit N = 1 means day (N+1) is bold
       ▼
  MCN_GETDAYSTATE handler
       │  pds->prgDayState[i] = mask
       ▼
  MONTHCAL_CLASS native renderer
       │  reads bitmask, bolds matching day numbers
       ▼
  Calendar display  ← zero custom drawing loops
```

The `MCN_GETDAYSTATE` notification fires whenever the calendar needs to know which days to embolden. We return one `MONTHDAYSTATE` per visible month. The OS does all the rendering. Our job is just the bitmask.

### Key Win32 techniques used

**`MCN_GETDAYSTATE` + `MCS_DAYSTATE`**
The MonthCal control has a hidden superpower: send it `MCS_DAYSTATE` at creation time and it will ask you — via `WM_NOTIFY` — which days should be bold. Without this flag the notification is silently suppressed. With it, you get native bold rendering for free.

**`SetWindowSubclass` (comctl32 v6)**
Used for the inline add-event EDIT control. The modern replacement for the legacy `SetWindowLongPtr` + `CallWindowProc` subclassing pattern. Survives nested subclasses and avoids ANSI/Unicode mismatches. Requires a Common Controls v6 manifest (included via `#pragma comment(linker,...)`).

**`RegisterHotKey` with conflict safety**
`RegisterHotKey` fails silently if another process already owns the combo. We check the return value, set a `g_hotkeyLive` flag, and surface a tray balloon notification if there is a conflict. The app keeps running. `MOD_NOREPEAT` prevents `WM_HOTKEY` flooding when the user holds the keys.

**`WS_EX_LAYERED` + `TrackMouseEvent`**
`WS_EX_LAYERED` must be set at `CreateWindowEx` time. `TrackMouseEvent` with `TME_LEAVE` delivers `WM_MOUSELEAVE` when the cursor exits the window. A 30 ms timer then decrements alpha in steps of 14, producing a ~400 ms smooth fade. `WM_MOUSEMOVE` cancels the timer and restores full opacity instantly.

**`TaskbarCreated` broadcast message**
When Explorer crashes and restarts, it broadcasts this registered message to all windows. We listen for it in `WindowProc` and re-add the tray icon and re-register the hotkey. Without this, the tray icon disappears after a shell restart and never returns.

**Owner-drawn `LISTBOX`**
`LBS_OWNERDRAWFIXED | LBS_HASSTRINGS` gives us per-item colour control without losing the built-in string storage. `WM_DRAWITEM` fires for each visible item. We set `itemData` to 1 for recurring events so the draw handler can colour them blue — no separate data structure needed.

**GDI font lifecycle**
`HFONT` objects must be deleted after every resize. The pattern: create new fonts → apply to controls → delete old fonts. Never delete a font that is currently selected into a DC.

---

## Changelog

### v1.3.0 — Full feature release
- **Add-Event inline** — subclassed EDIT control overlays the listbox; Enter saves, Escape cancels; uses `SetWindowSubclass` (safe, modern)
- **Recurring events** — `****-MM-DD` yearly, `****-**-DD` monthly; `IsEventMatch()` handles all patterns in one function
- **Hotkey submenu** — 3 presets + Disabled, check-marked, in the tray right-click menu; `MOD_NOREPEAT` prevents flood; tray balloon on conflict
- **MCM_SETDAYSTATE** bold highlighting — `MCS_DAYSTATE` flag + `MCN_GETDAYSTATE` handler; days with events appear bold
- **Week-number column** — `MCS_WEEKNUMBERS` flag added (one line, zero code)
- **Multi-format copy** — ISO 8601, US, and long-form via `NM_RCLICK` context menu
- **UTC clock** — second `STATIC` label, `GetSystemTime()` (always UTC, no offset math)
- **Smooth idle transparency** — `WS_EX_LAYERED`, `TrackMouseEvent`, `TID_FADE` timer with step fade
- **Owner-drawn listbox** — recurring events in accent blue; empty-state hint in muted grey
- **F5 hot-reload** — reload events without restarting
- **Silent tray boot** — `ShowWindow` removed from `WinMain`; no aggressive popup on startup
- **Settings persistence** — font size, hotkey, opacity all saved to `HKCU\Software\PerfectCalendar`

### v1.2.0
- Agenda view (flat-file events listbox)
- UTC label, global hotkey, click-to-copy date
- Dark listbox styling

### v1.1.0
- GDI font leak fix, `DestroyIcon` cleanup
- Named layout constants
- Tray icon retry timer

### v1.0.0
- Initial release: dark MonthCal, system tray, startup toggle, mouse-wheel zoom, HiDPI, drag header

---

## FAQ

**Will it slow down my PC?**  
No. When hidden, it is a suspended message loop consuming no CPU. Idle RAM is around 2 MB.

**Where are my events stored?**  
`%APPDATA%\PerfectCalendar\events.dat` — a plain text file you can edit in Notepad, back up, sync via any file sync tool, or put in Git.

**Does it sync with Google Calendar / Outlook?**  
No, intentionally. Sync engines require background processes, authentication, and network access. The flat-file approach is offline-first and always fast.

**Can I move the `.exe` anywhere?**  
Yes. It is fully portable. Settings go to the registry under `HKCU\Software\PerfectCalendar`; events go to `%APPDATA%`. No files are written next to the executable.

**The hotkey doesn't work.**  
Another app owns that key combination. Open the tray menu → Set Hotkey and pick a different preset.

---

## License

MIT — do whatever you want with it.

---

<p align="center">
  <sub>Built with the Win32 API, a C compiler, and a philosophical objection to 80 MB calendar apps.</sub>
</p>
