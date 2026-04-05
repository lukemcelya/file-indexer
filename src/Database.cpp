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

Database::~Database()
{
  if (m_db)
  {
    finalizeAll();
    sqlite3_close(m_db);
  }
}

Database::Database(Database&& other) noexcept
  : m_db{other.m_db}
{
  other.m_db = nullptr;
}

auto Database::operator=(Database&& other) noexcept -> Database&
{
  if (this == &other)
    return *this;

  if (m_db)
  {
    finalizeAll();
    sqlite3_close(m_db);
  }

  m_db = other.m_db;
  other.m_db = nullptr;

  return *this;
}

auto Database::open(const fs::path& dbPath) -> std::expected<Database, db::Error>
{
  Database db;

  if (const auto parent = dbPath.parent_path(); !parent.empty())
  {
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec)
      return std::unexpected(db::Error{ec.value(), ec.message()});
  }

  const int rc = sqlite3_open(dbPath.string().c_str(), &db.m_db);
  if (rc != SQLITE_OK)
  {
    std::string msg = db.m_db ? sqlite3_errmsg(db.m_db) : "Unknown sqlite error";
    if (db.m_db)
    {
      sqlite3_close(db.m_db);
      db.m_db = nullptr;
    }

    return std::unexpected(
      db::Error{
        rc,
        std::move(msg)
      });
  }

  if (const auto init = db.initializeSchema(); !init)
  {
    sqlite3_close(db.m_db);
    db.m_db = nullptr;
    return std::unexpected(init.error());
  }

  return db;
}

auto Database::finishIndex() -> std::expected<void, db::Error>
{
  finalizeStatement(m_stmtIndexInsert);
  if (const auto commitRes = commit(); !commitRes)
    return std::unexpected(commitRes.error());

  return {};
}

auto Database::beginRescan() -> std::expected<void, db::Error>
{
  if (const auto trnsRes = beginTransaction(); !trnsRes)
    return std::unexpected(trnsRes.error());

  std::string query = "INSERT INTO entries (index_id, relative_path, name, extension, is_directory, size_bytes, last_written_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
  if (const auto prep = prepare(m_stmtEntryInsert, query.c_str()); !prep)
    return std::unexpected(prep.error());

  query = "UPDATE entries SET size_bytes = ?, last_written_at = ? WHERE index_id = ? AND relative_path = ?;";
  if (const auto prep = prepare(m_stmtEntryUpdate, query.c_str()); !prep)
    return std::unexpected(prep.error());

  query = "DELETE FROM entries WHERE index_id = ? AND relative_path = ?;";
  if (const auto prep = prepare(m_stmtEntryDelete, query.c_str()); !prep)
    return std::unexpected(prep.error());

  return {};
}

auto Database::cancelRescan() -> std::expected<void, db::Error>
{
  finalizeStatement(m_stmtEntryInsert);
  finalizeStatement(m_stmtEntryUpdate);
  finalizeStatement(m_stmtEntryDelete);
  if (const auto rbcRes = rollback(); !rbcRes)
    return std::unexpected(rbcRes.error());

  return {};
}

auto Database::finishRescan() -> std::expected<void, db::Error>
{
  finalizeStatement(m_stmtEntryInsert);
  finalizeStatement(m_stmtEntryUpdate);
  finalizeStatement(m_stmtEntryDelete);
  if (const auto commitRes = commit(); !commitRes)
    return std::unexpected(commitRes.error());

  return {};
}

auto Database::insertIndex(const Index& index) -> std::expected<std::int64_t, db::Error>
{
  if (const auto trnsRes = beginTransaction(); !trnsRes)
    return std::unexpected(trnsRes.error());

  const auto prep = prepare(m_stmtIndexInsert, "INSERT INTO indexes (root_path, created_at, last_scanned_at) VALUES (?, ?, ?);");
  if (!prep)
    return std::unexpected(prep.error());

  if (const auto res = bindText(m_stmtIndexInsert, 1, index.root().string().c_str()); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtIndexInsert, 2, index.createdAt()); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtIndexInsert, 3, index.lastScannedAt()); !res)
    return std::unexpected(res.error());

  if (sqlite3_step(m_stmtIndexInsert) != SQLITE_DONE)
  {
    db::Error error{
    sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtIndexInsert);

    /* Error causes db to rollback
     * If rollback fails, append rollback error
     * to initial failure
     */
    if (const auto rb = rollback(); !rb)
      error.message += " | Rollback also failed: " + rb.error().message;
    return std::unexpected(error);
  }

  const sqlite3_int64 indexId = sqlite3_last_insert_rowid(m_db);

  finalizeStatement(m_stmtIndexInsert);

  return indexId;
}

