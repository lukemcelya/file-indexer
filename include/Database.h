#pragma once

#include "Index.h"
#include "Entry.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>
#include <unordered_map>
#include <ostream>

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
  sqlite3* m_db = nullptr;\
  sqlite3_stmt* m_stmtIndexInsert{};
  sqlite3_stmt* m_stmtEntryInsert{};
  sqlite3_stmt* m_stmtEntryDelete{};
  sqlite3_stmt* m_stmtEntryUpdate{};

public:
  Database() : Database("indexes.db") {};
  explicit Database(const std::string& dbPath);
  ~Database();

  bool exec(std::string_view query);
  void beginTransaction();
  void commit();

  // Scan functions
  std::expected<std::int64_t, Error> insertIndex(const Index& index);
  void prepareEntryInsert();
  void prepareEntryDelete();
  void prepareEntryUpdate();
  std::expected<void, Error> insertEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, Error> deleteEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, Error> updateEntry(std::int64_t indexId, const Entry& entry);

  void finalizeIndexInsert();
  void finalizeEntryInsert();
  void finalizeEntryDelete();
  void finalizeEntryUpdate();

  std::vector<Index> loadIndexes();
  std::unordered_map<std::string, Entry> loadEntriesFromIndex(std::int64_t indexId);

  // Rescan functions TODO: update existing entries
  void deleteEntriesForIndex(std::int64_t indexId);

private:
  void initializeSchema();

  static std::int64_t toUnixTime(fs::file_time_type time);
  static fs::file_time_type toFileTime(std::int64_t time);
};
