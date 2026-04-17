#pragma once

#include <filesystem>

namespace util
{
  namespace fs = std::filesystem;

  fs::path executablePath();
  fs::path executableDir();
}