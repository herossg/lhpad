#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>
namespace explorer_menu {
constexpr bool packagedForVersion(DWORD major, DWORD build, BYTE productType) {
  return productType == VER_NT_WORKSTATION &&
         (major > 10 || (major == 10 && build >= 22000));
}
inline bool preferPackaged() {
  using RtlVersion = LONG (WINAPI *)(OSVERSIONINFOEXW *);
  auto ntdll = GetModuleHandleW(L"ntdll.dll");
  auto version = ntdll ? reinterpret_cast<RtlVersion>(
                            GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  OSVERSIONINFOEXW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  if (!version || version(&info) < 0)
    throw std::runtime_error("Unable to determine the Windows Explorer menu environment.");
  return packagedForVersion(info.dwMajorVersion, info.dwBuildNumber, info.wProductType);
}
class RegistrationLock {
  HANDLE handle{};
public:
  explicit RegistrationLock(const wchar_t *name) {
    handle = CreateMutexW(nullptr, FALSE, name);
    if (!handle)
      throw std::runtime_error("Unable to lock Explorer menu registration.");
    auto result = WaitForSingleObject(handle, 30000);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
      CloseHandle(handle); handle = nullptr;
      throw std::runtime_error("Another Explorer menu registration is still running.");
    }
  }
  ~RegistrationLock() { if (handle) { ReleaseMutex(handle); CloseHandle(handle); } }
  RegistrationLock(const RegistrationLock &) = delete;
  RegistrationLock &operator=(const RegistrationLock &) = delete;
};
} // namespace explorer_menu
