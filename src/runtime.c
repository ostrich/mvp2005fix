#include <windows.h>
#include <stdlib.h>

typedef void *(WINAPI *Direct3DCreate8Fn)(UINT sdk_version);
typedef HRESULT (WINAPI *CreateDeviceFn)(void *self, UINT adapter, DWORD device_type, HWND focus_window,
    DWORD behavior_flags, void *presentation_parameters, void **returned_device);
typedef HRESULT (WINAPI *SetVertexShaderConstantFn)(void *self, DWORD register_address,
    const void *constant_data, DWORD constant_count);
typedef HRESULT (WINAPI *SetVertexShaderFn)(void *self, DWORD handle);
typedef HRESULT (WINAPI *SetStreamSourceFn)(void *self, UINT stream_number, void *stream_data,
    UINT stride);

static BYTE saved_ex[5];
static BYTE saved_a[5];
static BYTE saved_d3d_create8[5];
static void *target_ex;
static void *target_a;
static void *target_d3d_create8;
static FARPROC real_Direct3DCreate8;
static void **real_d3d8_vtbl;
static void **real_device_vtbl;
static CreateDeviceFn real_CreateDevice;
static SetVertexShaderConstantFn real_SetVertexShaderConstant;
static SetVertexShaderFn real_SetVertexShader;
static SetStreamSourceFn real_SetStreamSource;
static void *d3d8_vtbl_copy[32];
static DWORD current_vertex_shader;
static DWORD current_stream_stride;
static float aspect_scale = 0.75f;
static int aspect_enabled = 1;
static int save_fix_enabled = 1;
static int debug_logging;
static LONG log_count;
static LONG iat_patch_log_count;
static LONG export_patch_log_count;
static volatile LONG d3d_create8_seen;
static volatile LONG d3d_device_seen;
static HANDLE worker_thread;

extern IMAGE_DOS_HEADER __ImageBase;

static float absf_local(float value)
{
    return value < 0.0f ? -value : value;
}

static int is_probably_2d_ortho_matrix(const float *f)
{
    if (!f) return 0;
    if (absf_local(f[0]) < 0.02f && absf_local(f[5]) < 0.02f &&
        (absf_local(f[3] + 1.0f) < 0.001f || absf_local(f[7] + 1.0f) < 0.001f)) {
        return 1;
    }
    return 0;
}

static int append_path(char *path, DWORD path_size, const char *leaf)
{
    DWORD len = lstrlenA(path);
    DWORD leaf_len = lstrlenA(leaf);

    if (len && path[len - 1] != '\\' && path[len - 1] != '/') {
        if (len + 1 >= path_size) return 0;
        path[len++] = '\\';
        path[len] = 0;
    }
    if (len + leaf_len >= path_size) return 0;
    memcpy(path + len, leaf, leaf_len + 1);
    return 1;
}

static int get_module_dir(char *path, DWORD path_size)
{
    char *slash;

    if (!GetModuleFileNameA((HMODULE)&__ImageBase, path, path_size)) return 0;
    path[path_size - 1] = 0;
    slash = path + lstrlenA(path);
    while (slash > path && slash[-1] != '\\' && slash[-1] != '/') slash--;
    *slash = 0;
    return 1;
}

static int get_ini_path(char *path, DWORD path_size)
{
    return get_module_dir(path, path_size) && append_path(path, path_size, "mvp2005fix.ini");
}

static int get_log_path(char *path, DWORD path_size)
{
    return get_module_dir(path, path_size) && append_path(path, path_size, "mvp2005fix.log");
}

static void log_line(const char *text)
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD written;

    if (!debug_logging) return;
    if (InterlockedIncrement(&log_count) > 300) return;
    if (!get_log_path(path, sizeof(path))) return;
    file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, text, lstrlenA(text), &written, NULL);
    CloseHandle(file);
}

static void load_config(void)
{
    char ini[MAX_PATH];
    char value[64];
    DWORD len;
    float parsed;

    if (!get_ini_path(ini, sizeof(ini))) return;
    debug_logging = GetPrivateProfileIntA("Debug", "Logging", 0, ini) != 0;
    len = GetPrivateProfileStringA("Aspect", "ScaleX", "0.75", value, sizeof(value), ini);
    if (len > 0) {
        parsed = (float)strtod(value, NULL);
        if (parsed > 0.1f && parsed < 2.0f) aspect_scale = parsed;
    }
    aspect_enabled = GetPrivateProfileIntA("Aspect", "Enabled", 1, ini) != 0;
    save_fix_enabled = GetPrivateProfileIntA("SaveFix", "Enabled", 1, ini) != 0;
    {
        char buf[160];
        DWORD scale_bits;
        memcpy(&scale_bits, &aspect_scale, sizeof(scale_bits));
        wsprintfA(buf, "config aspect=%d scale=%08lx save=%d\n",
            aspect_enabled, scale_bits, save_fix_enabled);
        log_line(buf);
    }
}

