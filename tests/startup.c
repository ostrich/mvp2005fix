#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "../src/startup.h"

static LPVOID allocation;
static int frees, fail_exit_code, fail_wait;
static LPVOID WINAPI tracked_alloc(HANDLE process, LPVOID address, SIZE_T size, DWORD type, DWORD protection)
{
    allocation = VirtualAllocEx(process, address, size, type, protection);
    return allocation;
}
static BOOL WINAPI tracked_free(HANDLE process, LPVOID address, SIZE_T size, DWORD type)
{
    ++frees;
    return VirtualFreeEx(process, address, size, type);
}
static BOOL WINAPI checked_exit_code(HANDLE thread, LPDWORD code)
{
    if (fail_exit_code) return FALSE;
    return GetExitCodeThread(thread, code);
}
static DWORD WINAPI checked_wait(HANDLE handle, DWORD timeout)
{
    if (fail_wait) return WAIT_FAILED;
    return WaitForSingleObject(handle, timeout);
}
#define VirtualAllocEx tracked_alloc
#define VirtualFreeEx tracked_free
#define GetExitCodeThread checked_exit_code
#define WaitForSingleObject checked_wait
#include "../src/injection.c"
#undef VirtualAllocEx
#undef VirtualFreeEx
#undef GetExitCodeThread
#undef WaitForSingleObject

static char self[MAX_PATH], fixture[MAX_PATH], runtime[MAX_PATH];
static PROCESS_INFORMATION child(const char *mode, BOOL real_runtime)
{
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi;
    char command[MAX_PATH + 40];
    si.cb = sizeof(si);
    assert(SetEnvironmentVariableA("MVP_STARTUP_TEST_MODE", mode));
    snprintf(command, sizeof(command), "\"%s\" %s", self, real_runtime ? "--runtime-child" : "--child");
    assert(CreateProcessA(self, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi));
    allocation = NULL;
    frees = 0;
    return pi;
}

static void finish(PROCESS_INFORMATION *pi, BOOL success)
{
    DWORD code;
    if (success) {
        // MinHook and the injector must preserve the initial suspension.
        assert(ResumeThread(pi->hThread) == 1);
    } else {
        TerminateProcess(pi->hProcess, 1);
    }
    assert(WaitForSingleObject(pi->hProcess, 10000) == WAIT_OBJECT_0);
    assert(GetExitCodeProcess(pi->hProcess, &code));
    if (success) assert(code == 0);
    CloseHandle(pi->hThread);
    CloseHandle(pi->hProcess);
}

static void scenario(const char *mode, const char *dll, DWORD timeout, InjectionResult expected)
{
    PROCESS_INFORMATION pi = child(mode, dll == runtime);
    InjectionResult result = inject_runtime(pi.hProcess, pi.dwProcessId, dll, timeout);
    if (result != expected) fprintf(stderr, "%s: got %d expected %d\n", mode, result, expected);
    assert(result == expected);
    if (result == INJECTION_LOAD_TIMEOUT || result == INJECTION_WAIT_FAILED) {
        MEMORY_BASIC_INFORMATION memory;
        assert(allocation && frees == 0);
        assert(VirtualQueryEx(pi.hProcess, allocation, &memory, sizeof(memory)));
        assert(memory.State == MEM_COMMIT); // Remote loader still owns its argument.
    } else {
        assert(frees == 1);
    }
    finish(&pi, result == INJECTION_OK);
    printf("PASS startup %s\n", mode);
}

int main(int argc, char **argv)
{
    char *slash;
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        HMODULE dll = GetModuleHandleA("startup-fixture.dll");
        typedef DWORD (WINAPI *ReadyFn)(void);
        ReadyFn ready;
        if (!dll) return 2;
        ready = (ReadyFn)(void *)GetProcAddress(dll, "startup_fixture_ready@0");
        return ready && ready() == 1 ? 0 : 3;
    }
    if (argc == 2 && strcmp(argv[1], "--runtime-child") == 0)
        return GetModuleHandleA("mvp2005fix.dll") ? 0 : 4;

    assert(GetModuleFileNameA(NULL, self, sizeof(self)) < sizeof(self));
    strcpy(fixture, self);
    slash = strrchr(fixture, '\\');
    assert(slash);
    strcpy(slash + 1, "startup-fixture.dll");
    strcpy(runtime, self);
    strcpy(strrchr(runtime, '\\') + 1, "mvp2005fix.dll");
    scenario("delayed-ready", fixture, 10000, INJECTION_OK);
    scenario("reject", fixture, 10000, INJECTION_LOAD_FAILED);
    scenario("missing", "Z:\\missing-mvp-startup-test.dll", 10000, INJECTION_LOAD_FAILED);
    scenario("fail", fixture, 10000, INJECTION_INIT_FAILED);
    scenario("silent", fixture, 1500, INJECTION_INIT_TIMEOUT);
    scenario("slowload", fixture, 1500, INJECTION_LOAD_TIMEOUT);
    scenario("exit", fixture, 10000, INJECTION_PROCESS_EXITED);
    fail_exit_code = 1;
    scenario("exit-code-error", fixture, 10000, INJECTION_LOAD_FAILED);
    fail_exit_code = 0;
    fail_wait = 1;
    scenario("wait-error", fixture, 10000, INJECTION_WAIT_FAILED);
    fail_wait = 0;
    {
        PROCESS_INFORMATION pi = child("collision", FALSE);
        char name[STARTUP_EVENT_NAME_SIZE];
        HANDLE existing;
        startup_event_name(name, pi.dwProcessId, TRUE);
        existing = CreateEventA(NULL, TRUE, TRUE, name);
        assert(existing);
        assert(inject_runtime(pi.hProcess, pi.dwProcessId, fixture, 10000) == INJECTION_EVENT_FAILED);
        assert(!allocation);
        CloseHandle(existing);
        finish(&pi, FALSE);
        puts("PASS existing readiness event rejected");
    }
    scenario("actual-runtime", runtime, 10000, INJECTION_OK);
    SetEnvironmentVariableA("MVP_STARTUP_TEST_MODE", NULL);
    puts("All startup tests passed.");
    return 0;
}
