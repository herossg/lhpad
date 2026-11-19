#include "core.h"
#include "shell_mode.hpp"
#include <exception>
#include <fstream>
#include <shlwapi.h>
#include <thread>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>
namespace lp {
namespace {
constexpr wchar_t publisher[] = L"CN=LHZip Development";
constexpr wchar_t packageName[] = L"LHPad.Explorer";
winrt::Windows::Foundation::Uri fileUri(const std::filesystem::path &path) {
  wchar_t url[32768]; DWORD size = 32768;
  winrt::check_hresult(UrlCreateFromPathW(path.c_str(), url, &size, 0));
  return winrt::Windows::Foundation::Uri(url);
}
bool currentPackage(const winrt::Windows::ApplicationModel::Package &package) {
  try {
  const auto version = package.Id().Version();
  const auto location = std::filesystem::path(package.EffectiveExternalLocation().Path().c_str()).lexically_normal();
  const auto expected = exeDir().lexically_normal();
  std::error_code fileError;
  return version.Major == 1 && version.Minor == 1 && version.Build == 2 &&
         version.Revision == 0 && package.Status().VerifyIsOK() &&
         std::filesystem::is_regular_file(expected / L"LHPad.exe", fileError) &&
         std::filesystem::is_regular_file(expected / L"LHPadShell.dll", fileError) &&
         CompareStringOrdinal(location.c_str(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
  } catch (const winrt::hresult_error &) { return false; }
}
template<class Work> void inMta(Work work) {
  std::exception_ptr failure;
  std::thread worker([&] {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      struct Cleanup { ~Cleanup() { winrt::uninit_apartment(); } } cleanup;
      work();
    } catch (...) { failure = std::current_exception(); }
  });
  worker.join();
  if (failure) std::rethrow_exception(failure);
}
void deploy(bool enable) {
  using namespace winrt::Windows::Management::Deployment;
  using winrt::Windows::Foundation::Metadata::ApiInformation;
  if (enable && (!explorer_menu::preferPackaged() ||
      !ApiInformation::IsPropertyPresent(L"Windows.Management.Deployment.AddPackageOptions", L"AllowUnsigned")))
    throw std::runtime_error("This Windows environment cannot register the packaged Explorer menu.");
  PackageManager manager;
  std::vector<winrt::Windows::ApplicationModel::Package> installed;
  for (const auto &package : manager.FindPackagesForUser(L"", packageName, publisher))
    installed.push_back(package);
  bool current = false;
  for (const auto &package : installed)
    current = current || (enable && currentPackage(package));
  if (enable && current && installed.size() == 1) return;
  if (enable && !current) {
    const auto directory = exeDir();
    for (const auto *file : {L"LHPad.exe", L"LHPadShell.dll", L"LHPad.Explorer.msix"})
      if (!std::filesystem::is_regular_file(directory / file))
        throw std::runtime_error("The complete LHPad Explorer menu distribution is required before registration.");
  }
  // Remove all versions on disable, extra versions when a valid provider is
  // present, and same-version packages whose location/status needs repair.
  std::exception_ptr removalFailure;
  for (const auto &package : installed) {
    const auto version = package.Id().Version();
    const bool sameVersion = version.Major == 1 && version.Minor == 1 &&
                             version.Build == 2 && version.Revision == 0;
    if (!enable || (current && !currentPackage(package)) ||
        (!current && sameVersion)) {
      try {
        auto result = manager.RemovePackageAsync(package.Id().FullName()).get();
        winrt::check_hresult(result.ExtendedErrorCode());
      } catch (...) { if (!removalFailure) removalFailure = std::current_exception(); }
    }
  }
  if (removalFailure) std::rethrow_exception(removalFailure);
  if (!enable || current) return;
  const auto directory = exeDir();
  const auto path = directory / L"LHPad.Explorer.msix";
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("LHPad.Explorer.msix is missing beside LHPad.exe.");
  AddPackageOptions options;
  options.ExternalLocationUri(fileUri(directory));
  auto result = manager.AddPackageByUriAsync(fileUri(path), options).get();
  if (FAILED(result.ExtendedErrorCode()))
    throw std::runtime_error("Windows 11 Explorer menu registration failed: " + winrt::to_string(result.ErrorText()));
  size_t count = 0;
  bool valid = false;
  for (const auto &package : manager.FindPackagesForUser(L"", packageName, publisher)) {
    ++count; valid = valid || currentPackage(package);
  }
  if (count != 1 || !valid)
    throw std::runtime_error("Explorer menu registration did not produce one healthy package at the current location.");
}
} // namespace
bool modernExplorerRegistered() {
  bool current = false;
  try { inMta([&] {
    winrt::Windows::Management::Deployment::PackageManager manager;
    size_t count = 0;
    for (const auto &package : manager.FindPackagesForUser(L"", packageName, publisher)) {
      ++count; current = current || currentPackage(package);
    }
    current = current && count == 1;
  }); } catch (const winrt::hresult_error &e) {
    throw std::runtime_error(winrt::to_string(e.message()));
  }
  return current;
}
void configureModernExplorer(bool enable) {
  try {
    inMta([&] { deploy(enable); });
    std::ofstream(exeDir() / L"shell-package-status.txt")
        << (enable ? "Windows 11 menu registration checked successfully.\n"
                   : "Windows 11 menu package removed.\n");
  } catch (const winrt::hresult_error &e) {
    auto message = winrt::to_string(e.message());
    std::ofstream(exeDir() / L"shell-package-status.txt") << message;
    throw std::runtime_error(message);
  } catch (const std::exception &e) {
    std::ofstream(exeDir() / L"shell-package-status.txt") << e.what();
    throw;
  }
}
} // namespace lp