auto Database::beginEntryInsert() -> std::expected<void, db::Error>
{
  if (const auto trnsRes = beginTransaction(); !trnsRes)
    return std::unexpected(trnsRes.error());

  const std::string query = "INSERT INTO entries (index_id, relative_path, name, extension, is_directory, size_bytes, last_written_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
  if (const auto prep = prepare(m_stmtEntryInsert, query.c_str()); !prep)
    return std::unexpected(prep.error());

  return {};
}

auto Database::finishEntryInsert() -> std::expected<void, db::Error>
{
  finalizeStatement(m_stmtEntryInsert);
  if (const auto commitRes = commit(); !commitRes)
    return std::unexpected(commitRes.error());

  return {};
}

auto Database::insertEntry(const std::int64_t indexId, const Entry& entry) -> std::expected<void, db::Error>
{
  // index_id > relative_path > name > extension > is_directory > size_bytes > last_written_at
  if (const auto res = bindInt64(m_stmtEntryInsert, 1, indexId); !res)
    return std::unexpected(res.error());
  if (const auto res = bindText(m_stmtEntryInsert, 2, entry.path.string().c_str()); !res)
    return std::unexpected(res.error());
  if (const auto res = bindText(m_stmtEntryInsert, 3, entry.name.string().c_str()); !res)
    return std::unexpected(res.error());
  if (const auto res = bindText(m_stmtEntryInsert, 4, entry.extension.string().c_str()); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtEntryInsert, 5, static_cast<std::int64_t>(entry.type)); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtEntryInsert, 6, static_cast<std::int64_t>(entry.size)); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtEntryInsert, 7, toUnixTime(entry.lastWrittenAt)); !res)
    return std::unexpected(res.error());

  if (sqlite3_step(m_stmtEntryInsert) != SQLITE_DONE)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtEntryInsert);
    return std::unexpected(error);
  }

  sqlite3_reset(m_stmtEntryInsert);
  sqlite3_clear_bindings(m_stmtEntryInsert);

  return {};
}

auto Database::deleteEntry(const std::int64_t indexId, const Entry& entry) -> std::expected<void, db::Error>
{
  if (const auto res = bindInt64(m_stmtEntryDelete, 1, indexId); !res)
    return std::unexpected(res.error());
  if (const auto res = bindText(m_stmtEntryDelete, 2, entry.path.string().c_str()); !res)
    return std::unexpected(res.error());

  if (sqlite3_step(m_stmtEntryDelete) != SQLITE_DONE)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtEntryDelete);
    return std::unexpected(error);
  }

  sqlite3_reset(m_stmtEntryDelete);
  sqlite3_clear_bindings(m_stmtEntryDelete);

  return {};
}

auto Database::updateEntry(const std::int64_t indexId, const Entry& entry) -> std::expected<void, db::Error>
{
  // size_bytes > last_written_at > index_id > relative_path
  const auto fileSize = toSqliteFileSize(entry.size);
  if (!fileSize)
    return std::unexpected(fileSize.error());

  if (const auto res = bindInt64(m_stmtEntryUpdate, 1, *fileSize); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtEntryUpdate, 2, toUnixTime(entry.lastWrittenAt)); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtEntryUpdate, 3, indexId); !res)
    return std::unexpected(res.error());
  if (const auto res = bindText(m_stmtEntryUpdate, 4, entry.path.string().c_str()); !res)
    return std::unexpected(res.error());

  if (sqlite3_step(m_stmtEntryUpdate) != SQLITE_DONE)
  {
    return std::unexpected(db::Error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)});
  }

  sqlite3_reset(m_stmtEntryUpdate);
  sqlite3_clear_bindings(m_stmtEntryUpdate);

  return {};
}

auto Database::findEntries(const std::string& query, const std::optional<std::int64_t> indexId) -> std::expected<std::vector<db::FindResult>, db::Error>
{
  if (const auto prep = prepareEntrySearch(query, indexId); !prep)
    return std::unexpected(prep.error());

  std::vector<db::FindResult> result{};
  int rc{};

  while ((rc = sqlite3_step(m_stmtEntrySearch)) == SQLITE_ROW)
  {
    const unsigned char* pathText = sqlite3_column_text(m_stmtEntrySearch, 0);
    const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::int64_t type = sqlite3_column_int64(m_stmtEntrySearch, 1) == 0 ? 0 : 1;

    const std::uintmax_t size = sqlite3_column_int64(m_stmtEntrySearch, 2);

    result.emplace_back(db::FindResult{
      path,
      type == 0 ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY,
      size
      });
  }

  if (rc != SQLITE_DONE)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtEntrySearch);
    return std::unexpected(error);
  }

  return result;
}

