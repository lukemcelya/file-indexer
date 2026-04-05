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
  sqlite3* m_db = nullptr;
  sqlite3_stmt* m_stmtIndexPath{};
  sqlite3_stmt* m_stmtIndexInsert{};
  sqlite3_stmt* m_stmtIndexShow{};
  sqlite3_stmt* m_stmtIndexLoad{};
  sqlite3_stmt* m_stmtIndexStats{};
  sqlite3_stmt* m_stmtEntryInsert{};
  sqlite3_stmt* m_stmtEntryDelete{};
  sqlite3_stmt* m_stmtEntryUpdate{};
  sqlite3_stmt* m_stmtEntrySearch{};
  sqlite3_stmt* m_stmtEntryLoad{};
  sqlite3_stmt* m_stmtDuplicateSearch{};

public:
  Database() = default;
  ~Database();

  Database(const Database&) = delete;
  auto operator=(const Database&) -> Database& = delete;

  Database(Database&& other) noexcept;
  auto operator=(Database&& other) noexcept -> Database&;

  // Initialization
  static std::expected<Database, db::Error> open(const fs::path& dbPath);

  // Scan and rescan functions
  std::expected<void, db::Error> finishIndex();
  std::expected<void, db::Error> beginRescan();
  std::expected<void, db::Error> cancelRescan();
  std::expected<void, db::Error> finishRescan();
  std::expected<std::int64_t, db::Error> insertIndex(const Index& index);
  std::expected<void, db::Error> beginEntryInsert();
  std::expected<void, db::Error> finishEntryInsert();
  std::expected<void, db::Error> insertEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, db::Error> deleteEntry(std::int64_t indexId, const Entry& entry);
  std::expected<void, db::Error> updateEntry(std::int64_t indexId, const Entry& entry);

  // Find/Search functions
  std::expected<std::vector<db::FindResult>, db::Error> findEntries(const std::string& query, std::optional<std::int64_t> indexId = std::nullopt);

  // Show function
  std::expected<db::ShowIndexResult, db::Error> showIndex(std::int64_t indexId);

  // Duplicate handling
  std::expected<std::vector<fs::path>, db::Error> findPotentialDuplicates(std::int64_t indexId);

  // Index stats
  std::expected<db::IndexStatsResult, db::Error> getIndexStats(std::int64_t indexId);

  // Index and Entry loading
  std::expected<std::vector<Index>, db::Error> loadIndexes();
  std::expected<std::unordered_map<std::string, Entry>, db::Error> loadEntriesFromIndex(std::int64_t indexId);

private:
  std::expected<void, db::Error> initializeSchema();

  // Transaction handling
  std::expected<void, db::Error> exec(std::string_view query);
  std::expected<void, db::Error> prepare(sqlite3_stmt*& stmt, const char* sql);
  std::expected<void, db::Error> beginTransaction();
  std::expected<void, db::Error> rollback();
  std::expected<void, db::Error> commit();

  // Getter for index path on id
  std::expected<fs::path, db::Error> indexPath(std::int64_t indexId);

  // Private prepare functions (bind input values)
  std::expected<void, db::Error> prepareEntrySearch(const std::string& query, const std::optional<std::int64_t> indexId = std::nullopt);
  std::expected<void, db::Error> prepareIndexShow(std::int64_t indexId);
  std::expected<void, db::Error> prepareDuplicateSearch(std::int64_t indexId);
  std::expected<void, db::Error> prepareIndexPath(std::int64_t indexId);
  std::expected<void, db::Error> prepareIndexStats(std::int64_t indexId);

  // Bind helpers
  std::expected<void, db::Error> bindInt64(sqlite3_stmt* stmt, int index, std::int64_t value) const;
  std::expected<void, db::Error> bindText(sqlite3_stmt* stmt, int index, std::string_view value) const;

  [[nodiscard]] db::Error makeError(const int rc) const;

  // Static helpers
  static std::int64_t toUnixTime(const fs::file_time_type& time);
  static fs::file_time_type toFileTime(std::int64_t time);
  static std::expected<std::int64_t, db::Error> toSqliteFileSize(std::uintmax_t size);

  // Sqlite3_stmt* handling
  std::expected<void, db::Error> stepDone(sqlite3_stmt* stmt);
  std::expected<bool, db::Error> stepRow(sqlite3_stmt* stmt);
  static void resetStatement(sqlite3_stmt* stmt);
  static void finalizeStatement(sqlite3_stmt*& stmt);
  void finalizeAll();
};
