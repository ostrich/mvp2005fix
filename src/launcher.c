#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>

#define ID_EXE_EDIT 1001
#define ID_BROWSE 1002
#define ID_RES_COMBO 1003
#define ID_SAVE_FIX 1004
#define ID_ASPECT_FIX 1005
#define ID_SAVE 1006
#define ID_LAUNCH 1008
#define ID_STATUS 1009
#define ID_CUSTOM_WIDTH 1010
#define ID_CUSTOM_HEIGHT 1011
#define WM_AUTOLAUNCH (WM_APP + 1)

typedef struct ResolutionOption {
    const char *label;
    DWORD width;
    DWORD height;
} ResolutionOption;

static const ResolutionOption resolutions[] = {
    {"1280 x 720", 1280, 720},
    {"1366 x 768", 1366, 768},
    {"1600 x 900", 1600, 900},
    {"1920 x 1080", 1920, 1080},
    {"1920 x 1200", 1920, 1200},
    {"2560 x 1440", 2560, 1440},
    {"2560 x 1600", 2560, 1600},
    {"2560 x 1080", 2560, 1080},
    {"3440 x 1440", 3440, 1440},
    {"3840 x 2160", 3840, 2160},
    {"5120 x 1440", 5120, 1440},
    {"5120 x 2160", 5120, 2160},
    {"Custom", 0, 0},
};

static HWND exe_edit;
static HWND res_combo;
static HWND save_check;
static HWND aspect_check;
static HWND status_text;
static HWND custom_width_edit;
static HWND custom_size_separator;
static HWND custom_height_edit;

