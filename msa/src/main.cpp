#include "alignment.hpp"
#include "arguments.hpp"
#include "core.hpp"
#include "file_discovery.hpp"
#include "guide_tree.hpp"
#include "helper.hpp"
#include "output.hpp"
#include "postprocessing.hpp"
#include "preprocessing.hpp"
#include <algorithm>
#include <execution>
#include <iostream>
#include <tbb/global_control.h>
#include <vector>

int main(int argc, char* argv[])
{
  // Limit threads
  tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 6);

  auto options { parse_cli_arguments(argc, argv) };
  auto file_families { discover_files(options) };

  std::cout << "Discovered " << file_families.size() << " file families"
            << std::endl;

  std::atomic<int> processed_count { 0 };
  const size_t     total { file_families.size() };

  std::for_each(std::execution::par,
                file_families.begin(),
                file_families.end(),
                [options, &processed_count, total](const auto& family_info)
                {
                  file_family file_family { family_info };

                  load_asts(file_family.variants, options);
                  build_token_tables(file_family.variants);
                  calculate_ngram_hashes(file_family.variants, options);

                  build_guide_tree(file_family, options);
                  print_guide_tree(*file_family.m_guide_tree, file_family);

                  align_file_variants(file_family.variants, options);

                  apply_filler_size(file_family.variants);

                  output(file_family, options);

                  int current = ++processed_count;
                  std::cout << "\rProcessed " << current << "/" << total
                            << " file families" << std::flush;
                });

  return 0;
}
