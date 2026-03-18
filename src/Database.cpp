#include "Database.h"
#include "Index.h"
#include "Entry.h"

#include <sqlite3.h>
#include <filesystem>
#include <string>
#include <vector>
#include <expected>
#include <chrono>
#include <unordered_map>

Database::Database(const fs::path& dbPath)
{
  fs::create_directories(dbPath.parent_path());

  const int rc = sqlite3_open(dbPath.string().c_str(), &m_db);

  if (rc != SQLITE_OK)
    throw std::runtime_error("Can't open database: " + std::string(sqlite3_errmsg(m_db)));

  initializeSchema();
}

Database::~Database()
{
  sqlite3_close(m_db);
}

bool Database::exec(const std::string_view query)
{
  char* errMsg;

  int rc = sqlite3_exec(m_db, query.data(), nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK)
  {
    const std::string errMsgStr = errMsg ? errMsg : sqlite3_errmsg(m_db); // So errMsg can be freed before exception
    sqlite3_free(errMsg);
    throw std::runtime_error(errMsgStr);
  }

  return true;
}

void Database::beginTransaction()
{
  exec("BEGIN TRANSACTION;");
}

void Database::commit()
{
  exec("COMMIT;");
}

auto Database::insertIndex(const Index& index) -> std::expected<std::int64_t, Error>
{
  sqlite3_prepare_v2(
    m_db,
    "INSERT INTO indexes (root_path, created_at, last_scanned_at) VALUES (?, ?, ?);",
    -1,
    &m_stmtIndexInsert,
    nullptr
    );

  sqlite3_bind_text(m_stmtIndexInsert, 1, index.root().string().c_str(), -1 , SQLITE_TRANSIENT);
  sqlite3_bind_int64(m_stmtIndexInsert, 2, index.createdAt());
  sqlite3_bind_int64(m_stmtIndexInsert, 3, index.lastScannedAt());

  if (sqlite3_step(m_stmtIndexInsert) != SQLITE_DONE)
  {
    finalizeEntryInsert();
    return std::unexpected(Error{
    sqlite3_errcode(m_db),
    sqlite3_errmsg(m_db)
    });
  }

  const sqlite3_int64 indexId = sqlite3_last_insert_rowid(m_db);

  finalizeIndexInsert();

  return indexId;
}

void Database::prepareEntryInsert()
{
  sqlite3_prepare_v2(
    m_db,
    "INSERT INTO entries (index_id, relative_path, name, extension, is_directory, size_bytes, last_written_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
    -1,
    &m_stmtEntryInsert,
    nullptr);
}

void Database::prepareEntryDelete()
{
  sqlite3_prepare_v2(
    m_db,
    "DELETE FROM entries WHERE index_id = ? AND relative_path = ?;",
    -1,
    &m_stmtEntryDelete,
    nullptr);
}

void Database::prepareEntryUpdate()
{
  sqlite3_prepare_v2(
    m_db,
    "UPDATE entries SET size_bytes = ?, last_written_at = ? WHERE index_id = ? AND relative_path = ?",
    -1,
    &m_stmtEntryUpdate,
    nullptr);
}

auto Database::insertEntry(const std::int64_t indexId, const Entry& entry) -> std::expected<void, Error>
{
  // index_id > relative_path > name > extension > is_directory > size_bytes > last_written_at
  sqlite3_bind_int64(m_stmtEntryInsert, 1, indexId);
  sqlite3_bind_text(m_stmtEntryInsert, 2, entry.path.string().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmtEntryInsert, 3, entry.name.string().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmtEntryInsert, 4, entry.extension.string().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(m_stmtEntryInsert, 5, static_cast<std::int64_t>(entry.type)); // Cast enum to int
  sqlite3_bind_int64(m_stmtEntryInsert, 6, static_cast<std::int64_t>(entry.size));
  sqlite3_bind_int64(m_stmtEntryInsert, 7, toUnixTime(entry.lastWrittenAt));

  if (sqlite3_step(m_stmtEntryInsert) != SQLITE_DONE)
  {
    finalizeEntryInsert();
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
        sqlite3_errmsg(m_db)
    });
  }

  sqlite3_reset(m_stmtEntryInsert);
  sqlite3_clear_bindings(m_stmtEntryInsert);

  return {};
}

