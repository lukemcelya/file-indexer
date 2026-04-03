#include "PlatformPaths.h"

#include <stdexcept>

#if defined(_WIN32)
  #include <windows.h>
#elif defind(__linux__)
  #include <unistd.h>
  #include <limits.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
#endif

namespace platform
{
  fs::path executablePath()
  {
  #if defined(_WIN32)

    char buffer[MAX_PATH];
    DWORD len = GetModuleFileName(nullptr, buffer, MAX_PATH);

    if (len == 0)
      throw std::runtime_error("Failed to get exe path");

    return fs::path(buffer);

  #elif defined(__linux__)

    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

    if (len == -1)
      throw std::runtime_error("Failed to get exe path");

    buffer[len] = '\0';
    return fs::path(buffer);

  #elif defined(__APPLE__)

    uint32_t size = 0;
    _NSGetExecutablePath(buffer, &size);

    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
      throw std::runtime_error("Failed to get exe path");

    return fs::path(buffer);

  #else
    #error Unsupported platform
  #endif
  }

  fs::path executableDir()
  {
    return executablePath().parent_path();
  }
}