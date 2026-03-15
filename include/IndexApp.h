#pragma once

#include "Index.h"
#include "Database.h"
#include "Scanner.h"

#include <filesystem>

namespace fs = std::filesystem;


class IndexApp
{
  using IndexStore = std::vector<Index>;
private:
  Database m_database;
  IndexStore m_indexStore;

public:
  IndexApp() = default;
  explicit IndexApp(const std::string& dbPath);

  [[nodiscard]]bool isIndexed(const fs::path& path) const;
  bool createIndex(const fs::path& path);
  bool rescanIndex(const fs::path& path);

private:
  static fs::path normalizePath(const fs::path& path);
};