auto Database::deleteEntry(const std::int64_t indexId, const Entry& entry) -> std::expected<void, Error>
{
  sqlite3_bind_int64(m_stmtEntryDelete, 1, indexId);
  sqlite3_bind_text(m_stmtEntryDelete, 2, entry.path.string().c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(m_stmtEntryDelete) != SQLITE_DONE)
  {
    finalizeEntryInsert();
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)});
  }

  sqlite3_reset(m_stmtEntryDelete);
  sqlite3_clear_bindings(m_stmtEntryDelete);

  return {};
}

auto Database::updateEntry(const std::int64_t indexId, const Entry& entry) -> std::expected<void, Error>
{
  // size_bytes > last_written_at > index_id > relative_path
  sqlite3_bind_int64(m_stmtEntryUpdate, 1, entry.size);
  sqlite3_bind_int64(m_stmtEntryUpdate, 2, toUnixTime(entry.lastWrittenAt));
  sqlite3_bind_int64(m_stmtEntryUpdate, 3, indexId);
  sqlite3_bind_text(m_stmtEntryUpdate, 4, entry.path.string().c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(m_stmtEntryUpdate) != SQLITE_DONE)
  {
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)});
  }

  sqlite3_reset(m_stmtEntryUpdate);
  sqlite3_clear_bindings(m_stmtEntryUpdate);

  return {};
}

auto Database::findEntries(const std::string& query, const std::optional<std::int64_t> indexId) -> std::expected<std::vector<FindResult>, Error>
{
  if (indexId == std::nullopt || !indexId.has_value())
    prepareEntrySearchNoId(query);
  else
    prepareEntrySearch(query, *indexId);

  std::vector<FindResult> result{};

  int rc;
  while ((rc = sqlite3_step(m_stmtEntrySearch)) == SQLITE_ROW)
  {
    const unsigned char* pathText = sqlite3_column_text(m_stmtEntrySearch, 0);
    const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::int64_t type = sqlite3_column_int64(m_stmtEntrySearch, 1) == 0 ? 0 : 1;

    const std::uintmax_t size = sqlite3_column_int64(m_stmtEntrySearch, 2);

    result.emplace_back(FindResult{
      path,
      type == 0 ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY,
      size
      });
  }

  if (rc != SQLITE_DONE)
  {
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)});
  }

  return result;
}

void Database::prepareEntrySearchNoId(const std::string& query)
{
  sqlite3_prepare_v2(
    m_db,
    "SELECT relative_path, is_directory, size_bytes FROM entries WHERE relative_path LIKE ? OR name LIKE ? OR extension LIKE?;",
    -1,
    &m_stmtEntrySearch,
    nullptr);

  const std::string newQuery = "%" + query + "%";

  sqlite3_bind_text(m_stmtEntrySearch, 1, newQuery.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmtEntrySearch, 2, newQuery.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmtEntrySearch, 3, newQuery.c_str(), -1, SQLITE_TRANSIENT);
}

void Database::prepareEntrySearch(const std::string& query, const std::int64_t indexId)
{
  sqlite3_prepare_v2(
    m_db,
    "SELECT relative_path, is_directory, size_bytes FROM entries WHERE index_id = ? AND relative_path LIKE ? OR name LIKE ? OR extension LIKE?;",
    -1,
    &m_stmtEntrySearch,
    nullptr);

  const std::string newQuery = "%" + query + "%";

  sqlite3_bind_int64(m_stmtEntrySearch, 1, indexId);
  sqlite3_bind_text(m_stmtEntrySearch, 2, newQuery.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmtEntrySearch, 3, newQuery.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmtEntrySearch, 4, newQuery.c_str(), -1, SQLITE_TRANSIENT);
}

void Database::finalizeIndexInsert()
{
  if (m_stmtIndexInsert != nullptr)
  {
    sqlite3_finalize(m_stmtIndexInsert);
    m_stmtIndexInsert = nullptr;
  }
}

void Database::finalizeEntryInsert()
{
  if (m_stmtEntryInsert != nullptr)
  {
    sqlite3_finalize(m_stmtEntryInsert);
    m_stmtEntryInsert = nullptr;
  }
}

void Database::finalizeEntryDelete()
{
  if (m_stmtEntryDelete != nullptr)
  {
    sqlite3_finalize(m_stmtEntryDelete);
    m_stmtEntryDelete = nullptr;
  }
}

void Database::finalizeEntryUpdate()
{
  if (m_stmtEntryUpdate != nullptr)
  {
    sqlite3_finalize(m_stmtEntryUpdate);
    m_stmtEntryUpdate = nullptr;
  }
}