auto Database::prepareEntrySearch(const std::string& query, const std::optional<std::int64_t> indexId) -> std::expected<void, db::Error>
{
  if (indexId.has_value())
  {
    const auto prep = prepare(m_stmtEntrySearch, "SELECT relative_path, is_directory, size_bytes FROM entries WHERE index_id = ? AND (relative_path LIKE ? OR name LIKE ? OR extension LIKE?);");
    if (!prep)
      return std::unexpected(prep.error());

    const std::string newQuery = "%" + query + "%";

    if (const auto res = bindInt64(m_stmtEntrySearch, 1, *indexId); !res)
      return std::unexpected(res.error());
    if (const auto res = bindText(m_stmtEntrySearch, 2, newQuery.c_str()); !res)
      return std::unexpected(res.error());
    if (const auto res = bindText(m_stmtEntrySearch, 3, newQuery.c_str()); !res)
      return std::unexpected(res.error());
    if (const auto res = bindText(m_stmtEntrySearch, 4, newQuery.c_str()); !res)
      return std::unexpected(res.error());
  }
  else
  {
    const auto prep = prepare(m_stmtEntrySearch, "SELECT relative_path, is_directory, size_bytes FROM entries WHERE relative_path LIKE ? OR name LIKE ? OR extension LIKE?;");
    if (!prep)
      return std::unexpected(prep.error());

    const std::string newQuery = "%" + query + "%";

    if (const auto res = bindText(m_stmtEntrySearch, 1, newQuery.c_str()); !res)
      return std::unexpected(res.error());
    if (const auto res = bindText(m_stmtEntrySearch, 2, newQuery.c_str()); !res)
      return std::unexpected(res.error());
    if (const auto res = bindText(m_stmtEntrySearch, 3, newQuery.c_str()); !res)
      return std::unexpected(res.error());
  }

  return {};
}

void Database::finishFind()
{
  finalizeStatement(m_stmtEntrySearch);
}

auto Database::showIndex(const std::int64_t indexId) -> std::expected<db::ShowIndexResult, db::Error>
{
  if (const auto prep = prepareIndexShow(indexId); !prep)
    return std::unexpected(prep.error());

  if (sqlite3_step(m_stmtIndexShow) != SQLITE_ROW)
  {
    db::Error error{
      sqlite3_errcode(m_db),
    sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtIndexShow);
    return std::unexpected(error);
  }

  const std::int64_t id = sqlite3_column_int64(m_stmtIndexShow, 0);

  const unsigned char* pathText = sqlite3_column_text(m_stmtIndexShow, 1);
  const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

  const std::int64_t createdAt = sqlite3_column_int64(m_stmtIndexShow, 2);

  const std::int64_t lastScannedAt = sqlite3_column_int64(m_stmtIndexShow, 3);

  finalizeStatement(m_stmtIndexShow);

  return db::ShowIndexResult{id, path, createdAt, lastScannedAt};
}

auto Database::prepareIndexShow(const std::int64_t indexId) -> std::expected<void, db::Error>
{
  if (const auto prep = prepare(m_stmtIndexShow, "SELECT * FROM indexes WHERE id = ?"); !prep)
    return std::unexpected(prep.error());

  if (const auto res = bindInt64(m_stmtIndexShow, 1, indexId); !res)
    return std::unexpected(res.error());

  return {};
}

auto Database::findPotentialDuplicates(const std::int64_t indexId) -> std::expected<std::vector<fs::path>, db::Error>
{
  const auto rootResult = indexPath(indexId);
  if (!rootResult)
    return std::unexpected(rootResult.error());

  if (const auto prep = prepareDuplicateSearch(indexId); !prep)
    return std::unexpected(prep.error());

  std::vector<fs::path> paths;
  int rc{};
  while ((rc = sqlite3_step(m_stmtDuplicateSearch)) == SQLITE_ROW)
  {
    const unsigned char* pathText = sqlite3_column_text(m_stmtDuplicateSearch, 0);
    const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    paths.emplace_back(*rootResult / path);
  }

  if (rc != SQLITE_DONE)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtDuplicateSearch);
    return std::unexpected(error);
  }

  finalizeStatement(m_stmtDuplicateSearch);
  return paths;
}

