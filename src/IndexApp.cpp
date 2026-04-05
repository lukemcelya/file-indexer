#include "IndexApp.h"
#include "Index.h"
#include "Cli.h"
#include "Scanner.h"
#include "Duplicates.h"
#include "AppResults.h"

#include <filesystem>
#include <string>
#include <algorithm>


namespace fs = std::filesystem;

IndexApp::IndexApp(Database db)
: m_database { std::move(db) }
{ }

auto IndexApp::loadIndexStore() -> std::expected<void, app::Error>
{
  auto loadResult = m_database.loadIndexes();
  if (!loadResult)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Index store loading failed",
        loadResult.error()
      });

  m_indexStore = std::move(loadResult.value());
  return {};
}

auto IndexApp::createIndex(const fs::path& path) -> std::expected<std::int64_t, app::Error>
{
  const fs::path indexPath = normalizePath(path);
  if (!fs::is_directory(indexPath))
    return std::unexpected(
      app::Error{
        app::Error::Type::InvalidPath,
        "Invalid path (path is not a directory)",
      });
  if (isIndexed(indexPath))
    return std::unexpected(
      app::Error{
        app::Error::Type::AlreadyIndexed,
        "Directory already indexed"
      });

  Index index { indexPath };
  auto indexId = m_database.insertIndex(index);
  if (!indexId)
  {
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Index insert failed",
        indexId.error()
      });
  }
  index.setId(*indexId);

  if (const auto finish = m_database.finishIndex(); !finish)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Finalize index failed",
        finish.error()
      });

  if (const auto begin = m_database.beginEntryInsert(); !begin)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Initialize entry insert failed",
        begin.error()
      });

  auto entryResult = scanner::scan(path, [&](const Entry& entry)
  {
    return m_database.insertEntry(index.id(), entry);
  }); // Insert entry as callback

  if (!entryResult)
  {
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Entry insert failed",
        entryResult.error()
      });
  }

  if (const auto finish = m_database.finishEntryInsert(); !finish)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Finalize entry insert failed",
        finish.error()
      });

  m_indexStore.push_back(std::move(index));
  return *indexId;
}

auto IndexApp::rescanIndex(const fs::path& path) -> std::expected<RescanStats, app::Error>
{
  const fs::path indexPath = normalizePath(path);
  if (!fs::is_directory(indexPath))
    return std::unexpected(
      app::Error{
        app::Error::Type::InvalidPath,
        "Invalid path (path is not a directory)"
      });

  const auto currentIndex = std::ranges::find_if(m_indexStore, [&](const Index& index)
  {
    return index.root() == indexPath;
  });

  if (currentIndex == m_indexStore.end())
    return std::unexpected(
      app::Error{
        app::Error::Type::NotIndexed,
        "Directory not indexed"
      });

  auto result = m_database.loadEntriesFromIndex(currentIndex->id());
  if (!result)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Entry loading failed",
        result.error()
      });

  auto& existing = result.value();
  RescanStats stats{};

  if (const auto rescanRes = m_database.beginRescan(); !rescanRes)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Rescan failed",
        rescanRes.error()
      });

  auto scanResult = scanner::scan(path, [&](const Entry& entry) -> std::expected<void, db::Error>
  {
    auto it = existing.find(entry.path.string());

    if (it == existing.end())
    {
      if (auto r = m_database.insertEntry(currentIndex->id(), entry); !r)
        return std::unexpected(r.error());

      ++stats.added;
      return {};
    }

    if (isEntryChanged(it->second, entry))
    {
      if (auto r = m_database.updateEntry(currentIndex->id(), entry); !r)
        return std::unexpected(r.error());

      ++stats.modified;
    }
    else
    {
      ++stats.unmodified;
    }

    existing.erase(it);
    return {};
  });

  if (!scanResult)
  {
    db::Error error{scanResult.error()};

    if (const auto cancelRes = m_database.cancelRescan(); !cancelRes)
      error.message += " | Cancel error: " + cancelRes.error().message;

    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Rescan failed",
        error
      });
  }

  for (const auto& [relativePath, oldEntry] : existing)
  {
    if (auto deleteRes = m_database.deleteEntry(currentIndex->id(), oldEntry); !deleteRes)
    {
      db::Error error{deleteRes.error()};

      if (const auto cancelRes = m_database.cancelRescan(); !cancelRes)
        error.message += " | Cancel error: " + cancelRes.error().message;

      return std::unexpected(
        app::Error{
          app::Error::Type::Database,
          "Delete failed",
          error
        });
    }

    ++stats.deleted;
  }

  if (const auto finish = m_database.finishRescan(); !finish)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Rescan finalize failed",
        finish.error()
      });

  return stats;
}

auto IndexApp::findAllEntries(const std::string& query) -> std::expected<std::vector<db::FindResult>, app::Error>
{
  const auto result = m_database.findEntries(query);
  if (!result)
  {
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Find entries failed",
        result.error()
      });
  }

  m_database.finishFind();
  return *result;
}

auto IndexApp::showIndex(const std::int64_t id) -> std::expected<db::ShowIndexResult, app::Error>
{
  if (!isIndexed(id))
    return std::unexpected(
      app::Error{
        app::Error::Type::NotIndexed,
        "Directory not indexed"
      });

  const auto result = m_database.showIndex(id);
  if (!result)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Show index failed",
        result.error()
      });

  return *result;
}

auto IndexApp::findDuplicates(const std::int64_t id) -> std::expected<std::vector<dup::DuplicateGroup>, app::Error>
{
  if (!isIndexed(id))
    return std::unexpected(
      app::Error{
        app::Error::Type::NotIndexed,
        "Directory not indexed"
      });

  const auto entries = m_database.findPotentialDuplicates(id);
  if (!entries)
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Find duplicates failed",
        entries.error()
      });

  return dup::find(*entries);
}

bool IndexApp::isIndexed(const fs::path& path) const
{
  // True if already exists in m_indexStore
  if (std::ranges::any_of(m_indexStore, [&](const Index& index)
  {
    return index.root() == path;
  })) { return true; }

  return false;
}

bool IndexApp::isIndexed(const std::int64_t id) const
{
  if (std::ranges::any_of(m_indexStore, [&](const Index& index)
  {
    return index.id() == id;
  })) { return true; }

  return false;
}

bool IndexApp::isEntryChanged(const Entry& oldEntry, const Entry& newEntry)
{
  return oldEntry.size != newEntry.size;
}

fs::path IndexApp::normalizePath(const fs::path& path)
{
  return fs::canonical(fs::absolute(path));
}