static BOOL WINAPI fake_GetDiskFreeSpaceExA(
    LPCSTR root,
    PULARGE_INTEGER free_to_caller,
    PULARGE_INTEGER total_bytes,
    PULARGE_INTEGER total_free)
{
    ULONGLONG one_gb = 1024ULL * 1024ULL * 1024ULL;
    ULONGLONG half_gb = 512ULL * 1024ULL * 1024ULL;
    (void)root;
    if (free_to_caller) free_to_caller->QuadPart = half_gb;
    if (total_bytes) total_bytes->QuadPart = one_gb;
    if (total_free) total_free->QuadPart = half_gb;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL WINAPI fake_GetDiskFreeSpaceA(
    LPCSTR root,
    LPDWORD sectors_per_cluster,
    LPDWORD bytes_per_sector,
    LPDWORD free_clusters,
    LPDWORD total_clusters)
{
    (void)root;
    if (sectors_per_cluster) *sectors_per_cluster = 8;
    if (bytes_per_sector) *bytes_per_sector = 512;
    if (free_clusters) *free_clusters = 131072;
    if (total_clusters) *total_clusters = 262144;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static int hook_function(void *target, void *replacement, BYTE saved[5])
{
    DWORD old_protect;
    BYTE patch[5];
    DWORD existing_target;
    if (!target || !replacement) return 0;
    if (*(BYTE *)target == 0xE9) {
        existing_target = (DWORD)((BYTE *)target + 5 + *(LONG *)((BYTE *)target + 1));
        return existing_target == (DWORD)replacement ? 1 : -1;
    }

    memcpy(saved, target, 5);
    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = (DWORD)((BYTE *)replacement - ((BYTE *)target + 5));

    if (VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy(target, patch, 5);
        FlushInstructionCache(GetCurrentProcess(), target, 5);
        VirtualProtect(target, 5, old_protect, &old_protect);
        return 2;
    }
    return 0;
}

static void unhook_function(void *target, BYTE saved[5])
{
    DWORD old_protect;
    if (!target) return;
    if (VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy(target, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), target, 5);
        VirtualProtect(target, 5, old_protect, &old_protect);
    }
}

static void install_save_fix(void)
{
    HMODULE kernel32;
    if (!save_fix_enabled) return;
    kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32) return;
    target_ex = (void *)GetProcAddress(kernel32, "GetDiskFreeSpaceExA");
    target_a = (void *)GetProcAddress(kernel32, "GetDiskFreeSpaceA");
    hook_function(target_ex, fake_GetDiskFreeSpaceExA, saved_ex);
    hook_function(target_a, fake_GetDiskFreeSpaceA, saved_a);
    log_line("save fix hooks installed\n");
}

static int is_patchable_resolution_pair(const BYTE *p)
{
    DWORD w = p[0] | (p[1] << 8);
    DWORD h = p[4] | (p[5] << 8);

    if (p[2] != 0 || p[3] != 0) return 0;
    if (w == 800 && h == 600) return 1;
    return 0;
}

static void patch_resolution_in_memory(void)
{
    char ini[MAX_PATH];
    DWORD width;
    DWORD height;
    BYTE *base;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    DWORD image_size;
    BYTE replacement[6];
    DWORD patched = 0;
    DWORD i;

    if (!get_ini_path(ini, sizeof(ini))) return;
    width = GetPrivateProfileIntA("Resolution", "Width", 0, ini);
    height = GetPrivateProfileIntA("Resolution", "Height", 0, ini);
    if (!width || !height || width > 65535 || height > 65535) return;

    base = (BYTE *)GetModuleHandleA(NULL);
    dos = (IMAGE_DOS_HEADER *)base;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    image_size = nt->OptionalHeader.SizeOfImage;

    replacement[0] = (BYTE)(width & 0xff);
    replacement[1] = (BYTE)((width >> 8) & 0xff);
    replacement[2] = 0;
    replacement[3] = 0;
    replacement[4] = (BYTE)(height & 0xff);
    replacement[5] = (BYTE)((height >> 8) & 0xff);

    for (i = 0; i + sizeof(replacement) <= image_size; i++) {
        if (is_patchable_resolution_pair(base + i)) {
            DWORD old_protect;
            if (VirtualProtect(base + i, sizeof(replacement), PAGE_EXECUTE_READWRITE, &old_protect)) {
                memcpy(base + i, replacement, sizeof(replacement));
                FlushInstructionCache(GetCurrentProcess(), base + i, sizeof(replacement));
                VirtualProtect(base + i, sizeof(replacement), old_protect, &old_protect);
                patched++;
                log_line("resolution pair patched in memory\n");
                if (patched >= 2) break;
            }
        }
    }
}

