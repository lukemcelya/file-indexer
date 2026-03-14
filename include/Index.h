#pragma once

#include <filesystem>

namespace fs = std::filesystem;

class Index
{
private:
  std::int64_t m_id {};
  fs::path m_root;
  std::int64_t m_createdAt;
  std::int64_t m_lastScannedAt;


public:
  explicit Index(fs::path root);
  Index(std::int64_t id, fs::path root, std::int64_t createdAt, std::int64_t lastScannedAt);

  [[nodiscard]] std::int64_t id() const;
  [[nodiscard]] fs::path root() const;
  [[nodiscard]] std::int64_t createdAt() const;
  [[nodiscard]] std::int64_t lastScannedAt() const;

  void setId(std::int64_t id);

private:
  static std::int64_t nowUnix();
};
