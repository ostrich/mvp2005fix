#ifndef MVP2005FIX_STARTUP_H
#define MVP2005FIX_STARTUP_H

#include <windows.h>

/* Created by the launcher before injection. Versioned and process-specific;
 * existing objects are rejected by the launcher rather than trusted. */
#define STARTUP_EVENT_NAME_SIZE 80
static inline void startup_event_name(char name[STARTUP_EVENT_NAME_SIZE], DWORD pid, BOOL ready)
{
    wsprintfA(name, "Local\\mvp2005fix.v1.%lu.%s", pid, ready ? "ready" : "failed");
}

static inline BOOL signal_startup_result(BOOL ready)
{
    char name[STARTUP_EVENT_NAME_SIZE];
    HANDLE event;
    BOOL result;
    startup_event_name(name, GetCurrentProcessId(), ready);
    event = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (!event) return FALSE;
    result = SetEvent(event);
    CloseHandle(event);
    return result;
}

#endif