static HRESULT WINAPI fake_SetVertexShaderConstant(void *self, DWORD register_address,
    const void *constant_data, DWORD constant_count)
{
    float adjusted_data[256];
    const void *data_to_send = constant_data;

    (void)current_vertex_shader;
    (void)current_stream_stride;

    if (aspect_enabled && constant_data && constant_count <= 64 && constant_count >= 4 &&
        (register_address == 0 || register_address == 12) &&
        !is_probably_2d_ortho_matrix((const float *)constant_data)) {
        static LONG patched_constant_log_count;
        memcpy(adjusted_data, constant_data, constant_count * 4 * sizeof(float));
        adjusted_data[0] *= aspect_scale;
        adjusted_data[1] *= aspect_scale;
        adjusted_data[2] *= aspect_scale;
        adjusted_data[3] *= aspect_scale;
        data_to_send = adjusted_data;
        if (InterlockedIncrement(&patched_constant_log_count) <= 20) {
            char buf[160];
            wsprintfA(buf, "SetVSC patched shader=%08lx stride=%lu reg=%lu count=%lu\n",
                current_vertex_shader, current_stream_stride, register_address, constant_count);
            log_line(buf);
        }
    }

    return real_SetVertexShaderConstant ?
        real_SetVertexShaderConstant(self, register_address, data_to_send, constant_count) : E_FAIL;
}

static HRESULT WINAPI fake_SetVertexShader(void *self, DWORD handle)
{
    current_vertex_shader = handle;
    return real_SetVertexShader ? real_SetVertexShader(self, handle) : E_FAIL;
}

static HRESULT WINAPI fake_SetStreamSource(void *self, UINT stream_number, void *stream_data,
    UINT stride)
{
    if (stream_number == 0) current_stream_stride = stride;
    return real_SetStreamSource ? real_SetStreamSource(self, stream_number, stream_data, stride) : E_FAIL;
}

static void patch_vtable_slot(void **vtbl, int slot, void *replacement, void **original)
{
    DWORD old_protect;

    if (!vtbl || !replacement) return;
    if (original && !*original) *original = vtbl[slot];
    if (VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_EXECUTE_READWRITE, &old_protect)) {
        vtbl[slot] = replacement;
        VirtualProtect(&vtbl[slot], sizeof(void *), old_protect, &old_protect);
    }
}

static HRESULT WINAPI fake_CreateDevice(void *self, UINT adapter, DWORD device_type, HWND focus_window,
    DWORD behavior_flags, void *presentation_parameters, void **returned_device)
{
    HRESULT hr;

    hr = real_CreateDevice(self, adapter, device_type, focus_window, behavior_flags,
        presentation_parameters, returned_device);

    if (SUCCEEDED(hr) && returned_device && *returned_device && !real_device_vtbl) {
        log_line("CreateDevice intercepted; patching device vtable\n");
        InterlockedExchange(&d3d_device_seen, 1);
        real_device_vtbl = *(void ***)*returned_device;
        real_SetVertexShader = (SetVertexShaderFn)real_device_vtbl[76];
        real_SetVertexShaderConstant = (SetVertexShaderConstantFn)real_device_vtbl[79];
        real_SetStreamSource = (SetStreamSourceFn)real_device_vtbl[83];
        patch_vtable_slot(real_device_vtbl, 76, fake_SetVertexShader, (void **)&real_SetVertexShader);
        patch_vtable_slot(real_device_vtbl, 79, fake_SetVertexShaderConstant, (void **)&real_SetVertexShaderConstant);
        patch_vtable_slot(real_device_vtbl, 83, fake_SetStreamSource, (void **)&real_SetStreamSource);
    }

    return hr;
}

static void *WINAPI fake_Direct3DCreate8(UINT sdk_version)
{
    Direct3DCreate8Fn fn = NULL;
    void *d3d;

    memcpy(&fn, &real_Direct3DCreate8, sizeof(fn));
    if (!fn) return NULL;
    log_line("Direct3DCreate8 intercepted\n");
    InterlockedExchange(&d3d_create8_seen, 1);
    if (target_d3d_create8) {
        unhook_function(target_d3d_create8, saved_d3d_create8);
    }
    d3d = fn(sdk_version);
    if (target_d3d_create8) {
        hook_function(target_d3d_create8, fake_Direct3DCreate8, saved_d3d_create8);
    }
    if (d3d && !real_d3d8_vtbl) {
        real_d3d8_vtbl = *(void ***)d3d;
        memcpy(d3d8_vtbl_copy, real_d3d8_vtbl, sizeof(d3d8_vtbl_copy));
        real_CreateDevice = (CreateDeviceFn)real_d3d8_vtbl[15];
        d3d8_vtbl_copy[15] = fake_CreateDevice;
        *(void ***)d3d = d3d8_vtbl_copy;
        log_line("Direct3D8 object vtable wrapped\n");
    }
    return d3d;
}

