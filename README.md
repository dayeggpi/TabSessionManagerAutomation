# TabSessionManagerAutomation
Goes one step further by capturing another hotkey to further automate the backup of Tab Session Manager sessions

The app is a Windows system-tray tool that triggers a tab-session save in your browser via a global hotkey.

Press the hotkey (defined in config.ini) → tabsess finds the target browser window, brings it to front, sends the configured key combo (define it in Tab Session Manager browser extension for `Export all sessions` and put the same value in config.ini, to save the session.

## How it works

1. Sits in the system tray, registers a global hotkey (defined in config.ini as `Hotkey`)
2. On hotkey press: finds the target browser process (defined in config.ini as `TargetExe`, focuses its window
3. Sends the "save session" key combo to that window (defined in Tab Session Manager browser extention for `Export all sessions` AND in config.ini as `TabSessionManagerSave`)

## Configuration

Edit `config.ini` (same folder as the `.exe`):

```ini
[tabsess]
Hotkey=Ctrl+Alt+D               ; global hotkey to trigger
TabSessionManagerSave=Ctrl+Shift+L  ; keys sent to the browser
TargetExe=waterfox.exe          ; browser process name (case-insensitive)
```

Right-click the tray icon → **Reload Config** to apply changes without restarting.

Supported modifier keys: `Ctrl`, `Alt`, `Shift`, `Win`  
Supported keys: `A`–`Z`, `0`–`9`, `F1`–`F12`, `Tab`, `Enter`, `Space`, `Esc`, `Back`, `Del`, `Ins`, `Home`, `End`, `PgUp`, `PgDn`, arrow keys

## Build

Requires MinGW (`gcc` + `windres`) in PATH:

```bat
build.bat
```

Or with MSVC:

```bat
rc tabsess.rc
cl /O2 tabsess.c tabsess.res /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:WINDOWS
```

Or see in Releases for prebuilt binaries

## Requirements

- Windows
- Browser with a tab session manager extension (e.g. [Tab Session Manager](https://github.com/sienori/Tab-Session-Manager))