auto Database::prepareDuplicateSearch(const std::int64_t indexId) -> std::expected<void, db::Error>
{
  const std::string query = "SELECT relative_path "
                      "FROM entries "
                      "WHERE index_id = ? "
                      "  AND is_directory = 0 "
                      "  AND size_bytes IN ("
                      "    SELECT size_bytes "
                      "    FROM entries "
                      "    WHERE index_id = ? "
                      "      AND is_directory = 0 "
                      "    GROUP BY size_bytes "
                      "    HAVING COUNT(*) > 1 "
                      ") "
                      "ORDER BY size_bytes;";

  if (const auto prep = prepare(m_stmtDuplicateSearch, query.c_str()); !prep)
    return std::unexpected(prep.error());

  if (const auto res = bindInt64(m_stmtDuplicateSearch, 1, indexId); !res)
    return std::unexpected(res.error());
  if (const auto res = bindInt64(m_stmtDuplicateSearch, 2, indexId); !res)
    return std::unexpected(res.error());

  return {};
}


auto Database::loadIndexes() -> std::expected<std::vector<Index>, db::Error>
{
  std::vector<Index> indexes;

  if (const auto prep = prepare(m_stmtIndexLoad, "SELECT * FROM indexes;"); !prep)
    return std::unexpected(prep.error());

  int rc{};
  while ((rc = sqlite3_step(m_stmtIndexLoad)) == SQLITE_ROW)
  {
    const std::int64_t id = sqlite3_column_int64(m_stmtIndexLoad, 0);

    const unsigned char* pathText = sqlite3_column_text(m_stmtIndexLoad, 1);
    fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::int64_t createdAt = sqlite3_column_int64(m_stmtIndexLoad, 2);

    const std::int64_t lastScannedAt = sqlite3_column_int64(m_stmtIndexLoad, 3);

    indexes.emplace_back(id, std::move(path), createdAt, lastScannedAt);
  }

  if (rc != SQLITE_DONE)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtIndexLoad);
    return std::unexpected(error);
  }

  finalizeStatement(m_stmtIndexLoad);
  return indexes;
}

auto Database::loadEntriesFromIndex(const std::int64_t indexId) -> std::expected<std::unordered_map<std::string, Entry>, db::Error>
{
  std::unordered_map<std::string, Entry> entries{};
  const auto prep = prepare(m_stmtEntryLoad, "SELECT relative_path, name, extension, is_directory, size_bytes, last_written_at FROM entries WHERE index_id = ?;");
  if (!prep)
    return std::unexpected(prep.error());

  if (const auto res = bindInt64(m_stmtEntryLoad, 1, indexId); !res)
    return std::unexpected(res.error());

  int rc{};
  while ((rc = sqlite3_step(m_stmtEntryLoad)) == SQLITE_ROW)
  {
    const unsigned char* pathText = sqlite3_column_text(m_stmtEntryLoad, 0);
    const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

    const std::string pathKey = path.string();

    const unsigned char* nameText = sqlite3_column_text(m_stmtEntryLoad, 1);
    const fs::path name = nameText ? fs::path(reinterpret_cast<const char*>(nameText)) : fs::path{};

    const unsigned char* extText = sqlite3_column_text(m_stmtEntryLoad, 2);
    const fs::path ext = extText ? fs::path(reinterpret_cast<const char*>(extText)) : fs::path{};

    const std::int64_t type = sqlite3_column_int64(m_stmtEntryLoad, 3) == 0 ? 0 : 1; // 0 is FILE type, anything else is DIRECTORY

    const std::uintmax_t size = sqlite3_column_int64(m_stmtEntryLoad, 4);

    const fs::file_time_type lastWritten = toFileTime(sqlite3_column_int64(m_stmtEntryLoad, 5));

    entries.try_emplace(pathKey, Entry{
      path,
      name,
      ext,
      type == 0 ? Entry::EntryType::FILE : Entry::EntryType::DIRECTORY,
      size,
      lastWritten
    });
  }

  if (rc != SQLITE_DONE)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtEntryLoad);
    return std::unexpected(error);
  }

  finalizeStatement(m_stmtEntryLoad);
  return entries;
}

