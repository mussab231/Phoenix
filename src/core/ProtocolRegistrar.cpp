#include "core/ProtocolRegistrar.h"

#include <windows.h>
#include <shlobj.h>

#include <string>

namespace ProtocolRegistrar {

namespace {

const std::wstring keyFor() {
    return std::wstring(L"Software\\Classes\\") + L"phoenix";
}

} // namespace

bool isRegistered() {
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, keyFor().c_str(), 0,
                            KEY_READ, &key);
    if (rc != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

bool registerHandler() {
    wchar_t exe[MAX_PATH]{};
    DWORD len = static_cast<DWORD>(sizeof(exe) / sizeof(exe[0]));
    if (!GetModuleFileNameW(nullptr, exe, len))
        return false;
    const std::wstring command = L"\"" + std::wstring(exe) + L"\" \"%1\"";
    const std::wstring root = keyFor();
    const std::wstring icon = std::wstring(exe) + L",0";
    const std::wstring openCmd = root + L"\\shell\\open\\command";

    HKEY rootKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &rootKey, nullptr) != ERROR_SUCCESS)
        return false;

    bool ok = true;
    ok = ok && RegSetValueExW(rootKey, L"URL Protocol", 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(L""),
                              wcslen(L"") + 1) == ERROR_SUCCESS;
    ok = ok && RegSetValueExW(rootKey, L"FriendlyTypeName", 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(L"Phoenix Download Manager"),
                              (wcslen(L"Phoenix Download Manager") + 1) *
                                   sizeof(wchar_t)) == ERROR_SUCCESS;
    RegCloseKey(rootKey);

    HKEY iconKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (root + L"\\DefaultIcon").c_str(), 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &iconKey, nullptr) ==
        ERROR_SUCCESS) {
        ok = ok && RegSetValueExW(iconKey, L"", 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(icon.c_str()),
                                  static_cast<DWORD>((icon.size() + 1) *
                                                         sizeof(wchar_t))) ==
                       ERROR_SUCCESS;
        RegCloseKey(iconKey);
    }

    HKEY cmdKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, openCmd.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &cmdKey, nullptr) == ERROR_SUCCESS) {
        ok = ok && RegSetValueExW(cmdKey, L"", 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(command.c_str()),
                                  static_cast<DWORD>((command.size() + 1) *
                                                         sizeof(wchar_t))) ==
                       ERROR_SUCCESS;
        RegCloseKey(cmdKey);
    }

    // Ask the shell to notice the new association.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool unregisterHandler() {
    LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, keyFor().c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

} // namespace ProtocolRegistrar