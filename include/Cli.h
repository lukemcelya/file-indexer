#pragma once

#include "IndexApp.h"
#include "Database.h"

#include <string>

class Cli
{
private:
  IndexApp& m_indexApp;

public:
  explicit Cli(IndexApp& indexApp);

  int run(int argc, const char* argv[]);

  static void printError(const Database::Error& error);

private:
  int handleCommand(const std::vector<std::string>& args);
  void repl();

  int handleIndex(std::string_view dir);
  int handleRescan(std::string_view dir);
  [[nodiscard]] int handleFind(const std::vector<std::string>& args) const;
  int handleDuplicate(const std::vector<std::string>& args);
  int handleStats(const std::vector<std::string>& args);
  int handleCompare(const std::vector<std::string>& args);

  static std::vector<std::string> tokenize(const std::string& input);
  static void printUsage();
  static void printFindResults(const std::vector<Database::FindResult>& findResults);
};
