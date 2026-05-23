#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define APP_NAME    "tabsess"
#define WM_TRAYICON (WM_USER + 1)
#define HOTKEY_ID   1
#define ID_TRAY_EXIT   1001
#define ID_TRAY_RELOAD 1002
#define IDI_MAIN    1

static NOTIFYICONDATA g_nid;
static HWND      g_hwnd;
static HINSTANCE g_hInst;
static UINT  g_hotkey_mod, g_hotkey_vk;
static UINT  g_send_mod,   g_send_vk;
static char  g_target_exe[64];
static char  g_ini_path[MAX_PATH];

/* ---- string helpers ---- */

static void trim(char *s) {
    char *e = s + strlen(s) - 1;
    while (e >= s && isspace((unsigned char)*e)) *e-- = '\0';
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

/* ---- hotkey parser ---- */

static UINT parse_vk(const char *s) {
    if (strlen(s) == 1) {
        char c = (char)toupper((unsigned char)s[0]);
        if (c >= 'A' && c <= 'Z') return (UINT)c;
        if (c >= '0' && c <= '9') return (UINT)c;
    }
    if (_stricmp(s,"F1")==0)  return VK_F1;
    if (_stricmp(s,"F2")==0)  return VK_F2;
    if (_stricmp(s,"F3")==0)  return VK_F3;
    if (_stricmp(s,"F4")==0)  return VK_F4;
    if (_stricmp(s,"F5")==0)  return VK_F5;
    if (_stricmp(s,"F6")==0)  return VK_F6;
    if (_stricmp(s,"F7")==0)  return VK_F7;
    if (_stricmp(s,"F8")==0)  return VK_F8;
    if (_stricmp(s,"F9")==0)  return VK_F9;
    if (_stricmp(s,"F10")==0) return VK_F10;
    if (_stricmp(s,"F11")==0) return VK_F11;
    if (_stricmp(s,"F12")==0) return VK_F12;
    if (_stricmp(s,"Tab")==0)    return VK_TAB;
    if (_stricmp(s,"Enter")==0 || _stricmp(s,"Return")==0) return VK_RETURN;
    if (_stricmp(s,"Space")==0)  return VK_SPACE;
    if (_stricmp(s,"Esc")==0 || _stricmp(s,"Escape")==0) return VK_ESCAPE;
    if (_stricmp(s,"Back")==0 || _stricmp(s,"Backspace")==0) return VK_BACK;
    if (_stricmp(s,"Del")==0  || _stricmp(s,"Delete")==0)    return VK_DELETE;
    if (_stricmp(s,"Ins")==0  || _stricmp(s,"Insert")==0)    return VK_INSERT;
    if (_stricmp(s,"Home")==0)   return VK_HOME;
    if (_stricmp(s,"End")==0)    return VK_END;
    if (_stricmp(s,"PgUp")==0 || _stricmp(s,"PageUp")==0)   return VK_PRIOR;
    if (_stricmp(s,"PgDn")==0 || _stricmp(s,"PageDown")==0) return VK_NEXT;
    if (_stricmp(s,"Up")==0)    return VK_UP;
    if (_stricmp(s,"Down")==0)  return VK_DOWN;
    if (_stricmp(s,"Left")==0)  return VK_LEFT;
    if (_stricmp(s,"Right")==0) return VK_RIGHT;
    return 0;
}

/* "Ctrl+Alt+L" -> mod flags + VK code */
static BOOL parse_hotkey(const char *str, UINT *mod, UINT *vk) {
    char buf[64];
    strncpy(buf, str, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    *mod = 0; *vk = 0;

    char *parts[8];
    int   count = 0;
    char *p = buf, *start = buf;

    while (*p && count < 7) {
        if (*p == '+') { *p = '\0'; parts[count++] = start; start = p+1; }
        p++;
    }
    parts[count++] = start;

    for (int i = 0; i < count-1; i++) {
        trim(parts[i]);
        if (_stricmp(parts[i],"Ctrl")==0 || _stricmp(parts[i],"Control")==0) *mod |= MOD_CONTROL;
        else if (_stricmp(parts[i],"Alt")==0)   *mod |= MOD_ALT;
        else if (_stricmp(parts[i],"Shift")==0) *mod |= MOD_SHIFT;
        else if (_stricmp(parts[i],"Win")==0)   *mod |= MOD_WIN;
    }

    trim(parts[count-1]);
    *vk = parse_vk(parts[count-1]);
    return *vk != 0;
}

/* ---- config ---- */

static void get_ini_path(void) {
    GetModuleFileNameA(NULL, g_ini_path, MAX_PATH);
    char *p = strrchr(g_ini_path, '\\');
    if (p) *(p+1) = '\0';
    strncat(g_ini_path, "config.ini", MAX_PATH - strlen(g_ini_path) - 1);
}

static void load_config(void) {
    char val[64];
    GetPrivateProfileStringA("tabsess","Hotkey","Ctrl+Alt+L", val,sizeof(val),g_ini_path);
    if (!parse_hotkey(val, &g_hotkey_mod, &g_hotkey_vk)) {
        g_hotkey_mod = MOD_CONTROL|MOD_ALT; g_hotkey_vk = 'L';
    }
    GetPrivateProfileStringA("tabsess","TabSessionManagerSave","Ctrl+Shift+L", val,sizeof(val),g_ini_path);
    if (!parse_hotkey(val, &g_send_mod, &g_send_vk)) {
        g_send_mod = MOD_CONTROL|MOD_SHIFT; g_send_vk = 'L';
    }
    GetPrivateProfileStringA("tabsess","TargetExe","waterfox.exe", g_target_exe,sizeof(g_target_exe),g_ini_path);
}

/* ---- find target window ---- */

typedef struct { DWORD pid; HWND hwnd; } FindData;

static BOOL CALLBACK enum_wnd_cb(HWND hwnd, LPARAM lp) {
    FindData *d = (FindData*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == d->pid && IsWindowVisible(hwnd)) {
        char title[8];
        if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
            d->hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static HWND find_target_hwnd(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return NULL;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, g_target_exe) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    if (!pid) return NULL;

    FindData d = { pid, NULL };
    EnumWindows(enum_wnd_cb, (LPARAM)&d);
    return d.hwnd;
}

/* ---- foreground ---- */

static void bring_to_front(HWND hwnd) {
    if (!hwnd) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);

    HWND fgwnd = GetForegroundWindow();
    if (fgwnd && fgwnd != hwnd) {
        DWORD fg_tid  = GetWindowThreadProcessId(fgwnd, NULL);
        DWORD tgt_tid = GetWindowThreadProcessId(hwnd, NULL);
        if (fg_tid && fg_tid != tgt_tid) {
            AttachThreadInput(fg_tid, tgt_tid, TRUE);
            SetForegroundWindow(hwnd);
            BringWindowToTop(hwnd);
            AttachThreadInput(fg_tid, tgt_tid, FALSE);
            return;
        }
    }
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
}

/* ---- send input ---- */

static void send_combo(UINT mod, UINT vk) {
    INPUT in[8];
    int n = 0;
    memset(in, 0, sizeof(in));

#define KD(k) do{in[n].type=INPUT_KEYBOARD;in[n].ki.wVk=(k);n++;}while(0)
#define KU(k) do{in[n].type=INPUT_KEYBOARD;in[n].ki.wVk=(k);in[n].ki.dwFlags=KEYEVENTF_KEYUP;n++;}while(0)

    if (mod & MOD_CONTROL) KD(VK_CONTROL);
    if (mod & MOD_SHIFT)   KD(VK_SHIFT);
    if (mod & MOD_ALT)     KD(VK_MENU);
    if (mod & MOD_WIN)     KD(VK_LWIN);
    KD(vk);
    KU(vk);
    if (mod & MOD_WIN)     KU(VK_LWIN);
    if (mod & MOD_ALT)     KU(VK_MENU);
    if (mod & MOD_SHIFT)   KU(VK_SHIFT);
    if (mod & MOD_CONTROL) KU(VK_CONTROL);

#undef KD
#undef KU

    SendInput(n, in, sizeof(INPUT));
}

static void send_enter(void) {
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_RETURN;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_RETURN; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

/* ---- action ---- */

static void do_action(void) {
    HWND target = find_target_hwnd();
    if (!target) return;

    bring_to_front(target);
    Sleep(150);
    send_combo(g_send_mod, g_send_vk);
    Sleep(500);
    send_enter();
}

/* ---- tray ---- */

static void tray_add(void) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = (HICON)LoadImageA(g_hInst, MAKEINTRESOURCEA(IDI_MAIN), IMAGE_ICON,
                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                LR_DEFAULTCOLOR);
    strncpy(g_nid.szTip, APP_NAME, sizeof(g_nid.szTip)-1);
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void tray_remove(void) {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

static void show_menu(void) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING,    ID_TRAY_RELOAD, "Reload config");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING,    ID_TRAY_EXIT,   "Exit");
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
}

/* ---- window proc ---- */

static void reload_config(void) {
    UnregisterHotKey(g_hwnd, HOTKEY_ID);
    load_config();
    if (!RegisterHotKey(g_hwnd, HOTKEY_ID, g_hotkey_mod | MOD_NOREPEAT, g_hotkey_vk))
        MessageBoxA(NULL, "Hotkey registration failed.\nCheck config.ini.", APP_NAME, MB_ICONWARNING);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP) show_menu();
        break;

    case WM_HOTKEY:
        if (wp == HOTKEY_ID) do_action();
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TRAY_EXIT:   DestroyWindow(hwnd); break;
        case ID_TRAY_RELOAD: reload_config();      break;
        }
        break;

    case WM_DESTROY:
        tray_remove();
        UnregisterHotKey(hwnd, HOTKEY_ID);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    return 0;
}

/* ---- entry ---- */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd; (void)nShow;

    HANDLE mutex = CreateMutexA(NULL, TRUE, APP_NAME "_mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }

    g_hInst = hInst;
    get_ini_path();
    load_config();

    HICON hIconBig = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(IDI_MAIN), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                       LR_DEFAULTCOLOR);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInst;
    wc.hIcon         = hIconBig;
    wc.lpszClassName = APP_NAME;
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, APP_NAME, APP_NAME, 0,
                              0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);

    if (!RegisterHotKey(g_hwnd, HOTKEY_ID, g_hotkey_mod | MOD_NOREPEAT, g_hotkey_vk)) {
        MessageBoxA(NULL,
                    "Failed to register hotkey.\n"
                    "Another app may be using it, or config.ini has a bad Hotkey value.",
                    APP_NAME, MB_ICONERROR);
        return 1;
    }

    tray_add();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    CloseHandle(mutex);
    return (int)msg.wParam;
}
