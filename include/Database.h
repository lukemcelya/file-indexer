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

  struct FindResult
  {
    fs::path path;
    Entry::EntryType type;
    std::uintmax_t size;
  };

private:
  sqlite3* m_db = nullptr;\
  sqlite3_stmt* m_stmtIndexInsert{};
  sqlite3_stmt* m_stmtEntryInsert{};
  sqlite3_stmt* m_stmtEntryDelete{};
  sqlite3_stmt* m_stmtEntryUpdate{};
  sqlite3_stmt* m_stmtEntrySearch{};

public:
  Database() : Database("indexes.db") {};
  explicit Database(const std::string& dbPath);
  ~Database();

  // Transaction handling
  bool exec(std::string_view query);
  void beginTransaction();
  void commit();

  // Scan and rescan functions
  std::expected<std::int64_t, Error> insertIndex(const Index& index);
  void prepareEntryInsert();
  void prepareEntryDelete();
  void prepareEntryUpdate();
  std::expected<void, Error> insertEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, Error> deleteEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, Error> updateEntry(std::int64_t indexId, const Entry& entry);

  // Find/Search functions
  std::expected<std::vector<FindResult>, Error> findEntries(const std::string& query, std::optional<std::int64_t> indexId = std::nullopt);
  void prepareEntrySearchNoId(const std::string& query);
  void prepareEntrySearch(const std::string& query, std::int64_t indexId);

  // Sqlite3_stmt* cleanup
  void finalizeIndexInsert();
  void finalizeEntryInsert();
  void finalizeEntryDelete();
  void finalizeEntryUpdate();

  // Index and Entry loading
  std::vector<Index> loadIndexes();
  std::unordered_map<std::string, Entry> loadEntriesFromIndex(std::int64_t indexId);

private:
  void initializeSchema();

  // Static helpers (convert int64_t time <> file_time_type)
  static std::int64_t toUnixTime(fs::file_time_type time);
  static fs::file_time_type toFileTime(std::int64_t time);
};
