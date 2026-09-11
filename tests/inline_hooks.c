/* Isolated Windows/Wine regression executable. Includes the runtime so calls
 * exercise its actual trampoline publication and factory callback. */
#include <stdio.h>
#include <assert.h>
#include "../src/runtime.c"

static LONG fail_context, fail_suspend, fail_protect;
static BOOL WINAPI checked_context(HANDLE thread, LPCONTEXT context)
{
    if (InterlockedExchange(&fail_context, 0)) return FALSE;
    return GetThreadContext(thread, context);
}
static DWORD WINAPI checked_suspend(HANDLE thread)
{
    if (InterlockedExchange(&fail_suspend, 0)) return (DWORD)-1;
    return SuspendThread(thread);
}
static BOOL WINAPI checked_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old)
{
    if (InterlockedExchange(&fail_protect, 0)) return FALSE;
    return VirtualProtect(address, size, protection, old);
}
#define GetThreadContext checked_context
#define SuspendThread checked_suspend
#define VirtualProtect checked_protect
#include "../third_party/minhook/src/hook.c"
#undef GetThreadContext
#undef SuspendThread
#undef VirtualProtect

typedef DWORD (WINAPI *UnaryFn)(DWORD);
static UnaryFn arithmetic, arithmetic_original;
static LONG stop_calls, call_count, bad_calls;
static DWORD WINAPI arithmetic_detour(DWORD value)
{
    return arithmetic_original(value) + 1000;
}
static DWORD WINAPI arithmetic_caller(void *unused)
{
    (void)unused;
    while (!InterlockedCompareExchange(&stop_calls, 0, 0)) {
        DWORD value = arithmetic(41);
        if (value != 42 && value != 1042) InterlockedIncrement(&bad_calls);
        InterlockedIncrement(&call_count);
    }
    return 0;
}
static BYTE *executable(const BYTE *bytes, SIZE_T size)
{
    BYTE *memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(memory);
    memcpy(memory, bytes, size);
    assert(FlushInstructionCache(GetCurrentProcess(), memory, size));
    return memory;
}
static void join(HANDLE thread)
{
    assert(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    CloseHandle(thread);
}

static void test_live_install_and_reapply(void)
{
    /* An instruction crosses the five-byte patch boundary. */
    static const BYTE body[] = {0x8b,0x44,0x24,0x04,0x83,0xc0,0x01,0xc2,0x04,0x00};
    BYTE *target = executable(body, sizeof(body));
    HANDLE callers[4];
    int i;
    arithmetic = (UnaryFn)target;
    assert(MH_CreateHook(target, arithmetic_detour, (void **)&arithmetic_original) == MH_OK);
    assert(arithmetic_original(41) == 42);
    for (i = 0; i < 4; ++i) {
        callers[i] = CreateThread(NULL, 0, arithmetic_caller, NULL, 0, NULL);
        assert(callers[i]);
    }
    Sleep(20);
    assert(MH_EnsureHookEnabled(target) == MH_OK);
    for (i = 0; i < 1000; ++i) {
        assert(MH_EnsureHookEnabled(target) == MH_ERROR_ENABLED);
        assert(arithmetic(41) == 1042);
    }
    Sleep(20);
    InterlockedExchange(&stop_calls, 1);
    for (i = 0; i < 4; ++i) join(callers[i]);
    assert(call_count > 0 && bad_calls == 0);

    // Controlled loader restoration: callers have all stopped.
    memcpy(target, body, 5);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    assert(MH_EnsureHookEnabled(target) == MH_OK);
    assert(arithmetic(41) == 1042);
    target[0] = 0xcc;
    assert(MH_EnsureHookEnabled(target) == MH_ERROR_PATCH_CONFLICT);
    assert(target[0] == 0xcc);
    memcpy(target, body, 5);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    assert(MH_EnsureHookEnabled(target) == MH_OK);
    printf("PASS live installation (%ld calls), instruction relocation, idempotence, restoration, conflict\n", call_count);
}

static DWORD WINAPI idle_thread(void *event)
{
    WaitForSingleObject(event, INFINITE);
    return 0;
}
static void test_failures_and_suspended_ip(void)
{
    static const BYTE body[] = {0x8b,0x44,0x24,0x04,0x83,0xc0,0x01,0xc2,0x04,0x00};
    BYTE *target = executable(body, sizeof(body));
    HANDLE event = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, idle_thread, event, CREATE_SUSPENDED, NULL);
    CONTEXT saved = {0}, changed = {0};
    void *original;
    assert(event && thread);
    assert(MH_CreateHook(target, arithmetic_detour, &original) == MH_OK);
    fail_suspend = 1;
    assert(MH_EnsureHookEnabled(target) == MH_ERROR_THREAD_CONTROL);
    assert(memcmp(target, body, sizeof(body)) == 0);
    fail_context = 1;
    assert(MH_EnsureHookEnabled(target) == MH_ERROR_THREAD_CONTROL);
    assert(memcmp(target, body, sizeof(body)) == 0);
    fail_protect = 1;
    assert(MH_EnsureHookEnabled(target) == MH_ERROR_MEMORY_PROTECT);
    assert(memcmp(target, body, sizeof(body)) == 0);

    saved.ContextFlags = CONTEXT_CONTROL;
    assert(GetThreadContext(thread, &saved));
    changed = saved;
    changed.Eip = (DWORD)target + 4;
    assert(SetThreadContext(thread, &changed));
    assert(MH_EnsureHookEnabled(target) == MH_OK);
    assert(GetThreadContext(thread, &changed));
    assert(changed.Eip == (DWORD)original + 4);
    assert(SetThreadContext(thread, &saved));
    // Every failed attempt must preserve the preexisting suspend count.
    assert(ResumeThread(thread) == 1);
    SetEvent(event);
    join(thread);
    CloseHandle(event);
    puts("PASS suspend/context/protection failures, retry, suspended IP relocation and resume counts");
}

