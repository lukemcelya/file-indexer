#include "Cli.h"

#include <charconv>

#include "IndexApp.h"
#include "Database.h"
#include "AppResults.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <sstream>
#include <time.h>

Cli::Cli(IndexApp& indexApp)
  : m_indexApp { indexApp }
{ }

int Cli::run(const int argc, const char* argv[])
{
  if (argc == 1)
  {
    repl();
    return 0;
  }

  std::vector<std::string> args;
  for (std::size_t i{1}; i < argc; ++i)
    args.emplace_back(argv[i]);

  return handleCommand(args, false);
}

int Cli::handleCommand(const std::vector<std::string>& args, const bool isRepl)
{
  if (args.empty())
  {
    std::cout << "No command given\n";
    return 1;
  }

  const std::string& command = args[0];

  if (command == "index")
    return handleIndex(args[1]);
  if (command == "rescan")
    return handleRescan(args[1]);
  if (command == "find")
    return handleFind(args);
  if (command == "duplicates")
    return handleDuplicate(args);
  if (command == "stats")
    return handleStats(args);
  if (command == "show")
    return handleShow(args);

  printUsage();
  return 1;
}

void Cli::repl()
{
  while (true)
  {
    std::cout << "> ";
    std::string input;
    if (!std::getline(std::cin, input))
      break;

    if (input == "quit" || input == "exit") // Maybe add more exit commands
      break;

    std::vector<std::string> args = tokenize(input);

    if (args.empty())
      continue;

    handleCommand(args, true);
  }
}

int Cli::handleIndex(const std::string_view dir)
{
  const auto res = m_indexApp.createIndex(dir);
  if (!res)
  {
    printError(res.error());
    return 1;
  }

  std::cout << "Index (" << *res << ") " << dir << " created\n";
  return 0;
}

int Cli::handleRescan(const std::string_view dir)
{
  const auto result = m_indexApp.rescanIndex(dir);
  if (!result)
  {
    printError(result.error());
    return 1;
  }

  std::cout << "Rescan stats:\n"
            << "Added: " << result.value().added << "\n"
            << "Deleted: " << result.value().deleted << "\n"
            << "Modified: " << result.value().modified << "\n"
            << "Unmodified: " << result.value().unmodified << "\n";
  return 0;
}

int Cli::handleFind(const std::vector<std::string>& args) const
{
  const auto result = m_indexApp.findAllEntries(args[1]);
  if (!result)
  {
    printError(result.error());
    return 1;
  }

  printFindResults(*result);
  return 0;
}

int Cli::handleDuplicate(const std::vector<std::string>& args)
{
  const auto indexId = parseIndexFlag(args);
  if (!indexId)
  {
    std::cout << "No valid index\n";
    return 1;
  }

  auto result = m_indexApp.findDuplicates(*indexId);
  if (!result)
  {
    printError(result.error());
    return 1;
  }

  printDuplicates(result.value());
  return 0;
}

int Cli::handleStats(const std::vector<std::string>& args)
{
  const auto indexId = parseIndexFlag(args);
  if (!indexId)
  {
    std::cout << "No valid index\n";
    return 1;
  }

  const auto result = m_indexApp.indexStats(*indexId);
  if (!result)
  {
    printError(result.error());
    return 1;
  }

  printIndexStats(*result);
  return 0;
}

int Cli::handleShow(const std::vector<std::string>& args) const
{
  const auto indexId = parseIndexFlag(args);
  if (!indexId)
  {
    std::cout << "No valid index\n";
    return 1;
  }

  auto result = m_indexApp.showIndex(*indexId);
  if (!result)
  {
    printError(result.error());
    return 1;
  }

  printShowIndex(*result);

  return 0;
}

std::vector<std::string> Cli::tokenize(const std::string& input)
{
  std::istringstream ss(input);
  std::vector<std::string> tokens;
  std::string word;

  while (ss >> word)
    tokens.emplace_back(word);

  return tokens;
}

void Cli::printUsage()
{
  std::cout << "Usage:\n"
            << "    index                   <directory>\n"
            << "    rescan                  <directory>\n"
            << "    show                    <directory>\n"
            << "    find                    <query>\n"
            << "    duplicates --index      <id>\n"
            << "    stats\n";
}

void Cli::printFindResults(const std::vector<db::FindResult>& findResults)
{
  std::cout << "Found " << findResults.size() << " results:\n";

  for (const auto& [path, type, size] : findResults)
  {
    std::string fileType = type == Entry::EntryType::FILE ? "[FILE] " : "[DIR ] ";

    std::cout << fileType << path << " " << size << " bytes\n";
  }
}

void Cli::printShowIndex(const db::ShowIndexResult& index)
{
  std::cout << "ID: " << index.id << "\n"
            << "Path: " << index.root << "\n"
            << "Created at: " << formatTimestamp(index.createdAt) << "\n"
            << "Last scanned at: " << formatTimestamp(index.lastScannedAt) << "\n";
}

void Cli::printDuplicates(const std::vector<dup::DuplicateGroup>& duplicates)
{
  if (duplicates.empty())
  {
    std::cout << "No duplicates found\n";
    return;
  }

  std::cout << "Duplicates:\n-----------\n";

  for (const auto& [hash, files] : duplicates)
  {
    if (!files.empty())
      std::cout << "Hash: " << hash << "\n";

    for (const auto& file : files)
    {
      std::cout << "  " << file << "\n";
    }
  }
}

void Cli::printIndexStats(const db::IndexStatsResult& stats)
{
  std::cout << "ID: " << stats.id << "\n"
            << "Directories: " << stats.dirCount << "\n"
            << "Files: " << stats.fileCount << "\n"
            << "Total size: " << formatBytes(stats.totalSize) << "\n"
            << "Last scanned at: " << formatTimestamp(stats.lastScannedAt) << "\n";
}

void Cli::printError(const app::Error& error)
{
  if (error.type == app::Error::Type::Database)
  {
    std::cerr << error.message << "\n"
              << "SQLite error (" << error.dbError.value().code << ") " << error.dbError.value().message << "\n";
  }
  else
    std::cerr << error.message << "\n";
}

std::optional<std::int64_t> Cli::parseIndexFlag(const std::vector<std::string>& args)
{
  if (args.size() <= 2)
    return std::nullopt;

  if (args[1] != "--index")
    return std::nullopt;

  const std::string_view intToParse = args[2];

  std::int64_t result{};
  const auto [ptr, ec] = std::from_chars(intToParse.data(), intToParse.data() + intToParse.size(), result);
  if (ec != std::errc() || ptr != intToParse.data() + intToParse.size())
    return std::nullopt;

  return result;
}

std::string Cli::formatTimestamp(const std::int64_t timestamp)
{
  // using namespace std::chrono;
  //
  // auto tp = floor<seconds>(system_clock::time_point{seconds(timestamp)});
  // return std::format("{:%Y-%m-%d %H:%M:%S}", tp);

  auto tt = static_cast<std::time_t>(timestamp);

  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif

  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);


  return buffer;
}

std::string Cli::formatBytes(const std::uint64_t bytes)
{
  constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  auto size = static_cast<double>(bytes);

  // Divide by 4 with multiples of 1024 -> convert to unit with index
  int unitIndex = 0;
  while (size >= 1024.0 && unitIndex < 4)
  {
    size /= 1024.0;
    ++unitIndex;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1) << size << " " << units[unitIndex];
  return oss.str();
}