static BOOL CALLBACK set_child_font(HWND child, LPARAM font)
{
    SendMessageA(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

static void set_status(const char *text)
{
    SetWindowTextA(status_text, text);
}

static int custom_resolution_index(void)
{
    return (int)(sizeof(resolutions) / sizeof(resolutions[0])) - 1;
}

static void update_custom_resolution_controls(void)
{
    int idx = (int)SendMessageA(res_combo, CB_GETCURSEL, 0, 0);
    int custom = idx == custom_resolution_index();
    int show = custom ? SW_SHOW : SW_HIDE;

    EnableWindow(custom_width_edit, custom);
    EnableWindow(custom_height_edit, custom);
    ShowWindow(custom_width_edit, show);
    ShowWindow(custom_size_separator, show);
    ShowWindow(custom_height_edit, show);
}

static void set_default_resolution_from_display(void)
{
    DWORD width = (DWORD)GetSystemMetrics(SM_CXSCREEN);
    DWORD height = (DWORD)GetSystemMetrics(SM_CYSCREEN);
    int custom_idx = custom_resolution_index();
    int selected_idx = 9;
    char text[32];
    int i;

    if (width < 640 || height < 480) {
        selected_idx = 9;
    } else {
        selected_idx = custom_idx;
        for (i = 0; i < custom_idx; i++) {
            if (resolutions[i].width == width && resolutions[i].height == height) {
                selected_idx = i;
                break;
            }
        }
    }

    SendMessageA(res_combo, CB_SETCURSEL, selected_idx, 0);
    if (selected_idx == custom_idx) {
        wsprintfA(text, "%lu", (unsigned long)width);
        SetWindowTextA(custom_width_edit, text);
        wsprintfA(text, "%lu", (unsigned long)height);
        SetWindowTextA(custom_height_edit, text);
    } else {
        SetWindowTextA(custom_width_edit, "");
        SetWindowTextA(custom_height_edit, "");
    }
    update_custom_resolution_controls();
}

static void get_app_dir(char *app_dir, DWORD app_dir_size);

static int file_exists(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void path_dirname(const char *path, char *out, DWORD out_size)
{
    const char *slash = path + lstrlenA(path);
    DWORD len;
    while (slash > path && slash[-1] != '\\' && slash[-1] != '/') slash--;
    len = (DWORD)(slash - path);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, path, len);
    out[len] = 0;
}

static void join_path(char *out, DWORD out_size, const char *dir, const char *name)
{
    DWORD dir_len = lstrlenA(dir);
    DWORD name_len = lstrlenA(name);
    DWORD pos = 0;

    if (!out_size) return;
    out[0] = 0;
    if (dir_len >= out_size) return;
    memcpy(out, dir, dir_len);
    pos = dir_len;
    if (pos && out[pos - 1] != '\\' && out[pos - 1] != '/') {
        if (pos + 1 >= out_size) {
            out[0] = 0;
            return;
        }
        out[pos++] = '\\';
    }
    if (pos + name_len >= out_size) {
        out[0] = 0;
        return;
    }
    memcpy(out + pos, name, name_len);
    out[pos + name_len] = 0;
}

static void get_ini_path(char *path, DWORD path_size)
{
    char app_dir[MAX_PATH];
    get_app_dir(app_dir, sizeof(app_dir));
    join_path(path, path_size, app_dir, "mvp2005fix.ini");
}

static void save_launcher_settings(const char *exe_path)
{
    char ini[MAX_PATH];
    get_ini_path(ini, sizeof(ini));
    WritePrivateProfileStringA("Launcher", "ExePath", exe_path, ini);
}

static int load_launcher_settings(char *exe_path, DWORD exe_size)
{
    char ini[MAX_PATH];
    get_ini_path(ini, sizeof(ini));
    GetPrivateProfileStringA("Launcher", "ExePath", "", exe_path, exe_size, ini);
    return exe_path[0] && file_exists(exe_path);
}

static int find_default_game_exe(char *exe_path, DWORD exe_size)
{
    char app_dir[MAX_PATH];
    get_app_dir(app_dir, sizeof(app_dir));
    join_path(exe_path, exe_size, app_dir, "mvp2005.exe");
    return exe_path[0] && file_exists(exe_path);
}

static void get_app_dir(char *app_dir, DWORD app_dir_size)
{
    char app_path[MAX_PATH];
    GetModuleFileNameA(NULL, app_path, sizeof(app_path));
    path_dirname(app_path, app_dir, app_dir_size);
}

static void write_config(DWORD width, DWORD height, int aspect_enabled, int save_enabled)
{
    char ini[MAX_PATH];
    char value[64];
    float scale = 1.0f;

    get_ini_path(ini, sizeof(ini));
    snprintf(value, sizeof(value), "%lu", width);
    WritePrivateProfileStringA("Resolution", "Width", value, ini);
    snprintf(value, sizeof(value), "%lu", height);
    WritePrivateProfileStringA("Resolution", "Height", value, ini);
    if (aspect_enabled && width && height) {
        scale = ((float)height * 4.0f) / ((float)width * 3.0f);
    }
    snprintf(value, sizeof(value), "%.8g", scale);
    WritePrivateProfileStringA("Aspect", "Enabled", aspect_enabled ? "1" : "0", ini);
    WritePrivateProfileStringA("Aspect", "ScaleX", value, ini);
    WritePrivateProfileStringA("SaveFix", "Enabled", save_enabled ? "1" : "0", ini);
}

static void do_browse(HWND hwnd)
{
    OPENFILENAMEA ofn;
    char path[MAX_PATH] = "mvp2005.exe";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "MVP Baseball executable (mvp2005.exe)\0mvp2005.exe\0"
        "Executables (*.exe)\0*.exe\0"
        "All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(exe_edit, path);
        save_launcher_settings(path);
    }
}

static int get_selected(HWND hwnd, char *exe_path, DWORD exe_size, char *game_dir, DWORD dir_size,
    const ResolutionOption **res)
{
    int idx;
    GetWindowTextA(exe_edit, exe_path, exe_size);
    if (!file_exists(exe_path)) {
        MessageBoxA(hwnd, "Select mvp2005.exe first.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }
    path_dirname(exe_path, game_dir, dir_size);
    save_launcher_settings(exe_path);
    idx = (int)SendMessageA(res_combo, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= (int)(sizeof(resolutions) / sizeof(resolutions[0]))) idx = 4;
    *res = &resolutions[idx];
    return 1;
}

static int get_selected_resolution(HWND hwnd, const ResolutionOption *res, DWORD *width, DWORD *height)
{
    char width_text[32];
    char height_text[32];
    char *end;
    unsigned long parsed_width;
    unsigned long parsed_height;

    if (res->width && res->height) {
        *width = res->width;
        *height = res->height;
        return 1;
    }

    GetWindowTextA(custom_width_edit, width_text, sizeof(width_text));
    GetWindowTextA(custom_height_edit, height_text, sizeof(height_text));
    parsed_width = strtoul(width_text, &end, 10);
    if (!width_text[0] || *end) {
        MessageBoxA(hwnd, "Enter a numeric custom width.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }
    parsed_height = strtoul(height_text, &end, 10);
    if (!height_text[0] || *end) {
        MessageBoxA(hwnd, "Enter a numeric custom height.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }
    if (parsed_width < 640 || parsed_width > 65535 || parsed_height < 480 || parsed_height > 65535) {
        MessageBoxA(hwnd, "Custom resolution must be between 640x480 and 65535x65535.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }

    *width = (DWORD)parsed_width;
    *height = (DWORD)parsed_height;
    return 1;
}

static int write_selected_config(HWND hwnd)
{
    char exe_path[MAX_PATH];
    char game_dir[MAX_PATH];
    const ResolutionOption *res;
    DWORD width;
    DWORD height;
    int save_enabled;
    int aspect_enabled;

    if (!get_selected(hwnd, exe_path, sizeof(exe_path), game_dir, sizeof(game_dir), &res)) return 0;
    if (!get_selected_resolution(hwnd, res, &width, &height)) return 0;

    save_enabled = Button_GetCheck(save_check) == BST_CHECKED;
    aspect_enabled = Button_GetCheck(aspect_check) == BST_CHECKED;
    (void)game_dir;
    write_config(width, height, aspect_enabled, save_enabled);
    return 1;
}

static int config_is_complete(void)
{
    char ini[MAX_PATH];
    get_ini_path(ini, sizeof(ini));
    return GetPrivateProfileIntA("Resolution", "Width", 0, ini) >= 640 &&
        GetPrivateProfileIntA("Resolution", "Height", 0, ini) >= 480;
}

static void do_save(HWND hwnd)
{
    if (write_selected_config(hwnd)) {
        set_status("Saved config. No game files were modified.");
    }
}

static int inject_dll(HANDLE process, const char *dll_path)
{
    SIZE_T len = lstrlenA(dll_path) + 1;
    LPVOID remote_path;
    HANDLE thread;
    DWORD wait_result;
    HMODULE kernel32;
    FARPROC load_library_proc;
    LPTHREAD_START_ROUTINE load_library;

    remote_path = VirtualAllocEx(process, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return 0;
    if (!WriteProcessMemory(process, remote_path, dll_path, len, NULL)) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return 0;
    }

    kernel32 = GetModuleHandleA("kernel32.dll");
    load_library_proc = GetProcAddress(kernel32, "LoadLibraryA");
    memcpy(&load_library, &load_library_proc, sizeof(load_library));
    thread = CreateRemoteThread(process, NULL, 0, load_library, remote_path, 0, NULL);
    if (!thread) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return 0;
    }

    wait_result = WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return wait_result == WAIT_OBJECT_0;
}

static int launch_target(HWND hwnd, const char *exe_path, int write_ui_config)
{
    char game_dir[MAX_PATH];
    char app_dir[MAX_PATH];
    char dll_path[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    path_dirname(exe_path, game_dir, sizeof(game_dir));
    if (write_ui_config) {
        if (!write_selected_config(hwnd)) return 0;
    } else if (config_is_complete()) {
        save_launcher_settings(exe_path);
    } else {
        MessageBoxA(hwnd, "mvp2005fix.ini is missing runtime settings. Run the launcher interactively once to choose settings.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }

    get_app_dir(app_dir, sizeof(app_dir));
    join_path(dll_path, sizeof(dll_path), app_dir, "mvp2005fix.dll");
    if (!file_exists(dll_path)) {
        MessageBoxA(hwnd, "mvp2005fix.dll was not found next to mvp2005fix.exe.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (CreateProcessA(exe_path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, game_dir, &si, &pi)) {
        if (!inject_dll(pi.hProcess, dll_path)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            MessageBoxA(hwnd, "Could not inject mvp2005fix.dll. The game was not started.", "mvp2005fix", MB_ICONERROR);
            return 0;
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (hwnd) set_status("Launched mvp2005.exe with runtime fixes injected.");
        return 1;
    } else {
        MessageBoxA(hwnd, "Launch failed.", "mvp2005fix", MB_ICONERROR);
        return 0;
    }
}

static void do_launch(HWND hwnd)
{
    char exe_path[MAX_PATH];
    char game_dir[MAX_PATH];
    const ResolutionOption *res;

    if (!get_selected(hwnd, exe_path, sizeof(exe_path), game_dir, sizeof(game_dir), &res)) return;
    (void)res;
    if (launch_target(hwnd, exe_path, 1) && hwnd) {
        DestroyWindow(hwnd);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    int i;
    HFONT font;

    switch (msg) {
    case WM_CREATE:
        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        CreateWindowA("STATIC", "mvp2005.exe", WS_CHILD | WS_VISIBLE, 12, 16, 78, 20, hwnd, NULL, NULL, NULL);
        exe_edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            96, 12, 258, 21, hwnd, (HMENU)ID_EXE_EDIT, NULL, NULL);
        CreateWindowA("BUTTON", "Browse", WS_CHILD | WS_VISIBLE, 364, 12, 72, 24, hwnd, (HMENU)ID_BROWSE, NULL, NULL);

        CreateWindowA("STATIC", "Resolution", WS_CHILD | WS_VISIBLE, 12, 44, 78, 20, hwnd, NULL, NULL, NULL);
        res_combo = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            96, 40, 190, 250, hwnd, (HMENU)ID_RES_COMBO, NULL, NULL);
        for (i = 0; i < (int)(sizeof(resolutions) / sizeof(resolutions[0])); i++) {
            SendMessageA(res_combo, CB_ADDSTRING, 0, (LPARAM)resolutions[i].label);
        }
        custom_width_edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            296, 40, 60, 21, hwnd, (HMENU)ID_CUSTOM_WIDTH, NULL, NULL);
        custom_size_separator = CreateWindowA("STATIC", "x", WS_CHILD | WS_VISIBLE,
            362, 44, 12, 20, hwnd, NULL, NULL, NULL);
        custom_height_edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            376, 40, 60, 21, hwnd, (HMENU)ID_CUSTOM_HEIGHT, NULL, NULL);
        set_default_resolution_from_display();

        aspect_check = CreateWindowA("BUTTON", "True widescreen 3D aspect fix", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            96, 72, 260, 22, hwnd, (HMENU)ID_ASPECT_FIX, NULL, NULL);
        Button_SetCheck(aspect_check, BST_CHECKED);
        save_check = CreateWindowA("BUTTON", "Save-game disk-space fixer", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            96, 98, 260, 22, hwnd, (HMENU)ID_SAVE_FIX, NULL, NULL);
        Button_SetCheck(save_check, BST_CHECKED);

        CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE, 96, 130, 82, 26, hwnd, (HMENU)ID_SAVE, NULL, NULL);
        CreateWindowA("BUTTON", "Launch", WS_CHILD | WS_VISIBLE, 186, 130, 82, 26, hwnd, (HMENU)ID_LAUNCH, NULL, NULL);
        status_text = CreateWindowA("STATIC", "Select mvp2005.exe, choose settings, then Launch.", WS_CHILD | WS_VISIBLE,
            12, 168, 420, 20, hwnd, (HMENU)ID_STATUS, NULL, NULL);

        EnumChildWindows(hwnd, set_child_font, (LPARAM)font);
        {
            char remembered[MAX_PATH];
            if (load_launcher_settings(remembered, sizeof(remembered))) {
                SetWindowTextA(exe_edit, remembered);
                if (config_is_complete()) {
                    set_status("Launching with existing config...");
                    PostMessageA(hwnd, WM_AUTOLAUNCH, 0, 0);
                } else {
                    set_status("Loaded previous mvp2005.exe path. Choose settings, then Launch.");
                }
            } else if (find_default_game_exe(remembered, sizeof(remembered))) {
                SetWindowTextA(exe_edit, remembered);
                set_status("Found mvp2005.exe next to the launcher. Choose settings, then Launch.");
            }
        }
        return 0;
    case WM_AUTOLAUNCH:
        do_launch(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_RES_COMBO:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                update_custom_resolution_controls();
            }
            return 0;
        case ID_BROWSE:
            do_browse(hwnd);
            return 0;
        case ID_SAVE:
            do_save(hwnd);
            return 0;
        case ID_LAUNCH:
            do_launch(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    RECT window_rect;
    DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    char remembered[MAX_PATH];
    char remembered_dir[MAX_PATH];
    (void)prev;
    (void)cmd;

    if (load_launcher_settings(remembered, sizeof(remembered))) {
        path_dirname(remembered, remembered_dir, sizeof(remembered_dir));
        if (config_is_complete()) {
            return launch_target(NULL, remembered, 0) ? 0 : 1;
        }
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = inst;
    wc.lpszClassName = "mvp2005fixWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    window_rect.left = 0;
    window_rect.top = 0;
    window_rect.right = 448;
    window_rect.bottom = 200;
    AdjustWindowRect(&window_rect, window_style, FALSE);

    hwnd = CreateWindowA("mvp2005fixWindow", "mvp2005fix", window_style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
