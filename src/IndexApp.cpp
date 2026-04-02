#include "IndexApp.h"
#include "Index.h"
#include "Cli.h"
#include "Scanner.h"

#include <filesystem>
#include <string>
#include <algorithm>

#include "Duplicates.h"

namespace fs = std::filesystem;

IndexApp::IndexApp(const std::string& dbPath)
: m_database { dbPath }
, m_indexStore { m_database.loadIndexes() }
{ }


bool IndexApp::createIndex(const fs::path& path)
{
  const fs::path indexPath = normalizePath(path);
  if (!fs::is_directory(indexPath) || isIndexed(indexPath))
    return false;

  Index index { indexPath };
  auto indexId = m_database.insertIndex(index);

  if (!indexId)
  {
    Cli::printDbError(indexId.error());
    return false;
  }
  index.setId(indexId.value());

  m_database.finishIndex();
  m_database.beginEntryInsert();

  auto entryResult = scanner::scan(path, [&](const Entry& entry)
  {
    return m_database.insertEntry(index.id(), entry);
  }); // Insert entry as callback

  if (!entryResult)
  {
    Cli::printDbError(entryResult.error());
    return false;
  }

  m_database.finishEntryInsert();

  m_indexStore.push_back(std::move(index));
  return true;
}

auto IndexApp::rescanIndex(const fs::path& path) -> std::expected<RescanStats, std::string>
{
  const fs::path indexPath = normalizePath(path);
  if (!fs::is_directory(indexPath) || !isIndexed(indexPath))
    return std::unexpected("Directory not found");

  const auto currentIndex = std::ranges::find_if(m_indexStore, [&](const Index& index)
  {
    return index.root() == indexPath;
  });

  if (currentIndex == m_indexStore.end())
    return std::unexpected("Directory not indexed");

  auto existing = m_database.loadEntriesFromIndex(currentIndex->id());
  RescanStats stats{};

  m_database.beginRescan();

  auto scanResult = scanner::scan(path, [&](const Entry& entry) -> std::expected<void, db::Error>
  {
    const auto it = existing.find(entry.path.string());

    if (it == existing.end())
    {
      m_database.prepareEntryInsert();
      if (auto r = m_database.insertEntry(currentIndex->id(), entry); !r)
        return std::unexpected(r.error());

      ++stats.added;
      return {};
    }

    if (isEntryChanged(it->second, entry))
    {
      m_database.prepareEntryUpdate();
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
    Cli::printDbError(scanResult.error());
    return std::unexpected("Index not found");
  }

  for (const auto& [relativePath, oldEntry] : existing)
  {
    m_database.prepareEntryDelete();
    if (auto r = m_database.deleteEntry(currentIndex->id(), oldEntry); !r)
    {
      Cli::printDbError(r.error());
      return std::unexpected("Deletion not completed");
    }

    ++stats.deleted;
  }

  m_database.finishRescan();

  return stats;
}

std::vector<db::FindResult> IndexApp::findAllEntries(const std::string& query)
{
  const auto result = m_database.findEntries(query);
  if (!result)
  {
    Cli::printDbError(result.error());
    return {};
  }

  m_database.finishFind();
  return *result;
}

std::expected<db::ShowIndexResult, std::string> IndexApp::showIndex(const std::int64_t id)
{
  if (!isIndexed(id))
    return std::unexpected("Index not found");

  auto result = m_database.showIndex(id);
  if (!result)
    return std::unexpected(result.error().message);

  return *result;
}

std::expected<std::vector<dup::DuplicateGroup>, std::string> IndexApp::findDuplicates(const std::int64_t id)
{
  if (!isIndexed(id))
    return std::unexpected("Index not found");

  const auto entries = m_database.findPotentialDuplicates(id);
  if (!entries)
    return std::unexpected(entries.error().message);

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
