#include "IndexApp.h"
#include "Index.h"
#include "Cli.h"
#include "Scanner.h"
#include "Duplicates.h"
#include "AppResults.h"

#include <filesystem>
#include <string>
#include <algorithm>
#include <ranges>


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
  const auto indexPath = normalizePath(path);
  if (!indexPath)
    return std::unexpected(indexPath.error());

  if (!fs::is_directory(*indexPath))
    return std::unexpected(
      app::Error{
        app::Error::Type::InvalidPath,
        "Invalid path (path is not a directory)",
      });

  if (isIndexed(*indexPath))
    return std::unexpected(
      app::Error{
        app::Error::Type::AlreadyIndexed,
        "Directory already indexed"
      });

  if (const auto res = m_database.beginTransaction(); !res)
    return databaseFailure("Begin transaction failed", res.error(), false);

  Index index { *indexPath };
  auto indexId = m_database.insertIndex(index);

  if (!indexId)
    return databaseFailure("Index insert failed", indexId.error(), true);

  index.setId(*indexId);

  if (const auto begin = m_database.prepareEntryInsert(); !begin)
    return databaseFailure("Prepare entry insert failed", begin.error(), true);

  auto entryResult = scanner::scan(*indexPath, [&](const Entry& entry)
  {
    return m_database.insertEntry(index.id(), entry);
  });

  if (!entryResult)
  {
    m_database.finalizeEntryInsert();
    return databaseFailure("Entry insert failed", entryResult.error(), true);
  }

  m_database.finalizeEntryInsert();
  if (const auto res = m_database.commit(); !res)
    return databaseFailure("Commit failed", res.error(), true);

  m_indexStore.push_back(std::move(index));
  return *indexId;
}

auto IndexApp::rescanIndex(const fs::path& path) -> std::expected<RescanStats, app::Error>
{
  const auto indexPath = normalizePath(path);
  if (!indexPath)
    return std::unexpected(indexPath.error());

  if (!fs::is_directory(*indexPath))
    return std::unexpected(
      app::Error{
        app::Error::Type::InvalidPath,
        "Invalid path (path is not a directory)"
      });

  const auto currentIndex = std::ranges::find_if(m_indexStore, [&](const Index& index)
  {
    return index.root() == *indexPath;
  });

  if (currentIndex == m_indexStore.end())
    return std::unexpected(
      app::Error{
        app::Error::Type::NotIndexed,
        "Directory not indexed"
      });

  const auto indexId = currentIndex->id();

  auto result = m_database.loadEntriesFromIndex(indexId);
  if (!result)
    return databaseFailure("Entry loading failed", result.error(), false);

  auto& existing = result.value();
  RescanStats stats{};

  if (const auto res = m_database.beginTransaction(); !res)
    return databaseFailure("Begin transaction failed", res.error(), false);

  if (const auto rescanRes = m_database.prepareRescan(); !rescanRes)
    return databaseFailure("Prepare rescan failed", rescanRes.error(), true);


  auto scanResult = scanner::scan(*indexPath, [&](const Entry& entry) -> std::expected<void, db::Error>
  {
    auto it = existing.find(entry.path.string());

    if (it == existing.end())
    {
      if (auto r = m_database.insertEntry(indexId, entry); !r)
        return std::unexpected(r.error());

      ++stats.added;
      return {};
    }

    if (isEntryChanged(it->second, entry))
    {
      if (auto r = m_database.updateEntry(indexId, entry); !r)
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
    m_database.finalizeRescan();
    return databaseFailure("Rescan failed", scanResult.error(), true);
  }

  for (const auto &oldEntry: existing | std::views::values)
  {
    if (auto deleteRes = m_database.deleteEntry(indexId, oldEntry); !deleteRes)
    {
      m_database.finalizeRescan();
      return databaseFailure("Delete failed", deleteRes.error(), true);
    }
    ++stats.deleted;
  }

  m_database.finalizeRescan();
  if (const auto commit = m_database.commit(); !commit)
    return databaseFailure("Commit failed", commit.error(), true);

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

auto IndexApp::indexStats(const std::int64_t id) -> std::expected<db::IndexStatsResult, app::Error>
{
  if (!isIndexed(id))
  {
    return std::unexpected(
      app::Error{
        app::Error::Type::NotIndexed,
        "Directory not indexed"
      });
  }

  const auto stats = m_database.getIndexStats(id);
  if (!stats)
  {
    return std::unexpected(
      app::Error{
        app::Error::Type::Database,
        "Get index stats failed",
        stats.error()
      });
  }

  return *stats;
}

bool IndexApp::isIndexed(const fs::path& path) const
{
  return std::ranges::any_of(m_indexStore, [&](const Index& index)
  {
    return index.root() == path;
  });
}

bool IndexApp::isIndexed(const std::int64_t id) const
{
  return std::ranges::any_of(m_indexStore, [&](const Index& index)
  {
    return index.id() == id;
  });
}

auto IndexApp::databaseFailure(const std::string& message, db::Error error, const bool rollbackNeeded) -> std::unexpected<app::Error>
{
  if (rollbackNeeded)
  {
    if (const auto rb = m_database.rollback(); !rb)
      error.message += " | Rollback also failed: " + rb.error().message;
  }

  return std::unexpected(
    app::Error{
      app::Error::Type::Database,
      message,
      std::move(error)
    });
}

bool IndexApp::isEntryChanged(const Entry& oldEntry, const Entry& newEntry)
{
  return oldEntry.size != newEntry.size || oldEntry.lastWrittenAt != newEntry.lastWrittenAt;
}

auto IndexApp::normalizePath(const fs::path& path) -> std::expected<fs::path, app::Error>
{
  try
  {
    if (!fs::exists(path))
      return std::unexpected(app::Error{app::Error::Type::InvalidPath, "Invalid directory path"});

    return fs::canonical(fs::absolute(path));
  }
  catch (const fs::filesystem_error& e)
  {
    return std::unexpected(app::Error{app::Error::Type::InvalidPath, e.what()});
  }
}
