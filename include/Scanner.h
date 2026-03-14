#pragma once

#include "Entry.h"

#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace scanner
{
  std::vector<Entry> scan(const fs::path& root);
}
