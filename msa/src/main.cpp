#include "arguments.hpp"
#include "file_discovery.hpp"

int main(int argc, char *argv[])
{
  auto options { parse_cli_arguments(argc, argv) };
  auto file_families { discover_files(options) };
  return 0;
}
