#include "alignment.hpp"
#include "arguments.hpp"
#include "file_discovery.hpp"
#include "preprocessing.hpp"
#include <algorithm>
#include <execution>
#include <vector>

int main(int argc, char* argv[])
{
  auto options { parse_cli_arguments(argc, argv) };
  auto file_families { discover_files(options) };

  std::for_each(std::execution::par,
                file_families.begin(),
                file_families.end(),
                [options](auto& file_family)
                {
                  load_asts(file_family.variants, options);
                  build_token_tables(file_family.variants);
                  calculate_ngram_hashes(file_family.variants, options);
                  auto hash_count { build_hash_count(file_family.variants) };

                  align_file_variants(file_family.variants);
                });

  return 0;
}
