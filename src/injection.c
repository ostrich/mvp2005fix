#include "injection.h"
#include "startup.h"

#if !defined(__i386__) && !defined(_M_IX86)
#error "mvp2005fix requires 32-bit x86"
#endif

InjectionResult inject_runtime(HANDLE process, DWORD pid, const char *dll_path, DWORD timeout_ms)
{
    char name[STARTUP_EVENT_NAME_SIZE];
    HANDLE ready = NULL, failed = NULL, thread = NULL;
    HANDLE waits[3];
    LPVOID remote_path = NULL;
    SIZE_T len = lstrlenA(dll_path) + 1;
    HMODULE kernel32;
    FARPROC proc;
    LPTHREAD_START_ROUTINE load_library;
    DWORD wait_result, exit_code, started = GetTickCount(), elapsed;
    BOOL thread_finished = FALSE;
    InjectionResult result = INJECTION_EVENT_FAILED;

    startup_event_name(name, pid, TRUE);
    ready = CreateEventA(NULL, TRUE, FALSE, name);
    if (!ready || GetLastError() == ERROR_ALREADY_EXISTS) goto done;
    startup_event_name(name, pid, FALSE);
    failed = CreateEventA(NULL, TRUE, FALSE, name);
    if (!failed || GetLastError() == ERROR_ALREADY_EXISTS) goto done;

    result = INJECTION_LOAD_FAILED;
    kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32) goto done;
    proc = GetProcAddress(kernel32, "LoadLibraryA");
    if (!proc) goto done;
    memcpy(&load_library, &proc, sizeof(load_library));
    remote_path = VirtualAllocEx(process, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) goto done;
    if (!WriteProcessMemory(process, remote_path, dll_path, len, NULL)) goto done;
    thread = CreateRemoteThread(process, NULL, 0, load_library, remote_path, 0, NULL);
    if (!thread) goto done;

    elapsed = GetTickCount() - started;
    wait_result = WaitForSingleObject(thread, elapsed < timeout_ms ? timeout_ms - elapsed : 0);
    if (wait_result != WAIT_OBJECT_0) {
        result = wait_result == WAIT_TIMEOUT ? INJECTION_LOAD_TIMEOUT : INJECTION_WAIT_FAILED;
        goto done;
    }
    thread_finished = TRUE;
    if (!GetExitCodeThread(thread, &exit_code) || exit_code == 0) goto done;

    // Process death/failure wins if several handles are signaled together.
    waits[0] = process;
    waits[1] = failed;
    waits[2] = ready;
    elapsed = GetTickCount() - started;
    wait_result = WaitForMultipleObjects(3, waits, FALSE,
        elapsed < timeout_ms ? timeout_ms - elapsed : 0);
    switch (wait_result) {
    case WAIT_OBJECT_0: result = INJECTION_PROCESS_EXITED; break;
    case WAIT_OBJECT_0 + 1: result = INJECTION_INIT_FAILED; break;
    case WAIT_OBJECT_0 + 2: result = INJECTION_OK; break;
    case WAIT_TIMEOUT: result = INJECTION_INIT_TIMEOUT; break;
    default: result = INJECTION_WAIT_FAILED; break;
    }

done:
    // On timeout or wait failure the remote loader may still reference its
    // argument. Let the caller's process termination reclaim that allocation.
    if (remote_path && (!thread || thread_finished))
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    if (thread) CloseHandle(thread);
    if (failed) CloseHandle(failed);
    if (ready) CloseHandle(ready);
    return result;
}

const char *injection_error_message(InjectionResult result)
{
    switch (result) {
    case INJECTION_EVENT_FAILED: return "Could not create the runtime readiness events.";
    case INJECTION_LOAD_FAILED: return "Could not load mvp2005fix.dll into the game.";
    case INJECTION_LOAD_TIMEOUT: return "Timed out loading mvp2005fix.dll.";
    case INJECTION_INIT_FAILED: return "mvp2005fix.dll could not initialize its runtime hooks.";
    case INJECTION_INIT_TIMEOUT: return "Timed out waiting for runtime initialization. Ensure the launcher and DLL are from the same release.";
    case INJECTION_PROCESS_EXITED: return "The game exited during runtime initialization.";
    default: return "Could not wait for runtime initialization.";
    }
}
