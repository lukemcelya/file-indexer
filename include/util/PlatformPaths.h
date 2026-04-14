#pragma once

#include <filesystem>

namespace platform
{
  namespace fs = std::filesystem;

  fs::path executablePath();
  fs::path executableDir();
}