#include "alignment.hpp"
#include "arguments.hpp"
#include "combination_refinement.hpp"
#include "core.hpp"
#include "event_sink.hpp"
#include "events.hpp"
#include "file_discovery.hpp"
#include "helper.hpp"
#include "output.hpp"
#include "postprocessing.hpp"
#include "preprocessing.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <tbb/blocked_range.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <vector>

int main(int argc, char* argv[])
{
  auto                options { parse_cli_arguments(argc, argv) };
  tbb::global_control gc(tbb::global_control::max_allowed_parallelism,
                         options.threads);
  auto                file_families { discover_files(options) };

  std::cout << "Discovered " << file_families.size() << " file families"
            << std::endl;

  std::sort(file_families.begin(),
            file_families.end(),
            [](const auto& a, const auto& b)
            {
              return std::filesystem::file_size(a.variants.front().filepath)
                     > std::filesystem::file_size(b.variants.front().filepath);
            });

  std::filesystem::create_directories(options.output_directory);

  event_sink sink;
  sink.start(options.output_directory / "events.jsonl");

  const auto run_start { std::chrono::steady_clock::now() };
  const std::string run_id { std::to_string(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count()) };

  sink.push(event { .kind           = event_kind::run_started,
                    .total_families = file_families.size(),
                    .thread_count   = static_cast<size_t>(
                        tbb::this_task_arena::max_concurrency()),
                    .run_id = run_id });

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

          const int thread_slot { tbb::this_task_arena::current_thread_index() };
          set_event_context(&sink, thread_slot, file_family.name);
          const auto family_start { std::chrono::steady_clock::now() };

          {
            stage_timer t(pipeline_stage::load_asts);
            load_asts(file_family.variants, options);
          }
          {
            stage_timer t(pipeline_stage::build_token_tables);
            build_token_tables(file_family.variants);
          }
          {
            stage_timer t(pipeline_stage::calculate_ngram_hashes);
            calculate_ngram_hashes(file_family.variants, options);
          }

          {
            stage_timer t(pipeline_stage::align_file_variants);
            align_file_variants(file_family.variants, options);
          }

          {
            stage_timer t(pipeline_stage::refine_rare_combinations);
            refine_rare_combinations(file_family.variants);
          }

          {
            stage_timer t(pipeline_stage::apply_filler_size);
            apply_filler_size(file_family.variants);
          }

          {
            stage_timer t(pipeline_stage::output);
            output(file_family, options);
          }

          double family_duration_ms { std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now()
                                          - family_start)
                                          .count() };
          sink.push(event { .kind        = event_kind::family_finished,
                            .thread_slot = thread_slot,
                            .family_name = file_family.name,
                            .duration_ms = family_duration_ms });
          clear_event_context();

          int current = ++processed_count;
          std::cout << "\rProcessed " << current << "/" << total
                    << " file families" << std::flush;
        }
      },
      tbb::simple_partitioner());

  double run_duration_ms { std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - run_start)
                               .count() };
  sink.push(event { .kind = event_kind::run_finished,
                    .duration_ms = run_duration_ms });
  sink.stop();

  return 0;
}
