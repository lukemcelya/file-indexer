#pragma once

#include "Index.h"
#include "Entry.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>

class Database
{
private:
  sqlite3* m_db = nullptr;

public:
  Database() : Database("indexes.db") {};
  explicit Database(const std::string& dbPath);
  ~Database();

  bool exec(std::string_view query);
  std::expected<std::int64_t, std::string> saveIndex(const Index& index, const std::vector<Entry>& entries);
  std::vector<Index> loadIndexes();

private:
  void initializeSchema();

  static std::int64_t toUnixTime(fs::file_time_type time);
};
