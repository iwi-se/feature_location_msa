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
#include <iostream>
#include <tbb/blocked_range.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <vector>

int main(int argc, char* argv[])
{
  auto options { parse_cli_arguments(argc, argv) };
  tbb::global_control gc(tbb::global_control::max_allowed_parallelism,
                         options.threads);
  auto file_families { discover_files(options) };

  std::cout << "Discovered " << file_families.size() << " file families"
            << std::endl;

  std::sort(file_families.begin(),
            file_families.end(),
            [](const auto& a, const auto& b)
            {
              return std::filesystem::file_size(a.variants.front().filepath) >
                     std::filesystem::file_size(b.variants.front().filepath);
            });

  std::atomic<int> processed_count { 0 };
  const size_t     total { file_families.size() };

  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, file_families.size(), 1),
      [&](const tbb::blocked_range<size_t>& range)
      {
        for (size_t i = range.begin(); i != range.end(); ++i)
        {
          const auto& family_info = file_families[i];
          file_family file_family { family_info };

          load_asts(file_family.variants, options);
          build_token_tables(file_family.variants);
          calculate_ngram_hashes(file_family.variants, options);

          // build_guide_tree(file_family, options);
          // print_guide_tree(*file_family.m_guide_tree, file_family);

          align_file_variants(file_family.variants, options);
          // align_guide_tree(file_family, options);

          apply_filler_size(file_family.variants);

          output(file_family, options);

          int current = ++processed_count;
          std::cout << "\rProcessed " << current << "/" << total
                    << " file families" << std::flush;
        }
      },
      tbb::simple_partitioner());

  return 0;
}
