#include "Cli.h"
#include "IndexApp.h"
#include "Database.h"

#include <filesystem>
#include <iostream>
#include <print>

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
  for (size_t i{1}; i < argc; ++i)
    args.emplace_back(argv[i]);

  const int rc = handleCommand(args);
  if (rc == 1)
    printUsage();

  return rc;
}

void Cli::printError(const Database::Error& error)
{
  std::cerr << error << "\n";
}


int Cli::handleCommand(const std::vector<std::string>& args)
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

  printUsage();
  return 1;
}

void Cli::repl()
{
  while (true)
  {
    std::print("> ");
    std::string input;
    if (!std::getline(std::cin, input))
      break;

    if (input == "quit" || input == "exit") // Maybe add more exit commands
      break;

    std::vector<std::string> args = tokenize(input);

    if (args.empty())
      continue;

    if (handleCommand(args) == 1)
      printUsage();
  }
}

int Cli::handleIndex(std::string_view dir)
{
  if (!m_indexApp.createIndex(dir))
    return 1;

  std::print("Index: {} created.\n", dir);
  return 0;
}

int Cli::handleRescan(std::string_view dir)
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

int Cli::handleFind(const std::vector<std::string>& args)
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
            << "    find         <query>\n"
            << "    duplicates\n"
            << "    stats\n"
            << "    compare      <scan1> <scan2>\n";
}

void Cli::printFindResults(const std::vector<Database::FindResult>& findResults)
{
  std::cout << "Found " << findResults.size() << " results:\n";

  for (const auto& [path, type, size] : findResults)
  {
    std::string fileType = type == Entry::EntryType::FILE ? "[FILE] " : "[DIR ] ";

    std::cout << fileType << path << " " << size << " bytes\n";
  }
}