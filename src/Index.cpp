#include "Index.h"
#include "Entry.h"

#include <chrono>

Index::Index(fs::path root)
  : m_root { std::move(root) }
  , m_createdAt { nowUnix() }
  , m_lastScannedAt { nowUnix() }
{ }

Index::Index(const std::int64_t id, fs::path root, const std::int64_t createdAt, const std::int64_t lastScannedAt)
  : m_id { id }
  , m_root { std::move(root) }
  , m_createdAt { createdAt }
  , m_lastScannedAt { lastScannedAt }
{ }

std::int64_t Index::id() const
{
  return m_id;
}

fs::path Index::root() const
{
  return m_root;
}

std::int64_t Index::createdAt() const
{
  return m_createdAt;
}

std::int64_t Index::lastScannedAt() const
{
  return m_lastScannedAt;
}


void Index::setId(std::int64_t id)
{
  m_id = id;
}

std::int64_t Index::nowUnix()
{
  return std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
