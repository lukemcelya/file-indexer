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

bool IndexApp::isValidPath(const fs::path& path) const
{
  // False if not directory
  if (!fs::is_directory(path))
    return false;

  // False is already exists in m_indexStore
  if (std::ranges::any_of(m_indexStore, [&](const Index& index)
  {
    return index.root() == path;
  })) { return false; }

  return true;
}

bool IndexApp::createIndex(const fs::path& path)
{
  const fs::path indexPath = normalizePath(path);
  if (!isValidPath(indexPath))
    return false;

  Index index { indexPath };

  const std::vector<Entry> entries = scanner::scan(path);

  auto indexId = m_database.saveIndex(index, entries);

  if (!indexId)
  {
    Cli::printError(indexId.error());
    return false;
  }

  index.setId(indexId.value());

  m_indexStore.push_back(std::move(index));
  return true;
}

fs::path IndexApp::normalizePath(const fs::path& path)
{
  return fs::canonical(fs::absolute(path));
}
