#include "Duplicates.h"
#include "Hasher.h"

#include <fstream>
#include <unordered_map>

namespace dup
{
  std::vector<DuplicateGroup> find(const std::vector<fs::path>& paths)
  {
    if (paths.empty())
      return {};

    std::unordered_map<std::string, std::vector<fs::path>> hashGroups;

    for (const auto& file : paths)
    {
      std::ifstream ifs(file, std::ios::binary);
      const auto hash = hashing::sha256FileHex(ifs);
      hashGroups[hash].push_back(file);
    }

    std::vector<DuplicateGroup> groups;
    groups.reserve(hashGroups.size());

    for (auto& [hash, files] : hashGroups)
    {
      if (files.size() < 2)
        continue;

      groups.emplace_back(DuplicateGroup{hash, std::move(files)});
    }

    return groups;
  }
}