std::vector<Index> Database::loadIndexes()
{
  std::vector<Index> indexes;

  sqlite3_prepare_v2(m_db, "SELECT * FROM indexes;", -1, &m_stmtIndexInsert, nullptr);

  while (sqlite3_step(m_stmtIndexInsert) == SQLITE_ROW)
  {
    const std::int64_t id = sqlite3_column_int64(m_stmtIndexInsert, 0);

    const unsigned char* pathText = sqlite3_column_text(m_stmtIndexInsert, 1);
    fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::int64_t createdAt = sqlite3_column_int64(m_stmtIndexInsert, 2);

    const std::int64_t lastScannedAt = sqlite3_column_int64(m_stmtIndexInsert, 3);

    indexes.emplace_back(Index{id, std::move(path), createdAt, lastScannedAt});
  }

  finalizeIndexInsert();
  return indexes;
}

std::unordered_map<std::string, Entry> Database::loadEntriesFromIndex(const std::int64_t indexId)
{
  std::unordered_map<std::string, Entry> entries{};

  sqlite3_prepare_v2(m_db,
    "SELECT relative_path, name, extension, is_directory, size_bytes, last_written_at FROM entries WHERE index_id = ?;",
    -1,
    &m_stmtEntryInsert,
    nullptr);

  sqlite3_bind_int64(m_stmtEntryInsert, 1, indexId);

  while (sqlite3_step(m_stmtEntryInsert) == SQLITE_ROW)
  {
    const unsigned char* pathText = sqlite3_column_text(m_stmtEntryInsert, 0);
    const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::string pathKey = path.string();

    const unsigned char* nameText = sqlite3_column_text(m_stmtEntryInsert, 1);
    const fs::path name = nameText ? fs::path(reinterpret_cast<const char*>(nameText)) : fs::path{};

    const unsigned char* extText = sqlite3_column_text(m_stmtEntryInsert, 2);
    const fs::path ext = extText ? fs::path(reinterpret_cast<const char*>(extText)) : fs::path{};

    const std::int64_t type = sqlite3_column_int64(m_stmtEntryInsert, 3) == 0 ? 0 : 1; // 0 is FILE type, anything else is DIRECTORY

    const std::uintmax_t size = sqlite3_column_int64(m_stmtEntryInsert, 4);

    const fs::file_time_type lastWritten = toFileTime(sqlite3_column_int64(m_stmtEntryInsert, 5));

    entries.try_emplace(pathKey, Entry{
      path,
      name,
      ext,
      type == 0 ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY,
      size,
      lastWritten
    });
  }

  finalizeEntryInsert();
  return entries;
}

void Database::initializeSchema()
{
  std::string query = "CREATE TABLE IF NOT EXISTS indexes( "
                      "id INTEGER PRIMARY KEY, "
                      "root_path TEXT NOT NULL, "
                      "created_at INTEGER NOT NULL, "
                      "last_scanned_at INTEGER NOT NULL);";
  exec(query);

  query = "CREATE TABLE IF NOT EXISTS entries( "
          "id INTEGER PRIMARY KEY, "
          "index_id INTEGER NOT NULL, "
          "relative_path TEXT NOT NULL, "
          "name TEXT NOT NULL, "
          "extension TEXT, "
          "is_directory INTEGER NOT NULL, "
          "size_bytes INTEGER, "
          "last_written_at INTEGER NOT NULL, "
          "FOREIGN KEY(index_id) REFERENCES indexes(id));";
  exec(query);
}

// Converting to int64_t Unix Time from fs::file_time_type
std::int64_t Database::toUnixTime(const fs::file_time_type& time)
{
  const auto sysNow = std::chrono::system_clock::now();
  const auto fileNow = fs::file_time_type::clock::now();

  const auto sysTime = sysNow + (time - fileNow);

  return std::chrono::duration_cast<std::chrono::seconds>(sysTime.time_since_epoch()).count();
}

// Convert back to file_time_type from Unix Time
fs::file_time_type Database::toFileTime(const std::int64_t time)
{
  const auto sysNow = std::chrono::system_clock::now();
  const auto fileNow = fs::file_time_type::clock::now();

  const auto sysTime = std::chrono::system_clock::time_point{std::chrono::seconds{time}};

  return fileNow + (sysTime - sysNow);
}