auto Database::indexPath(const std::int64_t indexId) -> std::expected<fs::path, db::Error>
{
  if (const auto prep = prepareIndexPath(indexId); !prep)
    return std::unexpected(prep.error());

  if (sqlite3_step(m_stmtIndexPath) != SQLITE_ROW)
  {
    db::Error error{
      sqlite3_errcode(m_db),
      sqlite3_errmsg(m_db)
    };

    finalizeStatement(m_stmtIndexPath);
    return std::unexpected(error);
  }

  const unsigned char* pathText = sqlite3_column_text(m_stmtIndexPath, 0);
  const fs::path path = pathText ? fs::path(reinterpret_cast<const char*>(pathText)) : fs::path{};

  finalizeStatement(m_stmtIndexPath);

  return path;
}

auto Database::prepareIndexPath(const std::int64_t indexId) -> std::expected<void, db::Error>
{
  if (const auto prep = prepare(m_stmtIndexPath, "SELECT root_path FROM indexes WHERE id = ?"); !prep)
    return std::unexpected(prep.error());

  if (const auto res = bindInt64(m_stmtIndexPath, 1, indexId); !res)
    return std::unexpected(res.error());

  return {};
}

auto Database::initializeSchema() -> std::expected<void, db::Error>
{
  std::string query = "CREATE TABLE IF NOT EXISTS indexes( "
                      "id INTEGER PRIMARY KEY, "
                      "root_path TEXT NOT NULL, "
                      "created_at INTEGER NOT NULL, "
                      "last_scanned_at INTEGER NOT NULL);";
  if (const auto prep = exec(query); !prep)
    return std::unexpected(prep.error());

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
  if (const auto prep = exec(query); !prep)
    return std::unexpected(prep.error());

  return {};
}

auto Database::exec(const std::string_view query) -> std::expected<void, db::Error>
{
  char* errMsg;

  int rc = sqlite3_exec(m_db, query.data(), nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK)
  {
    const std::string errMsgStr = errMsg ? errMsg : sqlite3_errmsg(m_db); // So errMsg can be freed before exception
    sqlite3_free(errMsg);
    return std::unexpected(db::Error{
      sqlite3_errcode(m_db),
      errMsgStr
    });
  }

  return {};
}

auto Database::prepare(sqlite3_stmt*& stmt, const char* sql) -> std::expected<void, db::Error>
{
  if (const int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr); rc != SQLITE_OK)
  {
    return std::unexpected(db::Error{rc, sqlite3_errmsg(m_db)});
  }

  return {};
}

auto Database::beginTransaction() -> std::expected<void, db::Error>
{
  if (const auto res = exec("BEGIN TRANSACTION;"); !res)
    return std::unexpected(res.error());

  return {};
}

auto Database::rollback() -> std::expected<void, db::Error>
{
  if (const auto res = exec("ROLLBACK;"); !res)
    return std::unexpected(res.error());

  return {};
}

auto Database::commit() -> std::expected<void, db::Error>
{
  if (const auto res = exec("COMMIT;"); !res)
    return std::unexpected(res.error());

  return {};
}

auto Database::bindInt64(sqlite3_stmt* stmt, const int index, const std::int64_t value) const -> std::expected<void, db::Error>
{
  if (const int rc = sqlite3_bind_int64(stmt, index, value); rc != SQLITE_OK)
    return std::unexpected(makeError(rc));

  return {};
}

auto Database::bindText(sqlite3_stmt* stmt, const int index, const std::string_view value) const -> std::expected<void, db::Error>
{
  if (const int rc = sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT); rc != SQLITE_OK)
    return std::unexpected(makeError(rc));

  return {};
}

db::Error Database::makeError(const int rc) const
{
  return db::Error{rc, sqlite3_errmsg(m_db)};
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

auto Database::toSqliteFileSize(const std::uintmax_t size) -> std::expected<std::int64_t, db::Error>
{
  if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max()))
    return std::unexpected(
      db::Error{
        db::ERR_SIZE_OVERFLOW,
        "File exceeds sqlite3_int64 limit"
      });

  return static_cast<std::int64_t>(size);
}

void Database::finalizeAll()
{
  finalizeStatement(m_stmtIndexPath);
  finalizeStatement(m_stmtIndexInsert);
  finalizeStatement(m_stmtIndexShow);
  finalizeStatement(m_stmtIndexLoad);
  finalizeStatement(m_stmtEntryInsert);
  finalizeStatement(m_stmtEntryDelete);
  finalizeStatement(m_stmtEntryUpdate);
  finalizeStatement(m_stmtEntrySearch);
  finalizeStatement(m_stmtEntryLoad);
  finalizeStatement(m_stmtDuplicateSearch);
}

void Database::finalizeStatement(sqlite3_stmt*& stmt)
{
  if (stmt != nullptr)
  {
    sqlite3_finalize(stmt);
    stmt = nullptr;
  }
}
