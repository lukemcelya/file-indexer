#include "IndexApp.h"
#include "Cli.h"

int main(const int argc, const char* argv[])
{
  IndexApp app("data/file-index.db");
  Cli cli(app);

  return cli.run(argc, argv);
}