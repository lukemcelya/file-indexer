#pragma once

#include <filesystem>

namespace fs = std::filesystem;

struct Entry
{
   enum class EntryType
   {
      FILE = 0,
      DIRECTORY = 1 // Being overly explicit for database
   };

   fs::path path;
   fs::path name;
   fs::path extension;
   const EntryType type;
   std::uintmax_t size; // In bytes
   fs::file_time_type lastWrittenAt;
};