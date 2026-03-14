#include "Scanner.h"

#include <vector>
#include <filesystem>

namespace scanner
{
  std::vector<Entry> scan(const fs::path& root)
  {
    std::vector<Entry> result;

    for (const auto& entry: fs::recursive_directory_iterator(root))
    {
      const Entry::EntryType type = entry.is_regular_file() ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY;

      result.emplace_back(Entry{
        entry.path(),
        entry.path().filename(),
        entry.path().extension(),
        type,
        entry.is_regular_file() ? entry.file_size() : 0,
        entry.last_write_time()
      });
    }

    return result;
  }
}
