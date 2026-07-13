#include "combination_refinement.hpp"
#include "event_sink.hpp"
#include <functional>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
  constexpr size_t kRarityThreshold   = 10;
  constexpr size_t kMaxRefinementPass = 10;

  struct column_state
  {
      std::vector<bool>        present;
      std::vector<std::string> text;
  };

  column_state read_column(const std::vector<file_variant> &variants,
                           size_t                           col)
  {
    column_state state;
    state.present.resize(variants.size());
    state.text.resize(variants.size());
    for (size_t r {}; r < variants.size(); ++r)
    {
      auto &token { (*variants[r].m_token_table)[col] };
      state.present[r] = token.is_node();
      if (state.present[r])
      {
        state.text[r] = token.node->get_ts_text();
      }
    }
    return state;
  }

  combination_key key_from_state(const column_state &state)
  {
    combination_key key(state.present.size(), '0');
    for (size_t r {}; r < state.present.size(); ++r)
    {
      if (state.present[r])
      {
        key[r] = '1';
      }
    }
    return key;
  }

  bool is_all_filler(const combination_key &key)
  {
    return key.find('1') == std::string::npos;
  }

  bool state_is_consistent(const column_state &state)
  {
    std::optional<std::string> reference;
    for (size_t r {}; r < state.present.size(); ++r)
    {
      if (!state.present[r])
      {
        continue;
      }
      if (!reference)
      {
        reference = state.text[r];
      }
      else if (*reference != state.text[r])
      {
        return false;
      }
    }
    return true;
  }

  // Text shared by the present tokens of a (consistent) column; empty if none.
  std::string common_text(const column_state &state)
  {
    for (size_t r {}; r < state.present.size(); ++r)
    {
      if (state.present[r])
      {
        return state.text[r];
      }
    }
    return {};
  }

  // A single token relocation within one row, sliding across that row's
  // fillers from from_col to to_col. from_col/to_col may be far apart.
  struct move_candidate
  {
      size_t      row;
      size_t      from_col;
      size_t      to_col;
      std::string text;
      bool        is_pull; // pull = bring a token into the anchor column
  };

  // Generate the candidate moves available to row r at anchor column i.
  // A row contributes at most one pull and/or one push per side.
  std::vector<move_candidate>
      row_candidates(const std::vector<file_variant> &variants,
                     size_t                           i,
                     size_t                           r,
                     size_t                           n,
                     const std::string               &anchor_text)
  {
    std::vector<move_candidate> out;
    auto                       &row_table { *variants[r].m_token_table };

    auto row_filler = [&](size_t col) { return row_table[col].is_filler(); };
    auto row_text
        = [&](size_t col) { return row_table[col].node->get_ts_text(); };

    if (row_filler(i))
    {
      // PULL: the first non-filler encountered on each side is the only
      // candidate. It can be pulled in iff it matches the anchor column's
      // token; a non-matching token blocks the search (never skipped).
      for (size_t j { i }; j-- > 0;)
      {
        if (!row_filler(j))
        {
          if (!anchor_text.empty() && row_text(j) == anchor_text)
          {
            out.push_back({ r, j, i, row_text(j), true });
          }
          break;
        }
      }
      for (size_t j { i + 1 }; j < n; ++j)
      {
        if (!row_filler(j))
        {
          if (!anchor_text.empty() && row_text(j) == anchor_text)
          {
            out.push_back({ r, j, i, row_text(j), true });
          }
          break;
        }
      }
    }
    else
    {
      // PUSH: slide this row's token across its own fillers to the nearest
      // column that already holds a matching token. Row r's own next token
      // bounds the search on each side.
      std::string tok { row_text(i) };

      auto column_matches = [&](size_t col)
      {
        auto state { read_column(variants, col) };
        bool any_present { false };
        for (size_t rr {}; rr < variants.size(); ++rr)
        {
          if (state.present[rr])
          {
            any_present = true;
            if (state.text[rr] != tok)
            {
              return false;
            }
          }
        }
        return any_present;
      };

      for (size_t j { i }; j-- > 0;)
      {
        if (!row_filler(j))
        {
          break;
        }
        if (column_matches(j))
        {
          out.push_back({ r, i, j, tok, false });
          break;
        }
      }
      for (size_t j { i + 1 }; j < n; ++j)
      {
        if (!row_filler(j))
        {
          break;
        }
        if (column_matches(j))
        {
          out.push_back({ r, i, j, tok, false });
          break;
        }
      }
    }

    return out;
  }

  struct scenario_result
  {
      bool                                         feasible { false };
      int                                          score { 0 };
      bool                                         touches_left_col { false };
      bool                                         touches_right_col { false };
      int                                          pull_count { 0 };
      std::vector<move_candidate>                  moves;
      std::vector<std::pair<size_t, column_state>> before;
      std::vector<std::pair<size_t, column_state>> after;
  };

  scenario_result evaluate_scenario(
      const std::vector<file_variant>                   &variants,
      const std::vector<move_candidate>                 &moves,
      size_t                                             anchor,
      const std::unordered_map<combination_key, size_t> &combination_counts,
      size_t                                             threshold)
  {
    scenario_result result;
    if (moves.empty())
    {
      return result;
    }

    std::unordered_set<size_t> affected;
    for (auto &m : moves)
    {
      affected.insert(m.from_col);
      affected.insert(m.to_col);
    }

    std::unordered_map<size_t, column_state> before;
    std::unordered_map<size_t, column_state> after;
    for (size_t c : affected)
    {
      auto state { read_column(variants, c) };
      before[c] = state;
      after[c]  = std::move(state);
    }

    for (auto &m : moves)
    {
      after[m.from_col].present[m.row] = false;
      after[m.from_col].text[m.row].clear();
      after[m.to_col].present[m.row] = true;
      after[m.to_col].text[m.row]    = m.text;
    }

    for (auto &[c, state] : after)
    {
      if (!state_is_consistent(state))
      {
        return result;
      }
    }

    int score { 0 };

    auto score_transition
        = [&](const column_state &before_state, const column_state &after_state)
    {
      auto before_key { key_from_state(before_state) };
      auto after_key { key_from_state(after_state) };
      if (before_key == after_key)
      {
        return;
      }

      if (!is_all_filler(before_key))
      {
        auto   it { combination_counts.find(before_key) };
        size_t before_count { it != combination_counts.end() ? it->second : 0 };
        if (before_count > 0 && before_count < threshold)
        {
          score += 1;
        }
      }

      if (!is_all_filler(after_key))
      {
        auto   it { combination_counts.find(after_key) };
        size_t after_count { (it != combination_counts.end() ? it->second : 0)
                             + 1 };
        if (after_count < threshold)
        {
          score -= 1;
        }
      }
    };

    for (size_t c : affected)
    {
      score_transition(before[c], after[c]);
    }

    result.feasible = true;
    result.score    = score;
    result.moves    = moves;
    for (auto &m : moves)
    {
      if (m.is_pull)
      {
        result.pull_count++;
      }
      if (m.from_col < anchor || m.to_col < anchor)
      {
        result.touches_left_col = true;
      }
      if (m.from_col > anchor || m.to_col > anchor)
      {
        result.touches_right_col = true;
      }
    }
    for (size_t c : affected)
    {
      result.before.push_back({ c, before[c] });
      result.after.push_back({ c, after[c] });
    }
    return result;
  }

  void apply_transition(
      std::unordered_map<combination_key, size_t> &combination_counts,
      const column_state                          &before,
      const column_state                          &after)
  {
    auto before_key { key_from_state(before) };
    auto after_key { key_from_state(after) };
    if (before_key == after_key)
    {
      return;
    }

    if (!is_all_filler(before_key))
    {
      auto before_it { combination_counts.find(before_key) };
      if (before_it != combination_counts.end())
      {
        if (--before_it->second == 0)
        {
          combination_counts.erase(before_it);
        }
      }
    }

    if (!is_all_filler(after_key))
    {
      combination_counts[after_key]++;
    }
  }

  size_t count_rare_combinations(
      const std::unordered_map<combination_key, size_t> &counts)
  {
    size_t rare {};
    for (auto &[key, count] : counts)
    {
      if (count < kRarityThreshold)
      {
        ++rare;
      }
    }
    return rare;
  }

  std::vector<token_table>
      snapshot_tables(const std::vector<file_variant> &variants)
  {
    std::vector<token_table> tables;
    tables.reserve(variants.size());
    for (auto &v : variants)
    {
      tables.push_back(*v.m_token_table);
    }
    return tables;
  }

  void restore_tables(std::vector<file_variant>      &variants,
                      const std::vector<token_table> &tables)
  {
    for (size_t r {}; r < variants.size(); ++r)
    {
      variants[r].m_token_table = tables[r];
    }
  }

  // Runs up to kMaxRefinementPass passes of the rare-combination merge
  // search, sweeping columns either forward (0 -> n-1) or backward
  // (n-1 -> 0) within each pass. Mutates variants' token tables in place.
  void run_refinement_passes(std::vector<file_variant> &variants, bool forward)
  {
    size_t rows { variants.size() };

    for (size_t pass {}; pass < kMaxRefinementPass; ++pass)
    {
      auto   combination_counts { build_combination_counts(variants) };
      size_t n { variants.front().m_token_table->size() };

      auto is_rare = [&](const combination_key &key)
      {
        auto it { combination_counts.find(key) };
        return it != combination_counts.end() && it->second > 0
               && it->second < kRarityThreshold;
      };

      bool changed_this_pass { false };

      for (size_t idx {}; idx < n; ++idx)
      {
        size_t i { forward ? idx : n - 1 - idx };

        std::ostringstream progress_detail;
        progress_detail << (forward ? "forward" : "backward") << ", pass "
                        << (pass + 1) << "/" << kMaxRefinementPass;
        report_progress(pipeline_stage::refine_rare_combinations,
                        idx + 1,
                        n,
                        progress_detail.str());

        auto anchor_state { read_column(variants, i) };
        if (!is_rare(key_from_state(anchor_state)))
        {
          continue;
        }

        std::string anchor_text { common_text(anchor_state) };

        std::vector<std::vector<std::optional<move_candidate>>> options(rows);
        bool any_option { false };
        for (size_t r {}; r < rows; ++r)
        {
          options[r].push_back(std::nullopt); // no_op
          for (auto &cand : row_candidates(variants, i, r, n, anchor_text))
          {
            options[r].push_back(cand);
            any_option = true;
          }
        }

        if (!any_option)
        {
          continue;
        }

        bool            best_found { false };
        scenario_result best;

        std::vector<move_candidate> chosen;

        std::function<void(size_t)> recurse = [&](size_t r)
        {
          if (r == rows)
          {
            auto eval { evaluate_scenario(
                variants, chosen, i, combination_counts, kRarityThreshold) };
            if (!eval.feasible || eval.score < 0)
            {
              return;
            }

            bool better {};
            if (!best_found)
            {
              better = true;
            }
            else if (eval.score != best.score)
            {
              better = eval.score > best.score;
            }
            else if (auto eval_touches
                     = forward ? eval.touches_left_col : eval.touches_right_col,
                     best_touches
                     = forward ? best.touches_left_col : best.touches_right_col;
                     eval_touches != best_touches)
            {
              better = !eval_touches;
            }
            else if (eval.pull_count != best.pull_count)
            {
              better = eval.pull_count < best.pull_count;
            }
            else
            {
              better = false;
            }

            if (better)
            {
              best_found = true;
              best       = std::move(eval);
            }
            return;
          }

          for (auto &opt : options[r])
          {
            if (opt)
            {
              chosen.push_back(*opt);
              recurse(r + 1);
              chosen.pop_back();
            }
            else
            {
              recurse(r + 1);
            }
          }
        };

        recurse(0);

        if (!best_found)
        {
          continue;
        }

        for (size_t k {}; k < best.before.size(); ++k)
        {
          apply_transition(
              combination_counts, best.before[k].second, best.after[k].second);
        }

        for (auto &m : best.moves)
        {
          auto &table { *variants[m.row].m_token_table };
          table[m.to_col]   = std::move(table[m.from_col]);
          table[m.from_col] = FILLER;
        }

        changed_this_pass = true;
      }

      if (!changed_this_pass)
      {
        break;
      }
    }
  }
} // namespace

