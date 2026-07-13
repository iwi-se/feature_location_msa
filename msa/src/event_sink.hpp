#pragma once
#include "events.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <tbb/concurrent_queue.h>

// Single writer thread draining a concurrent queue fed by worker threads,
// appending JSON-Lines records to a log file for the dashboard to tail.
class event_sink
{
  public:
    void start(const std::filesystem::path& log_path);
    void stop();

    void push(event e);

  private:
    tbb::concurrent_bounded_queue<event> m_queue;
    std::thread                          m_writer;
    std::ofstream                        m_out;
};

// Per-thread context so deeply-nested code can emit log events without
// threading a sink reference through every call signature.
void set_event_context(event_sink*        sink,
                       int                thread_slot,
                       const std::string& family_name);
void clear_event_context();

// Emits a family_log event using the current thread's context. No-op if no
// context/sink has been set (e.g. code called outside the pipeline).
void log_event(const std::string& message);

// Emits a family_progress event using the current thread's context, for
// stages made up of a known number of discrete steps (e.g. the N-1
// pairwise alignments in align_file_variants). No-op if no context/sink
// has been set.
void report_progress(pipeline_stage stage, size_t current_step, size_t total_steps);

// Emits a family_variant_info event using the current thread's context,
// reporting how many variants a family has and how many of those are
// actually distinct by content (identical-content variants don't need an
// independent alignment). No-op if no context/sink has been set.
void report_variant_counts(size_t variant_count, size_t distinct_variant_count);

// RAII helper: emits family_stage_started on construction and
// family_stage_finished (with elapsed time) on destruction, using the
// current thread's context.
class stage_timer
{
  public:
    explicit stage_timer(pipeline_stage stage);
    ~stage_timer();

    stage_timer(const stage_timer&)            = delete;
    stage_timer& operator=(const stage_timer&) = delete;

  private:
    pipeline_stage                                     m_stage;
    std::chrono::steady_clock::time_point               m_start;
};
