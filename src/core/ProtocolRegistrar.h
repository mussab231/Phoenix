#pragma once

#include <string>

// Registers/unregisters Phoenix as the handler for the phoenix:// URL scheme
// in the CURRENT USER hive (no admin rights needed). When a browser or other
// app opens a phoenix:// link, Windows runs Phoenix.exe "<link>".
//
// Returns true on success. Failures are silent (registration is a bonus, not
// a requirement for the app to work).
namespace ProtocolRegistrar {

constexpr const char* kScheme = "phoenix";

bool isRegistered();
bool registerHandler();     // exe path is taken from GetModuleFileName
bool unregisterHandler();

} // namespace ProtocolRegistrar