combination_key compute_combination(const std::vector<file_variant> &variants,
                                    size_t                           col)
{
  return key_from_state(read_column(variants, col));
}

std::unordered_map<combination_key, size_t>
    build_combination_counts(const std::vector<file_variant> &variants)
{
  std::unordered_map<combination_key, size_t> counts;
  if (variants.empty() || !variants.front().m_token_table)
  {
    return counts;
  }

  size_t n { variants.front().m_token_table->size() };
  for (size_t col {}; col < n; ++col)
  {
    auto key { compute_combination(variants, col) };
    if (is_all_filler(key))
    {
      continue;
    }
    counts[key]++;
  }
  return counts;
}

void refine_rare_combinations(std::vector<file_variant> &variants)
{
  if (variants.empty() || !variants.front().m_token_table)
  {
    return;
  }

  size_t dbg_initial_n { variants.front().m_token_table->size() };

  auto   original_tables { snapshot_tables(variants) };
  auto   before_counts { build_combination_counts(variants) };
  size_t before_rare { count_rare_combinations(before_counts) };
  size_t before_combos { before_counts.size() };

  run_refinement_passes(variants, /*forward=*/true);
  auto   forward_tables { snapshot_tables(variants) };
  auto   forward_counts { build_combination_counts(variants) };
  size_t forward_rare { count_rare_combinations(forward_counts) };
  size_t forward_combos { forward_counts.size() };

  restore_tables(variants, original_tables);
  run_refinement_passes(variants, /*forward=*/false);
  auto   backward_tables { snapshot_tables(variants) };
  auto   backward_counts { build_combination_counts(variants) };
  size_t backward_rare { count_rare_combinations(backward_counts) };
  size_t backward_combos { backward_counts.size() };

  enum class winner_t
  {
    before,
    forward,
    backward
  };
  winner_t winner { winner_t::before };
  size_t   best_rare { before_rare };
  size_t   best_combos { before_combos };

  auto consider = [&](winner_t candidate, size_t rare, size_t combos)
  {
    if (rare < best_rare || (rare == best_rare && combos < best_combos))
    {
      winner      = candidate;
      best_rare   = rare;
      best_combos = combos;
    }
  };

  consider(winner_t::forward, forward_rare, forward_combos);
  consider(winner_t::backward, backward_rare, backward_combos);

  switch (winner)
  {
    case winner_t::before :
      restore_tables(variants, original_tables);
      log_event("[combination refinement] kept original (no improving pass)");
      break;
    case winner_t::forward :
      restore_tables(variants, forward_tables);
      log_event("[combination refinement] kept forward pass result");
      break;
    case winner_t::backward :
      restore_tables(variants, backward_tables);
      log_event("[combination refinement] kept backward pass result");
      break;
  }

  // Drop columns that became all-filler; this can only shrink the profile.
  size_t              n { variants.front().m_token_table->size() };
  std::vector<size_t> columns_to_keep;
  columns_to_keep.reserve(n);
  for (size_t col {}; col < n; ++col)
  {
    bool all_filler { true };
    for (auto &variant : variants)
    {
      if ((*variant.m_token_table)[col].is_node())
      {
        all_filler = false;
        break;
      }
    }
    if (!all_filler)
    {
      columns_to_keep.push_back(col);
    }
  }

  if (columns_to_keep.size() != n)
  {
    for (auto &variant : variants)
    {
      token_table compacted;
      compacted.reserve(columns_to_keep.size());
      for (auto col : columns_to_keep)
      {
        compacted.push_back(std::move((*variant.m_token_table)[col]));
      }
      *variant.m_token_table = std::move(compacted);
    }
  }

  // TEMP validation: profile must not have grown and every column must be
  // internally consistent (no two differing tokens in one column).
  {
    size_t final_n { variants.front().m_token_table->size() };
    size_t bad_columns {};
    for (size_t col {}; col < final_n; ++col)
    {
      if (!state_is_consistent(read_column(variants, col)))
      {
        ++bad_columns;
      }
    }
    for (auto &variant : variants)
    {
      if (variant.m_token_table->size() != final_n)
      {
        log_event("[VALIDATION] ERROR: unequal row lengths");
      }
    }
    std::ostringstream validation_msg;
    validation_msg << "[VALIDATION] columns " << dbg_initial_n << " -> "
                   << final_n
                   << (final_n <= dbg_initial_n ? " (ok, not grown)"
                                                 : " (GREW!)")
                   << ", inconsistent columns: " << bad_columns;
    log_event(validation_msg.str());
  }

  auto   final_counts { build_combination_counts(variants) };
  size_t rare_remaining {};
  for (auto &[key, count] : final_counts)
  {
    if (count < kRarityThreshold)
    {
      ++rare_remaining;
    }
  }
  std::ostringstream summary_msg;
  summary_msg << "[combination refinement] " << final_counts.size()
              << " distinct file combinations, " << rare_remaining
              << " rare (<" << kRarityThreshold << ")";
  log_event(summary_msg.str());
}
