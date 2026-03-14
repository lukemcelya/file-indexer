#pragma once

#include "Index.h"
#include "Entry.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>

class Database
{
public:
  struct Error
  {
    int code{};
    std::string message;

    friend std::ostream& operator<<(std::ostream& os, const Error& e)
    {
      return os << "SQLite error (" << e.code << "): " << e.message;
    }
  };

private:
  sqlite3* m_db = nullptr;
  sqlite3_stmt* m_stmt{};

public:
  Database() : Database("indexes.db") {};
  explicit Database(const std::string& dbPath);
  ~Database();

  bool exec(std::string_view query);
  void beginTransaction();
  void commit();

  std::expected<std::int64_t, Error> insertIndex(const Index& index);
  void prepareEntryInsert();
  std::expected<void, Error> insertEntry(std::int64_t indexId, const Entry& entry);
  void finalizeEntryInsert();
  std::vector<Index> loadIndexes();

private:
  void initializeSchema();

  static std::int64_t toUnixTime(fs::file_time_type time);
};
