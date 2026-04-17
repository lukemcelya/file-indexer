#include "FileTime.h"

#include <filesystem>
#include <cstdint>

namespace util
{
  std::int64_t toUnixTime(const fs::file_time_type& time)
  {
    const auto sysNow = std::chrono::system_clock::now();
    const auto fileNow = fs::file_time_type::clock::now();

    const auto sysTime = sysNow + (time - fileNow);

    return std::chrono::duration_cast<std::chrono::seconds>(sysTime.time_since_epoch()).count();
  }

  fs::file_time_type toFileTime(const std::int64_t time)
  {
    const auto sysNow = std::chrono::system_clock::now();
    const auto fileNow = fs::file_time_type::clock::now();

    const auto sysTime = std::chrono::system_clock::time_point{std::chrono::seconds{time}};

    return fileNow + (sysTime - sysNow);
  }
}