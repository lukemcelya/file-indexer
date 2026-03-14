#include "Database.h"

#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>

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

std::expected<std::int64_t, std::string> Database::saveIndex(const Index& index, const std::vector<Entry>&  entries)
{
  sqlite3_stmt* stmt;

  sqlite3_prepare_v2(
    m_db,
    "INSERT INTO indexes (root_path, created_at, last_scanned_at) VALUES (?, ?, ?);",
    -1,
    &stmt,
    nullptr
    );

  sqlite3_bind_text(stmt, 1, index.root().string().c_str(), -1 , SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, index.createdAt());
  sqlite3_bind_int64(stmt, 3, index.lastScannedAt());

  if (sqlite3_step(stmt) != SQLITE_DONE)
  {
    sqlite3_finalize(stmt);
    return std::unexpected("Could not insert index");
  }

  const sqlite3_int64 indexId = sqlite3_last_insert_rowid(m_db);

  sqlite3_finalize(stmt);

  if (entries.empty())
    return indexId;

  exec("BEGIN TRANSACTION;");

  sqlite3_prepare_v2(
    m_db,
    "INSERT INTO entries (index_id, full_path, name, extension, is_directory, size_bytes, last_written_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
    -1,
    &stmt,
    nullptr);

  for (const auto& [path, name, extension, type,
    size, lastWrittenAt] : entries)
  {
    sqlite3_bind_int64(stmt, 1, indexId);
    sqlite3_bind_text(stmt, 2, path.string().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.string().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, extension.string().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<std::int64_t>(type)); // Cast enum to int
    sqlite3_bind_int64(stmt, 6, static_cast<std::int64_t>(size));
    sqlite3_bind_int64(stmt, 7, toUnixTime(lastWrittenAt));

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
      sqlite3_finalize(stmt);
      return std::unexpected("Could not insert entries");
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_finalize(stmt);
  exec("COMMIT;");

  return indexId;
}

std::vector<Index> Database::loadIndexes()
{
  std::vector<Index> indexes;

  sqlite3_stmt* stmt;

  sqlite3_prepare_v2(m_db, "SELECT * FROM indexes;", -1, &stmt, nullptr);

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const std::int64_t id = sqlite3_column_int64(stmt, 0);

    const unsigned char* pathText = sqlite3_column_text(stmt, 1);
    fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::int64_t createdAt = sqlite3_column_int64(stmt, 2);

    const std::int64_t lastScannedAt = sqlite3_column_int64(stmt, 3);

    indexes.emplace_back(Index{id, std::move(path), createdAt, lastScannedAt});
  }

  return indexes;
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
          "full_path TEXT NOT NULL, "
          "name TEXT NOT NULL, "
          "extension TEXT, "
          "is_directory INTEGER NOT NULL, "
          "size_bytes INTEGER, "
          "last_written_at INTEGER NOT NULL, "
          "FOREIGN KEY(index_id) REFERENCES indexes(id));";
  exec(query);
}

// Converting fs::file_time_type to fit into int 64 on sqlite3 db
std::int64_t Database::toUnixTime(const fs::file_time_type time)
{
  const auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(
    time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

  return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}