static void *factory_vtbl[16];
static void **factory_object = factory_vtbl;
static HANDLE factory_entered, factory_release;
static BYTE *factory_target;
static LONG factory_calls;
static void *WINAPI factory_body(UINT sdk)
{
    InterlockedIncrement(&factory_calls);
    if (sdk == 1) {
        SetEvent(factory_entered);
        assert(WaitForSingleObject(factory_release, 10000) == WAIT_OBJECT_0);
    }
    return &factory_object;
}
static HRESULT WINAPI create_device_stub(void *self, UINT adapter, DWORD type, HWND window,
    DWORD flags, void *parameters, void **result)
{
    (void)self; (void)adapter; (void)type; (void)window;
    (void)flags; (void)parameters; (void)result;
    return E_FAIL;
}
static DWORD WINAPI factory_caller(void *sdk)
{
    Direct3DCreate8Fn fn = (Direct3DCreate8Fn)factory_target;
    assert(fn((UINT)sdk) == &factory_object);
    return 0;
}
static void test_runtime_factory(void)
{
    BYTE body[] = {0xb8,0,0,0,0,0xff,0xe0}; // mov eax, body; jmp eax
    void *destination = factory_body;
    HANDLE first, others[8];
    int i;
    memcpy(body + 1, &destination, 4);
    factory_target = executable(body, sizeof(body));
    factory_vtbl[15] = create_device_stub;
    factory_entered = CreateEventA(NULL, TRUE, FALSE, NULL);
    factory_release = CreateEventA(NULL, TRUE, FALSE, NULL);
    assert(factory_entered && factory_release);
    real_Direct3DCreate8 = (FARPROC)factory_target;
    target_d3d_create8 = factory_target;
    // The IAT callback is usable before an export trampoline exists.
    assert(fake_Direct3DCreate8(2) == &factory_object);
    // Start the concurrent phase with an unpatched factory vtable, too.
    factory_vtbl[15] = create_device_stub;
    real_d3d8_vtbl = NULL;
    real_CreateDevice = NULL;
    d3d8_hook_ready = 0;
    assert(hook_function(factory_target, fake_Direct3DCreate8, &d3d8_trampoline) == 2);
    assert(d3d8_trampoline != NULL);
    first = CreateThread(NULL, 0, factory_caller, (void *)1, 0, NULL);
    assert(first);
    assert(WaitForSingleObject(factory_entered, 10000) == WAIT_OBJECT_0);
    assert(factory_target[0] == 0xe9); // No bypass while original is blocked.
    for (i = 0; i < 8; ++i) {
        others[i] = CreateThread(NULL, 0, factory_caller, (void *)2, 0, NULL);
        assert(others[i]);
    }
    for (i = 0; i < 8; ++i) join(others[i]);
    assert(fake_Direct3DCreate8(2) == &factory_object); // IAT path after enable.
    assert(factory_target[0] == 0xe9);
    assert(real_CreateDevice == create_device_stub);
    assert(factory_vtbl[15] == fake_CreateDevice);
    assert(hook_function(factory_target, fake_Direct3DCreate8, &d3d8_trampoline) == 1);
    SetEvent(factory_release);
    join(first);
    assert(factory_calls == 11);
    CloseHandle(factory_entered);
    CloseHandle(factory_release);
    puts("PASS runtime IAT/export calls, concurrent factories, permanent patch during blocked original");
}

