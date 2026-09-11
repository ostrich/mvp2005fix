#include "../src/startup.h"

static volatile LONG initialized;

__declspec(dllexport) DWORD WINAPI startup_fixture_ready(void)
{
    return InterlockedCompareExchange(&initialized, 0, 0);
}

static DWORD WINAPI worker(void *unused)
{
    char mode[32];
    (void)unused;
    GetEnvironmentVariableA("MVP_STARTUP_TEST_MODE", mode, sizeof(mode));
    Sleep(200); // Deliberately finish after the remote LoadLibrary thread.
    if (lstrcmpA(mode, "silent") == 0) return 0;
    if (lstrcmpA(mode, "exit") == 0) ExitProcess(9);
    if (lstrcmpA(mode, "fail") == 0) {
        signal_startup_result(FALSE);
        return 0;
    }
    InterlockedExchange(&initialized, 1);
    signal_startup_result(TRUE);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    char mode[32];
    HANDLE thread;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(instance);
    GetEnvironmentVariableA("MVP_STARTUP_TEST_MODE", mode, sizeof(mode));
    if (lstrcmpA(mode, "reject") == 0) return FALSE;
    // Test-only loader delay exercises a still-running LoadLibrary timeout.
    if (lstrcmpA(mode, "slowload") == 0) Sleep(3000);
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!thread) return FALSE;
    CloseHandle(thread);
    return TRUE;
}
