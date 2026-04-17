#include "IndexApp.h"
#include "Cli.h"
#include "PlatformPaths.h"

#include <iostream>


int main(const int argc, const char* argv[])
{
  auto exeDir = util::executableDir();
  auto dbPath = exeDir / "data" / "file-index.db";

  auto dbResult = Database::open(dbPath);
  if (!dbResult)
  {
    std::cerr << dbResult.error().message << "\n";
    return 1;
  }

  IndexApp app(std::move(*dbResult));

  if (const auto loadResult = app.loadIndexStore(); !loadResult)
  {
    std::cerr << loadResult.error().message << "\n";
    return 1;
  }

  Cli cli(app);

  return cli.run(argc, argv);
}
