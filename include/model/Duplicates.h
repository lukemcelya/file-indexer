#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace dup
{
  namespace fs = std::filesystem;

  struct DuplicateGroup
  {
    std::string hash;
    std::vector<fs::path> files;
  };

  std::vector<DuplicateGroup> find(const std::vector<fs::path>& paths);
}