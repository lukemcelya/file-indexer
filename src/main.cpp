#include "IndexApp.h"
#include "Cli.h"

#include <iostream>

int main(const int argc, const char* argv[])
{
  auto dbResult = Database::open("data/file-index.db");
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