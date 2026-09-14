#include "core/PowerControl.h"

#include <windows.h>
#include <powrprof.h>

#include <cstdio>

namespace PowerControl {

bool g_testMode = false;

namespace {
bool testPrint(const char* name) {
    std::printf("AUTO:%s (test mode, not executing)\n", name);
    return false;
}
} // namespace

bool perform(Action action) {
    switch (action) {
    case Action::Sleep:
        if (g_testMode)
            return testPrint("SLEEP");
        // bHibernate = FALSE -> suspend (S3)
        return SetSuspendState(FALSE, FALSE, FALSE) != FALSE;
    case Action::Hibernate:
        if (g_testMode)
            return testPrint("HIBERNATE");
        // bHibernate = TRUE -> hibernate (S4)
        return SetSuspendState(TRUE, FALSE, FALSE) != FALSE;
    case Action::Shutdown:
        if (g_testMode)
            return testPrint("SHUTDOWN");
        {
            HANDLE token = GetCurrentProcessToken();
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!LookupPrivilegeValue(nullptr, SE_SHUTDOWN_NAME,
                                      &tp.Privileges[0].Luid))
                return false;
            AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
            if (GetLastError() != ERROR_SUCCESS)
                return false;
            return ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, 0) != FALSE;
        }
    case Action::None:
    default:
        return false;
    }
}

} // namespace PowerControl