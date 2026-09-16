#pragma once

#include <string>

// RFC 4122 v4 UUIDs. Download ids are only unique within a single process run;
// a UUID lets a persisted item, a restored session, or a browser-extension
// request keep its identity across restarts.
class Uuid {
public:
    // Random v4 UUID, lowercase, dashed (e.g.
    // "f47ac10b-58cc-4372-a567-0e02b2c3d479"). Uses a thread-safe RNG seeded
    // from the OS, never a weak LCG.
    static std::string create();
};
