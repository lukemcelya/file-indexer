#include "Cli.h"

#include <charconv>

#include "IndexApp.h"
#include "Database.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <sstream>

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

  const int rc = handleCommand(args, false);
  if (rc == 1)
    printUsage();

  return rc;
}

void Cli::printError(const db::Error& error)
{
  std::cerr << error << "\n";
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
  if (command == "duplicate")
    return handleDuplicate(args);
  if (command == "stats")
    return handleStats(args);
  if (command == "compare")
    return handleCompare(args);
  if (command == "show")
    return handleShow(args, isRepl);

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

    if (handleCommand(args, true) == 1)
      printUsage();
  }
}

int Cli::handleIndex(const std::string_view dir)
{
  if (!m_indexApp.createIndex(dir))
    return 1;

  std::cout << "Index: " << dir << " created\n";
  return 0;
}

int Cli::handleRescan(const std::string_view dir)
{
  const auto result = m_indexApp.rescanIndex(dir);
  if (!result)
  {
    std::cout << result.error() << "\n";
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

  if (result.empty())
  {
    return 1;
  }

  printFindResults(result);
  return 0;
}

int Cli::handleDuplicate(const std::vector<std::string>& args)
{
  return 0;
}

int Cli::handleStats(const std::vector<std::string>& args)
{
  return 0;
}

int Cli::handleCompare(const std::vector<std::string>& args)
{
  return 0;
}

int Cli::handleShow(const std::vector<std::string>& args, bool isRepl) const
{
  const std::size_t startIndex = isRepl ? 1 : 2; // On REPL, 0th arg is command instead of process
  const auto indexId = parseIndexFlag(args, startIndex);

  if (!indexId)
  {
    std::cout << "No valid index\n";
    return 1;
  }

  auto result = m_indexApp.showIndex(*indexId);
  if (!result)
  {
    std::cout << result.error() << "\n";
  }

  printShowIndex(result.value());

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
            << "    index        <directory>\n"
            << "    rescan       <directory>\n"
            << "    show         <directory>\n"
            << "    find         <query>\n"
            << "    duplicates\n"
            << "    stats\n"
            << "    compare      <scan1> <scan2>\n";
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
            << "Created at: " << index.createdAt << "\n"
            << "Last scanned at: " << index.lastScannedAt << "\n";
}

std::optional<std::int64_t> Cli::parseIndexFlag(const std::vector<std::string>& args, const std::size_t startIndex)
{
  if (startIndex >= args.size())
    return std::nullopt;

  if (args[startIndex] != "--index")
    return std::nullopt;

  const std::string_view intToParse = args[startIndex + 1];

  std::int64_t result{};
  const auto [ptr, ec] = std::from_chars(intToParse.data(), intToParse.data() + intToParse.size(), result);
  if (ec != std::errc() || ptr != intToParse.data() + intToParse.size())
    return std::nullopt;

  return result;
}
