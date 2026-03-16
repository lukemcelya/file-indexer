#pragma once

#include "Index.h"
#include "Database.h"
#include "Scanner.h"

#include <filesystem>
#include <string>
#include <ostream>

namespace fs = std::filesystem;


class IndexApp
{
  using IndexStore = std::vector<Index>;

  struct RescanStats
  {
    std::uint64_t added = 0;
    std::uint64_t deleted = 0;
    std::uint64_t modified = 0;
    std::uint64_t unmodified = 0;
  };

private:
  Database m_database;
  IndexStore m_indexStore;

public:
  IndexApp() = default;
  explicit IndexApp(const std::string& dbPath);

  [[nodiscard]]bool isIndexed(const fs::path& path) const;
  [[nodiscard]]bool isEntryChanged(const Entry& oldEntry, const Entry& newEntry);
  bool createIndex(const fs::path& path);
  std::expected<RescanStats, std::string> rescanIndex(const fs::path& path);

private:
  static fs::path normalizePath(const fs::path& path);
};