static void patch_loaded_d3d8_export(void)
{
    HMODULE d3d8 = GetModuleHandleA("d3d8.dll");
    FARPROC proc;
    int hook_result;

    if (!d3d8) d3d8 = GetModuleHandleA("D3D8.DLL");
    if (!d3d8) return;

    proc = GetProcAddress(d3d8, "Direct3DCreate8");
    if (!proc) return;
    if (!real_Direct3DCreate8) real_Direct3DCreate8 = proc;
    if (!target_d3d_create8) target_d3d_create8 = (void *)proc;
    if (target_d3d_create8 != (void *)proc) return;

    hook_result = hook_function((void *)proc, fake_Direct3DCreate8, saved_d3d_create8);
    if (hook_result == 2) {
        if (InterlockedIncrement(&export_patch_log_count) <= 5) {
            log_line("Direct3DCreate8 export/code patched\n");
        }
    } else if (hook_result == -1) {
        log_line("Direct3DCreate8 already hooked by another component; skipping code patch\n");
    }
}

static int patch_iat_proc(const char *dll_name, const char *proc_name, void *replacement, FARPROC *original)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imports;
    DWORD old_protect;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    if (!nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress) return 0;

    imports = (IMAGE_IMPORT_DESCRIPTOR *)(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    for (; imports->Name; imports++) {
        const char *name = (const char *)(base + imports->Name);
        IMAGE_THUNK_DATA *orig_thunk;
        IMAGE_THUNK_DATA *first_thunk;

        if (lstrcmpiA(name, dll_name) != 0) continue;

        orig_thunk = (IMAGE_THUNK_DATA *)(base + imports->OriginalFirstThunk);
        first_thunk = (IMAGE_THUNK_DATA *)(base + imports->FirstThunk);
        if (!orig_thunk) orig_thunk = first_thunk;

        for (; orig_thunk->u1.AddressOfData; orig_thunk++, first_thunk++) {
            IMAGE_IMPORT_BY_NAME *import_name;

            if (orig_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            import_name = (IMAGE_IMPORT_BY_NAME *)(base + orig_thunk->u1.AddressOfData);
            if (lstrcmpA((const char *)import_name->Name, proc_name) != 0) continue;

            if ((void *)first_thunk->u1.Function == replacement) return 1;
            if (original && !*original) *original = (FARPROC)first_thunk->u1.Function;
            if (VirtualProtect(&first_thunk->u1.Function, sizeof(void *), PAGE_READWRITE, &old_protect)) {
                first_thunk->u1.Function = (ULONG_PTR)replacement;
                VirtualProtect(&first_thunk->u1.Function, sizeof(void *), old_protect, &old_protect);
                if (InterlockedIncrement(&iat_patch_log_count) <= 10) {
                    log_line("IAT Direct3DCreate8 patched\n");
                }
                return 1;
            }
        }
    }

    return 0;
}

static void patch_d3d8_iat_once(void)
{
    if (!patch_iat_proc("D3D8.DLL", "Direct3DCreate8", fake_Direct3DCreate8, &real_Direct3DCreate8)) {
        if (!patch_iat_proc("d3d8.dll", "Direct3DCreate8", fake_Direct3DCreate8, &real_Direct3DCreate8)) {
            log_line("IAT Direct3DCreate8 patch failed\n");
        }
    }
}

static DWORD WINAPI startup_hook_thread(LPVOID param)
{
    DWORD i;
    (void)param;

    /*
     * SafeDisc can run loader/unpacker code after our early injection. Patch
     * during startup, then stop once D3D8 has been observed.
     */
    for (i = 0; i < 400; i++) {
        if (InterlockedCompareExchange(&d3d_device_seen, 0, 0) ||
            InterlockedCompareExchange(&d3d_create8_seen, 0, 0)) {
            log_line("startup hook thread finished after D3D8 interception\n");
            return 0;
        }
        patch_d3d8_iat_once();
        patch_loaded_d3d8_export();
        Sleep(50);
    }
    log_line("startup hook thread timed out\n");
    return 0;
}

static DWORD WINAPI init_worker_thread(LPVOID param)
{
    (void)param;
    load_config();
    log_line("mvp2005fix loaded\n");
    patch_resolution_in_memory();
    install_save_fix();
    patch_d3d8_iat_once();
    startup_hook_thread(NULL);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        worker_thread = CreateThread(NULL, 0, init_worker_thread, NULL, 0, NULL);
        if (worker_thread) CloseHandle(worker_thread);
    }
    return TRUE;
}
