#include "Database.h"
#include "Index.h"
#include "Entry.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>
#include <chrono>
#include <unordered_map>
#include <ostream>

Database::Database(const std::string& dbPath)
{
  const int rc = sqlite3_open(dbPath.c_str(), &m_db);

  if (rc != SQLITE_OK)
    throw std::runtime_error("Can't open database: " + std::string(sqlite3_errmsg(m_db)));

  initializeSchema();
}

Database::~Database()
{
  sqlite3_close(m_db);
}

bool Database::exec(std::string_view query)
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
    &m_stmt,
    nullptr
    );

  sqlite3_bind_text(m_stmt, 1, index.root().string().c_str(), -1 , SQLITE_TRANSIENT);
  sqlite3_bind_int64(m_stmt, 2, index.createdAt());
  sqlite3_bind_int64(m_stmt, 3, index.lastScannedAt());

  if (sqlite3_step(m_stmt) != SQLITE_DONE)
  {
    finalizeStatement();
    return std::unexpected(Error{
    sqlite3_errcode(m_db),
    sqlite3_errmsg(m_db)
    });
  }

  const sqlite3_int64 indexId = sqlite3_last_insert_rowid(m_db);

  finalizeStatement();

  return indexId;
}

void Database::prepareEntryInsert()
{
  sqlite3_prepare_v2(
    m_db,
    "INSERT INTO entries (index_id, relative_path, name, extension, is_directory, size_bytes, last_written_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
    -1,
    &m_stmt,
    nullptr);
}

void Database::prepareEntryDelete()
{
  sqlite3_prepare_v2(
    m_db,
    "DELETE FROM entries WHERE index_id = ? AND relative_path = ?;",
    -1,
    &m_stmt,
    nullptr);
}

void Database::prepareEntryUpdate()
{
  sqlite3_prepare_v2(
    m_db,
    "UPDATE entries SET size_bytes = ?, last_written_at = ? WHERE index_id = ? AND relative_path = ?",
    -1,
    &m_stmt,
    nullptr);
}

auto Database::insertEntry(std::int64_t indexId, const Entry& entry) -> std::expected<void, Error>
{
  // index_id > relative_path > name > extension > is_directory > size_bytes > last_written_at
  sqlite3_bind_int64(m_stmt, 1, indexId);
  sqlite3_bind_text(m_stmt, 2, entry.path.string().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt, 3, entry.name.string().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt, 4, entry.extension.string().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(m_stmt, 5, static_cast<std::int64_t>(entry.type)); // Cast enum to int
  sqlite3_bind_int64(m_stmt, 6, static_cast<std::int64_t>(entry.size));
  sqlite3_bind_int64(m_stmt, 7, toUnixTime(entry.lastWrittenAt));

  if (sqlite3_step(m_stmt) != SQLITE_DONE)
  {
    finalizeStatement();
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
        sqlite3_errmsg(m_db)
    });
  }

  sqlite3_reset(m_stmt);
  sqlite3_clear_bindings(m_stmt);

  return {};
}

auto Database::deleteEntry(std::int64_t indexId, const Entry& entry) -> std::expected<void, Error>
{
  sqlite3_bind_int64(m_stmt, 1, indexId);
  sqlite3_bind_text(m_stmt, 2, entry.path.string().c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(m_stmt) != SQLITE_DONE)
  {
    finalizeStatement();
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)});
  }

  sqlite3_reset(m_stmt);
  sqlite3_clear_bindings(m_stmt);

  return {};
}

auto Database::updateEntry(std::int64_t indexId, const Entry& entry) -> std::expected<void, Error>
{
  // size_bytes > last_written_at > index_id > relative_path
  sqlite3_bind_int64(m_stmt, 1, entry.size);
  sqlite3_bind_int64(m_stmt, 2, toUnixTime(entry.lastWrittenAt));
  sqlite3_bind_int64(m_stmt, 3, indexId);
  sqlite3_bind_text(m_stmt, 4, entry.path.string().c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(m_stmt) != SQLITE_DONE)
  {
    return std::unexpected(Error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)});
  }

  sqlite3_reset(m_stmt);
  sqlite3_clear_bindings(m_stmt);

  return {};
}

void Database::finalizeStatement()
{
  sqlite3_finalize(m_stmt);
}

std::vector<Index> Database::loadIndexes()
{
  std::vector<Index> indexes;

  sqlite3_prepare_v2(m_db, "SELECT * FROM indexes;", -1, &m_stmt, nullptr);

  while (sqlite3_step(m_stmt) == SQLITE_ROW)
  {
    const std::int64_t id = sqlite3_column_int64(m_stmt, 0);

    const unsigned char* pathText = sqlite3_column_text(m_stmt, 1);
    fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::int64_t createdAt = sqlite3_column_int64(m_stmt, 2);

    const std::int64_t lastScannedAt = sqlite3_column_int64(m_stmt, 3);

    indexes.emplace_back(Index{id, std::move(path), createdAt, lastScannedAt});
  }

  finalizeStatement();
  return indexes;
}

std::unordered_map<std::string, Entry> Database::loadEntriesFromIndex(const Index& index)
{
  std::unordered_map<std::string, Entry> entries;

  sqlite3_prepare_v2(m_db,
    "SELECT (relative_path, name, extension, is_directory, size_bytes, last_written_at) FROM entries WHERE index_id = ?;",
    -1,
    &m_stmt,
    nullptr);

  sqlite3_bind_int64(m_stmt, 1, index.id());

  while (sqlite3_step(m_stmt) == SQLITE_ROW)
  {
    const unsigned char* pathText = sqlite3_column_text(m_stmt, 0);
    fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    std::string pathKey = path.string();

    const unsigned char* nameText = sqlite3_column_text(m_stmt, 1);
    const fs::path name = nameText ? fs::path(reinterpret_cast<const char*>(nameText)) : fs::path{};

    const unsigned char* extText = sqlite3_column_text(m_stmt, 2);
    const fs::path ext = extText ? fs::path(reinterpret_cast<const char*>(extText)) : fs::path{};

    const std::int64_t type = sqlite3_column_int64(m_stmt, 3) == 0 ? 0 : 1; // 0 is FILE type, anything else is DIRECTORY

    const std::uintmax_t size = sqlite3_column_int64(m_stmt, 4);

    const fs::file_time_type lastWritten = toFileTime(sqlite3_column_int64(m_stmt, 5));

    entries.try_emplace(pathKey, Entry{
      path,
      name,
      ext,
      type == 0 ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY,
      size,
      lastWritten
    });
  }

  finalizeStatement();
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
std::int64_t Database::toUnixTime(const fs::file_time_type time)
{
  const auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(
    time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

  return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

// Convert back to file_time_type from Unix Time
fs::file_time_type Database::toFileTime(const std::int64_t time)
{
  const auto sysTime = std::chrono::system_clock::time_point{
    std::chrono::seconds{time}
  };

  return std::chrono::clock_cast<fs::file_time_type::clock>(sysTime);
}
