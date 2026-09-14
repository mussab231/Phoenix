#pragma once

// Windows power actions. `g_testMode` disables the real system call so the
// headless self-tests can verify the logic (and the UI wiring) without
// actually putting the machine to sleep.
namespace PowerControl {

    enum class Action : int {
        None = 0,
        Sleep = 1,      // suspend (S3)
        Hibernate = 2,  // S4
        Shutdown = 3    // power off
    };

    bool perform(Action action);
    extern bool g_testMode;
} // namespace PowerControl