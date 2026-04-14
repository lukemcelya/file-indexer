#pragma once

#include "Entry.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace scanner
{
  template <typename Callback>
  auto scan(const fs::path& path, Callback callback)
  {
    using Result = decltype(callback(std::declval<Entry>()));

    for (const auto& entry: fs::recursive_directory_iterator(path))
    {
      const Entry::EntryType type = entry.is_regular_file() ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY;

      Entry e{
        fs::relative(entry.path(), path),
        entry.path().filename(),
        entry.path().extension(),
        type,
        entry.is_regular_file() ? entry.file_size() : 0,
        entry.last_write_time()
      };

      if (auto r = callback(e); !r)
        return r;
    }

    return Result{};
  }
}
