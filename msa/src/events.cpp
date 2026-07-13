#include "events.hpp"
#include <cstdio>
#include <sstream>

std::string render_stage(pipeline_stage stage)
{
  switch (stage)
  {
    case pipeline_stage::load_asts : return "load_asts";
    case pipeline_stage::build_token_tables : return "build_token_tables";
    case pipeline_stage::calculate_ngram_hashes :
      return "calculate_ngram_hashes";
    case pipeline_stage::align_file_variants : return "align_file_variants";
    case pipeline_stage::refine_rare_combinations :
      return "refine_rare_combinations";
    case pipeline_stage::apply_filler_size : return "apply_filler_size";
    case pipeline_stage::output : return "output";
  }
  return "unknown";
}

namespace
{
  std::string render_kind(event_kind kind)
  {
    switch (kind)
    {
      case event_kind::run_started : return "run_started";
      case event_kind::family_stage_started : return "family_stage_started";
      case event_kind::family_stage_finished :
        return "family_stage_finished";
      case event_kind::family_progress : return "family_progress";
      case event_kind::family_variant_info : return "family_variant_info";
      case event_kind::family_log : return "family_log";
      case event_kind::family_finished : return "family_finished";
      case event_kind::run_finished : return "run_finished";
      case event_kind::shutdown : return "shutdown";
    }
    return "unknown";
  }

  void write_json_string(std::ostringstream& out, const std::string& s)
  {
    out << '"';
    for (char c : s)
    {
      switch (c)
      {
        case '"' : out << "\\\""; break;
        case '\\' : out << "\\\\"; break;
        case '\n' : out << "\\n"; break;
        case '\r' : out << "\\r"; break;
        case '\t' : out << "\\t"; break;
        default :
          if (static_cast<unsigned char>(c) < 0x20)
          {
            char buf[7];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out << buf;
          }
          else
          {
            out << c;
          }
      }
    }
    out << '"';
  }

  long long to_epoch_ms(const std::chrono::system_clock::time_point& tp)
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               tp.time_since_epoch())
        .count();
  }
} // namespace

std::string to_json_line(const event& e)
{
  std::ostringstream out;
  out << "{";
  out << "\"kind\":";
  write_json_string(out, render_kind(e.kind));
  out << ",\"timestamp_ms\":" << to_epoch_ms(e.timestamp);
  out << ",\"thread_slot\":" << e.thread_slot;

  if (!e.family_name.empty())
  {
    out << ",\"family\":";
    write_json_string(out, e.family_name);
  }

  if (e.kind == event_kind::family_stage_started
      || e.kind == event_kind::family_stage_finished
      || e.kind == event_kind::family_progress)
  {
    out << ",\"stage\":";
    write_json_string(out, render_stage(e.stage));
  }

  if (e.kind == event_kind::family_progress)
  {
    out << ",\"current_step\":" << e.current_step;
    out << ",\"total_steps\":" << e.total_steps;
    if (!e.detail.empty())
    {
      out << ",\"detail\":";
      write_json_string(out, e.detail);
    }
  }

  if (e.kind == event_kind::family_variant_info)
  {
    out << ",\"variant_count\":" << e.variant_count;
    out << ",\"distinct_variant_count\":" << e.distinct_variant_count;
  }

  if (e.kind == event_kind::family_log)
  {
    out << ",\"message\":";
    write_json_string(out, e.message);
  }

  if (e.kind == event_kind::family_stage_finished
      || e.kind == event_kind::family_finished
      || e.kind == event_kind::run_finished)
  {
    out << ",\"duration_ms\":" << e.duration_ms;
  }

  if (e.kind == event_kind::run_started)
  {
    out << ",\"total_families\":" << e.total_families;
    out << ",\"thread_count\":" << e.thread_count;
    out << ",\"run_id\":";
    write_json_string(out, e.run_id);
  }

  out << "}";
  return out.str();
}
