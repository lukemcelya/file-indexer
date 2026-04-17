#pragma once

#include <filesystem>
#include <cstdint>

namespace fs = std::filesystem;

namespace util
{
  std::int64_t toUnixTime(const fs::file_time_type& time);
  fs::file_time_type toFileTime(std::int64_t time);
}