static DWORD WINAPI fixed_detour(DWORD value)
{
    (void)value;
    return 999;
}
static void test_relative_call_and_hotpatch(void)
{
    BYTE body[] = {0xe8,3,0,0,0,0xc2,4,0,0xb8,42,0,0,0,0xc3};
    BYTE short_body[] = {0x90,0x90,0x90,0x90,0x90,0x31,0xc0,0xc2,4,0,0xcc,0x12};
    BYTE *relative = executable(body, sizeof(body));
    BYTE *hot = executable(short_body, sizeof(short_body));
    UnaryFn original;
    assert(MH_CreateHook(relative, fixed_detour, (void **)&original) == MH_OK);
    assert(original(0) == 42);
    assert(MH_EnsureHookEnabled(relative) == MH_OK);
    assert(((UnaryFn)relative)(0) == 999 && original(0) == 42);
    assert(MH_CreateHook(hot + 5, fixed_detour, (void **)&original) == MH_OK);
    assert(MH_EnsureHookEnabled(hot + 5) == MH_OK);
    assert(((UnaryFn)(hot + 5))(0) == 999 && original(0) == 0);
    assert(MH_EnsureHookEnabled(hot + 5) == MH_ERROR_ENABLED);
    // The five-byte function fits without patchAbove. A three-byte return with
    // nonpadding after it forces MinHook to use the five bytes above entry.
    {
        BYTE tiny[] = {0x90,0x90,0x90,0x90,0x90,0xc2,4,0,0x12,0x34};
        BYTE *above = executable(tiny, sizeof(tiny));
        assert(MH_CreateHook(above + 5, fixed_detour, (void **)&original) == MH_OK);
        assert(MH_EnsureHookEnabled(above + 5) == MH_OK);
        assert(above[0] == 0xe9 && above[5] == 0xeb);
        assert(((UnaryFn)(above + 5))(0) == 999);
        assert(MH_EnsureHookEnabled(above + 5) == MH_ERROR_ENABLED);
        memcpy(above, tiny, sizeof(tiny));
        FlushInstructionCache(GetCurrentProcess(), above, sizeof(tiny));
        assert(MH_EnsureHookEnabled(above + 5) == MH_OK);
        assert(((UnaryFn)(above + 5))(0) == 999);
    }
    puts("PASS relative call relocation and hotpatch-area restoration");
}

static void test_system_exports(void)
{
    HMODULE d3d8;
    Direct3DCreate8Fn factory;
    void *d3d;
    typedef ULONG (WINAPI *ReleaseFn)(void *);
    ULARGE_INTEGER available, total, free_bytes;
    DWORD sectors, bytes, free_clusters, clusters;
    install_save_fix();
    assert(GetDiskFreeSpaceExA("C:\\", &available, &total, &free_bytes));
    assert(total.QuadPart == 1024ULL * 1024 * 1024);
    assert(available.QuadPart == 512ULL * 1024 * 1024);
    assert(GetDiskFreeSpaceA("C:\\", &sectors, &bytes, &free_clusters, &clusters));
    assert(sectors == 8 && bytes == 512 && free_clusters == 131072 && clusters == 262144);
    d3d8 = LoadLibraryA("d3d8.dll");
    assert(d3d8);
    factory = (Direct3DCreate8Fn)(void *)GetProcAddress(d3d8, "Direct3DCreate8");
    assert(factory);
    patch_loaded_d3d8_export();
    assert(d3d8_trampoline);
    d3d = factory(220);
    assert(d3d && d3d8_hook_ready);
    assert((*(void ***)d3d)[15] == fake_CreateDevice);
    ((ReleaseFn)(*(void ***)d3d)[2])(d3d);
    puts("PASS system disk exports and real D3D8 factory interception");
}

int main(int argc, char **argv)
{
    assert(MH_Initialize() == MH_OK);
    InitializeCriticalSection(&vtable_lock);
    if (argc == 2 && strcmp(argv[1], "--system") == 0) {
        test_system_exports();
        return 0;
    }
    test_live_install_and_reapply();
    test_failures_and_suspended_ip();
    test_runtime_factory();
    test_relative_call_and_hotpatch();
    puts("All inline hook tests passed.");
    // Production hooks and trampolines deliberately live until process exit.
    return 0;
}
