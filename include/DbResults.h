#pragma once

#include "Entry.h"

#include <filesystem>
#include <string>
#include <ostream>

namespace db
{
  namespace fs = std::filesystem;

  struct Error
  {
    int code{};
    std::string message;

    friend std::ostream& operator<<(std::ostream& os, const Error& e)
    {
      return os << "SQLite error (" << e.code << "): " << e.message;
    }
  };

  constexpr int ERR_SIZE_OVERFLOW = -1;
  
  struct FindResult
  {
    fs::path path;
    Entry::EntryType type;
    std::uintmax_t size;
  };

  struct ShowIndexResult
  {
    std::int64_t id;
    fs::path root;
    std::int64_t createdAt;
    std::int64_t lastScannedAt;
  };
}