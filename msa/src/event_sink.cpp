#include "event_sink.hpp"

void event_sink::start(const std::filesystem::path& log_path)
{
  m_out.open(log_path, std::ios::out | std::ios::trunc);
  m_writer = std::thread(
      [this]()
      {
        event e;
        while (true)
        {
          m_queue.pop(e);
          if (e.kind == event_kind::shutdown)
          {
            break;
          }
          m_out << to_json_line(e) << '\n';
          m_out.flush();
        }
      });
}

void event_sink::stop()
{
  m_queue.push(event { .kind = event_kind::shutdown });
  if (m_writer.joinable())
  {
    m_writer.join();
  }
  m_out.close();
}

void event_sink::push(event e)
{
  m_queue.push(std::move(e));
}

namespace
{
  struct event_context
  {
      event_sink* sink { nullptr };
      int         thread_slot {};
      std::string family_name {};
  };

  thread_local event_context g_context {};
} // namespace

void set_event_context(event_sink*        sink,
                       int                thread_slot,
                       const std::string& family_name)
{
  g_context.sink        = sink;
  g_context.thread_slot = thread_slot;
  g_context.family_name = family_name;
}

void clear_event_context()
{
  g_context = event_context {};
}

void log_event(const std::string& message)
{
  if (g_context.sink == nullptr)
  {
    return;
  }
  g_context.sink->push(event { .kind        = event_kind::family_log,
                               .thread_slot = g_context.thread_slot,
                               .family_name = g_context.family_name,
                               .message     = message });
}

void report_progress(pipeline_stage    stage,
                     size_t            current_step,
                     size_t            total_steps,
                     const std::string& detail)
{
  if (g_context.sink == nullptr)
  {
    return;
  }
  g_context.sink->push(event { .kind         = event_kind::family_progress,
                               .thread_slot  = g_context.thread_slot,
                               .family_name  = g_context.family_name,
                               .stage        = stage,
                               .current_step = current_step,
                               .total_steps  = total_steps,
                               .detail       = detail });
}

void report_variant_counts(size_t variant_count, size_t distinct_variant_count)
{
  if (g_context.sink == nullptr)
  {
    return;
  }
  g_context.sink->push(
      event { .kind                   = event_kind::family_variant_info,
             .thread_slot            = g_context.thread_slot,
             .family_name            = g_context.family_name,
             .variant_count          = variant_count,
             .distinct_variant_count = distinct_variant_count });
}

stage_timer::stage_timer(pipeline_stage stage)
  : m_stage(stage)
  , m_start(std::chrono::steady_clock::now())
{
  if (g_context.sink == nullptr)
  {
    return;
  }
  g_context.sink->push(
      event { .kind        = event_kind::family_stage_started,
             .thread_slot = g_context.thread_slot,
             .family_name = g_context.family_name,
             .stage       = m_stage });
}

stage_timer::~stage_timer()
{
  if (g_context.sink == nullptr)
  {
    return;
  }
  double elapsed_ms { std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - m_start)
                          .count() };
  g_context.sink->push(
      event { .kind        = event_kind::family_stage_finished,
             .thread_slot = g_context.thread_slot,
             .family_name = g_context.family_name,
             .stage       = m_stage,
             .duration_ms = elapsed_ms });
}
