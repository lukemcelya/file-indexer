#include "IndexApp.h"
#include "Index.h"
#include "Cli.h"
#include "Scanner.h"

#include <filesystem>
#include <string>
#include <chrono>

namespace fs = std::filesystem;

IndexApp::IndexApp(const std::string& dbPath)
: m_database { dbPath }
, m_indexStore { m_database.loadIndexes() }
{ }

bool IndexApp::isIndexed(const fs::path& path) const
{
  // False is already exists in m_indexStore
  if (std::ranges::any_of(m_indexStore, [&](const Index& index)
  {
    return index.root() == path;
  })) { return true; }

  return false;
}

bool IndexApp::createIndex(const fs::path& path)
{
  const fs::path indexPath = normalizePath(path);
  if (!fs::is_directory(indexPath) || isIndexed(indexPath))
    return false;

  m_database.beginTransaction();

  Index index { indexPath };
  auto indexId = m_database.insertIndex(index);

  if (!indexId)
  {
    Cli::printError(indexId.error());
    return false;
  }
  index.setId(indexId.value());

  m_database.prepareEntryInsert();

  auto entryResult = scanner::scan(path, [&](const Entry& entry)
  {
    return m_database.insertEntry(index.id(), entry);
  }); // Insert entry as callback

  if (!entryResult)
  {
    Cli::printError(entryResult.error());
    return false;
  }

  m_database.finalizeEntryInsert();
  m_database.commit();

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

  std::unordered_map<std::string, Entry> existing = m_database.loadEntriesFromIndex(currentIndex->id());
  RescanStats stats{};

  m_database.beginTransaction();

  auto scanResult = scanner::scan(path, [&](const Entry& entry) -> std::expected<void, Database::Error>
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
    Cli::printError(scanResult.error());
    return std::unexpected("Index not found");
  }

  for (const auto& [relativePath, oldEntry] : existing)
  {
    m_database.prepareEntryDelete();
    if (auto r = m_database.deleteEntry(currentIndex->id(), oldEntry); !r)
    {
      Cli::printError(r.error());
      return std::unexpected("Deletion not completed");
    }

    ++stats.deleted;
  }

  m_database.finalizeEntryInsert();
  m_database.finalizeEntryDelete();
  m_database.finalizeEntryUpdate();
  m_database.commit();

  return stats;
}

std::vector<Database::FindResult> IndexApp::findAllEntries(const std::string& query)
{
  const auto result = m_database.findEntries(query);
  if (!result)
  {
    Cli::printError(result.error());
    return {};
  }

  return *result;
}

bool IndexApp::isEntryChanged(const Entry& oldEntry, const Entry& newEntry)
{
  return oldEntry.size != newEntry.size;
}

fs::path IndexApp::normalizePath(const fs::path& path)
{
  return fs::canonical(fs::absolute(path));
}
