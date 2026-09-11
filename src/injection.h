#ifndef MVP2005FIX_INJECTION_H
#define MVP2005FIX_INJECTION_H

#include <windows.h>

typedef enum InjectionResult {
    INJECTION_OK,
    INJECTION_EVENT_FAILED,
    INJECTION_LOAD_FAILED,
    INJECTION_LOAD_TIMEOUT,
    INJECTION_INIT_FAILED,
    INJECTION_INIT_TIMEOUT,
    INJECTION_PROCESS_EXITED,
    INJECTION_WAIT_FAILED
} InjectionResult;

/* The caller owns the suspended process and must terminate it on failure.
 * This function never resumes its main thread. */
InjectionResult inject_runtime(HANDLE process, DWORD pid, const char *dll_path, DWORD timeout_ms);
const char *injection_error_message(InjectionResult result);

#endif
