#pragma once

#include "Index.h"
#include "Entry.h"
#include "DbResults.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>
#include <optional>
#include <unordered_map>

namespace fs = std::filesystem;

class Database
{
private:
  sqlite3* m_db = nullptr;\
  sqlite3_stmt* m_stmtIndexPath{};
  sqlite3_stmt* m_stmtIndexInsert{};
  sqlite3_stmt* m_stmtIndexShow{};
  sqlite3_stmt* m_stmtEntryInsert{};
  sqlite3_stmt* m_stmtEntryDelete{};
  sqlite3_stmt* m_stmtEntryUpdate{};
  sqlite3_stmt* m_stmtEntrySearch{};
  sqlite3_stmt* m_stmtDuplicateSearch{};

public:
  Database() : Database("indexes.db") {};
  explicit Database(const fs::path& dbPath);
  ~Database();

  // Transaction handling
  bool exec(std::string_view query);
  void beginTransaction();
  void commit();

  // Scan and rescan functions
  std::expected<std::int64_t, db::Error> insertIndex(const Index& index);
  void prepareEntryInsert();
  void prepareEntryDelete();
  void prepareEntryUpdate();
  std::expected<void, db::Error> insertEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, db::Error> deleteEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, db::Error> updateEntry(std::int64_t indexId, const Entry& entry);

  // Find/Search functions
  std::expected<std::vector<db::FindResult>, db::Error> findEntries(const std::string& query, std::optional<std::int64_t> indexId = std::nullopt);
  void prepareEntrySearchNoId(const std::string& query);
  void prepareEntrySearch(const std::string& query, std::int64_t indexId);

  // Show function
  std::expected<db::ShowIndexResult, db::Error> showIndex(std::int64_t indexId);
  void prepareIndexShow(std::int64_t indexId);

  // Duplicate handling
  std::expected<std::vector<fs::path>, db::Error> findPotentialDuplicates(std::int64_t indexId);
  void prepareDuplicateSearch(std::int64_t indexId);

  // Sqlite3_stmt* cleanup
  void finalizeIndexInsert();
  void finalizeEntryInsert();
  void finalizeEntryDelete();
  void finalizeEntryUpdate();
  void finalizeIndexShow();
  void finalizeDuplicateSearch();
  void finalizeIndexPath();

  // Index and Entry loading
  std::vector<Index> loadIndexes();
  std::unordered_map<std::string, Entry> loadEntriesFromIndex(std::int64_t indexId);
  std::expected<fs::path, db::Error> indexPath(std::int64_t indexId);
  void prepareIndexPath(std::int64_t indexId);

private:
  void initializeSchema();

  // Static helpers (convert int64_t time <> file_time_type)
  static std::int64_t toUnixTime(const fs::file_time_type& time);
  static fs::file_time_type toFileTime(std::int64_t time);
  static std::int64_t toSqliteFileSize(std::uintmax_t size);
};
