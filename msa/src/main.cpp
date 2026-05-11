#include "arguments.hpp"
#include "file_discovery.hpp"
#include "parser.hpp"
#include <algorithm>
#include <execution>
#include <iostream>
#include <vector>

std::vector<file_variant_ast>
    load_asts(const std::vector<file_variant_path>& variants,
              const options&                        options)
{
  std::vector<file_variant_ast> file_variants {};
  for (const auto& file_variant : variants)
  {
    try
    {
      auto             content { parseFile(file_variant.filepath,
                               render_language(options.language)) };
      file_variant_ast new_file_variant { file_variant.variant, content };
      file_variants.emplace_back(new_file_variant);
    }
    catch (...)
    {
      std::cerr << "Unable to parse file " << file_variant.filepath
                << " of variant " << file_variant.variant << "." << std::endl;
    }
  }
  return file_variants;
}

int main(int argc, char* argv[])
{
  auto options { parse_cli_arguments(argc, argv) };
  auto file_families { discover_files(options) };

  std::for_each(
      std::execution::par,
      file_families.begin(),
      file_families.end(),
      [options](auto file_family)
      { auto file_variants_ast { load_asts(file_family.variants, options) }; });

  return 0;
}
