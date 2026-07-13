#pragma once
#include <chrono>
#include <string>

enum class pipeline_stage
{
  load_asts,
  build_token_tables,
  calculate_ngram_hashes,
  align_file_variants,
  refine_rare_combinations,
  apply_filler_size,
  output
};

std::string render_stage(pipeline_stage stage);

enum class event_kind
{
  run_started,
  family_stage_started,
  family_stage_finished,
  family_progress,
  family_variant_info,
  family_log,
  family_finished,
  run_finished,
  shutdown // internal sentinel, never written to the log
};

struct event
{
    event_kind                                        kind;
    std::chrono::system_clock::time_point              timestamp { std::chrono::system_clock::now() };
    int                                                thread_slot {};
    std::string                                        family_name {};
    pipeline_stage                                     stage {};
    std::string                                        message {};
    double                                              duration_ms {};
    size_t                                              total_families {};
    size_t                                              thread_count {};
    std::string                                         run_id {};
    size_t                                              current_step {};
    size_t                                              total_steps {};
    std::string                                         detail {};
    size_t                                              variant_count {};
    size_t                                              distinct_variant_count {};
};

// Serializes an event to a single JSON-Lines record (no trailing newline).
std::string to_json_line(const event& e);
