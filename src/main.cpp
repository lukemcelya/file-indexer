#include "IndexApp.h"
#include "Cli.h"

int main(const int argc, const char* argv[])
{
  IndexApp app("index.db");
  Cli cli(app);

  return cli.run(argc, argv);
}