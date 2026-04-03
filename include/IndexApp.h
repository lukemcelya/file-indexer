#pragma once

#include "Index.h"
#include "Database.h"
#include "Scanner.h"
#include "Duplicates.h"
#include "AppResults.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class IndexApp
{
private:
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
  IndexStore m_indexStore{};

public:
  explicit IndexApp(Database db);

  std::expected<void, app::Error> loadIndexStore();

  std::expected<std::int64_t, app::Error> createIndex(const fs::path& path);
  std::expected<RescanStats, app::Error> rescanIndex(const fs::path& path);
  std::expected<std::vector<db::FindResult>, app::Error> findAllEntries(const std::string& query);
  std::expected<db::ShowIndexResult, app::Error> showIndex(std::int64_t id);
  std::expected<std::vector<dup::DuplicateGroup>, app::Error> findDuplicates(std::int64_t id);

private:
  [[nodiscard]] bool isIndexed(const fs::path& path) const;
  [[nodiscard]] bool isIndexed(std::int64_t id) const;

  static bool isEntryChanged(const Entry& oldEntry, const Entry& newEntry);
  static fs::path normalizePath(const fs::path& path);
};
