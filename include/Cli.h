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

private:
  int handleCommand(const std::vector<std::string>& args, bool isRepl);
  void repl();

  int handleIndex(std::string_view dir);
  int handleRescan(std::string_view dir);
  [[nodiscard]] int handleFind(const std::vector<std::string>& args) const;
  int handleDuplicate(const std::vector<std::string>& args);
  int handleStats(const std::vector<std::string>& args);
  int handleCompare(const std::vector<std::string>& args);
  [[nodiscard]] int handleShow(const std::vector<std::string>& args) const;

  static std::vector<std::string> tokenize(const std::string& input);
  static void printUsage();
  static void printFindResults(const std::vector<db::FindResult>& findResults);
  static void printShowIndex(const db::ShowIndexResult& index);
  static void printDuplicates(const std::vector<dup::DuplicateGroup>& duplicates);
  static void printError(const app::Error& error);
  static std::optional<std::int64_t> parseIndexFlag(const std::vector<std::string>& args, std::size_t startIndex);
  static std::string formatTimestamp(std::int64_t timestamp);
};
