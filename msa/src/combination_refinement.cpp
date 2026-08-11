#include "combination_refinement.hpp"
#include "event_sink.hpp"
#include "helper.hpp"
#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
  constexpr size_t kRarityThreshold   = 10;
  constexpr size_t kMaxRefinementPass = 20;
  // Cap on Hamming distance (# of mismatched rows) between a rare anchor
  // column's presence pattern and a "wanted" (non-rare) target combination
  // we're willing to try to move it toward. Keeps the per-column search
  // bounded to combinations plausibly reachable with the ~1-2 candidate
  // moves available per row, instead of every distinct combination in the
  // file.
  constexpr size_t kMaxCombinationMismatches = 100;
  constexpr size_t kMaxScenarios             = 10'0000;
  // Dominates any other scenario's score outright: per-column deltas from
  // score_transition are bounded to +-1 and the number of affected columns
  // is bounded by the row count, so this margin guarantees a scenario that
  // fully dissolves the rare anchor column is always preferred, even if it
  // has minor negative side effects elsewhere (e.g. leaves some other
  // column rare).
  constexpr int kDissolveBonus = 10'0000;

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

  size_t column_combination_size(const column_state &state)
  {
    size_t count {};
    for (const auto &present : state.present)
    {
      if (present)
      {
        count++;
      }
    }
    return count;
  }

  bool is_all_filler(const combination_key &key)
  {
    return key.find('1') == std::string::npos;
  }

  // Number of rows where present differs from key's presence bit.
  size_t hamming_distance(const std::vector<bool> &present,
                          const combination_key   &key)
  {
    size_t dist {};
    for (size_t r {}; r < present.size(); ++r)
    {
      if (present[r] != (key[r] == '1'))
      {
        ++dist;
      }
    }
    return dist;
  }

  // Non-rare combination keys, sorted by ascending Hamming distance to
  // anchor_present, capped to kMaxCombinationMismatches.
  std::vector<combination_key> wanted_targets(
      const std::unordered_map<combination_key, size_t> &combination_counts,
      const std::vector<bool>                           &anchor_present,
      const size_t &threshold = kRarityThreshold)
  {
    std::vector<std::pair<size_t, combination_key>> scored;
    for (auto &[key, count] : combination_counts)
    {
      if (count < threshold)
      {
        continue; // not "wanted"
      }
      size_t dist { hamming_distance(anchor_present, key) };
      if (dist == 0 || dist > kMaxCombinationMismatches)
      {
        continue; // dist==0 defensive-only: structurally unreachable, since
                  // wanted keys are non-rare and the anchor's key is rare
      }
      scored.push_back({ dist, key });
    }
    std::sort(scored.begin(),
              scored.end(),
              [](auto &a, auto &b) { return a.first < b.first; });

    std::vector<combination_key> out;
    out.reserve(scored.size());
    for (auto &[dist, key] : scored)
    {
      out.push_back(key);
    }
    return out;
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

  // Expands moves chosen for representative rows into one move per row in
  // each representative's duplicate group (identity for non-duplicated
  // rows). Must be applied before evaluate_scenario/apply, since those need
  // to see every real row's state, not just representatives'.
  std::vector<move_candidate>
      expand_group_moves(const std::vector<move_candidate> &rep_moves,
                         const variant_dedup_groups        &groups)
  {
    std::vector<move_candidate> out;
    for (auto &m : rep_moves)
    {
      for (size_t row : groups.members_of.at(m.row))
      {
        move_candidate expanded { m };
        expanded.row = row;
        out.push_back(expanded);
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
      else
      {
        score += 1;
      }
    };

    for (size_t c : affected)
    {
      score_transition(before[c], after[c]);
    }

    if (auto it { after.find(anchor) };
        it != after.end() && is_all_filler(key_from_state(it->second)))
    {
      score += kDissolveBonus;
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

  // Classifies a scenario as a "strict" improvement for the currently
  // processed rare combination (processed_key = the anchor's combination
  // key before this scenario). Side effects on "other" affected columns
  // (everything but the anchor) are tallied as net sums across all of them,
  // not gated per column: a bad effect on one column may be offset by a
  // good effect on another.
  bool is_strict_improvement(
      const scenario_result                             &eval,
      const combination_key                             &processed_key,
      const std::unordered_map<combination_key, size_t> &combination_counts,
      size_t                                             anchor,
      size_t                                             threshold)
  {
    auto anchor_after_it { std::find_if(eval.after.begin(),
                                        eval.after.end(),
                                        [&](const auto &p)
                                        { return p.first == anchor; }) };
    auto anchor_after_key { anchor_after_it != eval.after.end()
                                ? key_from_state(anchor_after_it->second)
                                : processed_key };

    if (is_all_filler(anchor_after_key))
    {
      log_event("[is_strict_improvement] rule (a) anchor dissolves: anchor="
                + std::to_string(anchor) + ", processed_key=" + processed_key);
      return true; // rule (a): anchor dissolves, always strictly better
    }

    int net_rare_combinations {};
    int net_rare_counter_delta {};

    for (auto &[col, before_state] : eval.before)
    {
      auto  after_it { std::find_if(eval.after.begin(),
                                   eval.after.end(),
                                   [&](const auto &p)
                                   { return p.first == col; }) };
      auto &after_state { after_it->second };

      auto before_key { key_from_state(before_state) };
      auto after_key { key_from_state(after_state) };
      if (before_key == after_key)
      {
        continue;
      }

      if (!is_all_filler(before_key))
      {
        auto   it { combination_counts.find(before_key) };
        size_t before_count { it != combination_counts.end() ? it->second : 0 };
        if (before_count == 1)
        {
          net_rare_combinations -= 1;
        }
        if (before_count > 0 && before_count < threshold)
        {
          net_rare_counter_delta -= 1;
        }
      }

      if (!is_all_filler(after_key))
      {
        auto   it { combination_counts.find(after_key) };
        size_t existing_count { it != combination_counts.end() ? it->second
                                                               : 0 };
        size_t after_count { existing_count + 1 };
        if (after_count < threshold)
        {
          net_rare_counter_delta += 1;
          if (existing_count == 0)
          {
            net_rare_combinations += 1;
          }
        }
      }
    }

    if (net_rare_combinations < 0)
    {
      log_event(
          "[is_strict_improvement] rule (b) combination full removal: anchor="
          + std::to_string(anchor) + ", processed_key=" + processed_key
          + ", net_new_rare=" + std::to_string(net_rare_combinations)
          + ", net_rare_counter_delta="
          + std::to_string(net_rare_counter_delta));
      return true; // rule (b): full removal
    }

    if (net_rare_counter_delta < 0 && net_rare_combinations == 0)
    {
      log_event("[is_strict_improvement] rule (c) combination count net "
                "decrease: anchor="
                + std::to_string(anchor) + ", processed_key=" + processed_key
                + ", net_new_rare=" + std::to_string(net_rare_combinations)
                + ", net_rare_counter_delta="
                + std::to_string(net_rare_counter_delta));
      return true; // rule (c): plain decrease
    }

    return false;
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

  enum class row_action
  {
    no_op,
    push,
    pull
  };

  // strict:      only a scenario classified by is_strict_improvement is
  //              accepted; run once (see run_refinement_passes).
  // exploratory: any feasible scenario is accepted, score only ranks
  //              options within a column; run to a fixed point.
  enum class scenario_mode
  {
    strict,
    exploratory
  };

  // Required action for row r given the anchor's current presence and the
  // target key's desired presence bit.
  row_action required_action(bool anchor_present_r, bool target_present_r)
  {
    if (anchor_present_r == target_present_r)
    {
      return row_action::no_op;
    }
    return target_present_r ? row_action::pull : row_action::push;
  }

  // Runs a single sweep of the rare-combination search over all columns,
  // either forward (0 -> n-1) or backward (n-1 -> 0). Mutates variants'
  // token tables in place. Returns whether anything changed. Candidate
  // generation and scenario enumeration are identical regardless of mode;
  // only the consider() acceptance/ranking policy differs (see
  // scenario_mode).
  bool run_sweep(std::vector<file_variant>  &variants,
                 bool                        forward,
                 const variant_dedup_groups &groups,
                 scenario_mode               mode)
  {
    size_t      rows { variants.size() };
    const auto &representatives { groups.distinct_indices };

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

      auto            anchor_state { read_column(variants, i) };
      combination_key processed_key { key_from_state(anchor_state) };
      if (!is_rare(processed_key))
      {
        continue;
      }

      std::string anchor_text { common_text(anchor_state) };

      // Candidates are computed once per representative row, not once per
      // real row: duplicate rows (same AST) always yield identical
      // candidates, so computing them per row would be pure waste and
      // would blow up the scenario branching below combinatorially.
      std::vector<std::vector<std::optional<move_candidate>>> options(
          representatives.size());
      bool any_option { false };
      for (size_t ri {}; ri < representatives.size(); ++ri)
      {
        size_t rep { representatives[ri] };
        options[ri].push_back(std::nullopt); // no_op
        for (auto &cand : row_candidates(variants, i, rep, n, anchor_text))
        {
          options[ri].push_back(cand);
          any_option = true;
        }
      }

      if (!any_option)
      {
        continue;
      }

      auto targets { wanted_targets(combination_counts,
                                    anchor_state.present,
                                    column_combination_size(anchor_state)) };

      // Also try resolving the rare column by pushing it out entirely,
      // leaving it all-filler. All-filler columns are dropped later,
      // shortening the alignment, so evaluate_scenario awards this a
      // dominant score whenever it's reachable.
      targets.push_back(combination_key(rows, '0'));

      bool            best_found { false };
      scenario_result best;

      auto consider = [&](scenario_result &&eval)
      {
        if (!eval.feasible)
        {
          return;
        }
        if (mode == scenario_mode::strict
            && !is_strict_improvement(
                eval, processed_key, combination_counts, i, kRarityThreshold))
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
      };

      for (auto &target_key : targets)
      {
        // Branched on per representative only: since duplicate rows are
        // content-identical, required_action and target_key bits agree
        // across a whole group (see expand_group_moves), so a
        // representative's decision speaks for its entire group.
        std::vector<std::vector<move_candidate>> row_choices(
            representatives.size());
        bool feasible { true };

        for (size_t ri {}; ri < representatives.size() && feasible; ++ri)
        {
          size_t     r { representatives[ri] };
          row_action action { required_action(anchor_state.present[r],
                                              target_key[r] == '1') };
          if (action == row_action::no_op)
          {
            continue; // row_choices[ri] stays empty -> not branched on
          }

          bool want_pull { action == row_action::pull };
          for (auto &cand : options[ri])
          {
            if (cand && cand->is_pull == want_pull)
            {
              row_choices[ri].push_back(*cand);
            }
          }
          if (row_choices[ri].empty())
          {
            feasible = false; // required row has no matching candidate
          }
        }

        if (!feasible)
        {
          continue;
        }

        size_t num_scenarios { 1 };
        for (const auto row : row_choices)
        {
          num_scenarios *= std::max(1ul, row.size());
        }
        log_event("Found " + std::to_string(num_scenarios)
                  + " scenarios in column " + std::to_string(i + 1)
                  + " with text " + anchor_text);
        if (num_scenarios > kMaxScenarios)
        {
          log_event("Too many scenarios, skipping");
          continue;
        }

        std::vector<move_candidate> chosen;

        std::function<void(size_t)> generate = [&](size_t ri)
        {
          if (ri == representatives.size())
          {
            consider(evaluate_scenario(variants,
                                       expand_group_moves(chosen, groups),
                                       i,
                                       combination_counts,
                                       kRarityThreshold));
            return;
          }
          if (row_choices[ri].empty())
          {
            generate(ri + 1); // no-op row: not part of chosen
            return;
          }
          for (auto &cand : row_choices[ri])
          {
            chosen.push_back(cand);
            generate(ri + 1);
            chosen.pop_back();
          }
        };

        generate(0);
      }

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

    return changed_this_pass;
  }

  struct direction_outcome
  {
      std::vector<token_table> tables;
      size_t                   rare;
      size_t                   combos;
  };

  // Runs the strict phase unconditionally: one forward sweep followed by one
  // backward sweep (each a single pass, per design -- the strict phase never
  // iterates). Applied directly to variants with no before/after comparison
  // of any kind: strict-phase moves are, by construction (see
  // is_strict_improvement), always wanted regardless of their effect on the
  // rare/combos metric used below to gate the exploratory phase. This is a
  // hard floor -- nothing downstream of this call is allowed to revert it.
  void run_strict_phase(std::vector<file_variant>  &variants,
                        const variant_dedup_groups &groups)
  {
    report_progress(
        pipeline_stage::refine_rare_combinations, 1, 2, "forward strict");
    run_sweep(variants, /*forward=*/true, groups, scenario_mode::strict);
    report_progress(
        pipeline_stage::refine_rare_combinations, 2, 2, "backward strict");
    run_sweep(variants, /*forward=*/false, groups, scenario_mode::strict);
  }

  // Runs up to kMaxRefinementPass exploratory passes (iterated to a fixed
  // point), sweeping columns either forward (0 -> n-1) or backward
  // (n-1 -> 0), starting from baseline_tables (the post-strict-phase state,
  // never pristine original). This is the only phase whose result is
  // allowed to be discarded by the caller.
  direction_outcome
      run_exploratory_direction(std::vector<file_variant>      &variants,
                                bool                            forward,
                                const variant_dedup_groups     &groups,
                                const std::vector<token_table> &baseline_tables)
  {
    restore_tables(variants, baseline_tables);
    std::string direction { forward ? "forward" : "backward" };

    for (size_t pass {}; pass < kMaxRefinementPass; ++pass)
    {
      report_progress(pipeline_stage::refine_rare_combinations,
                      pass + 1,
                      kMaxRefinementPass,
                      direction + " exploratory");

      if (!run_sweep(variants, forward, groups, scenario_mode::exploratory))
      {
        break;
      }
    }

    auto   counts { build_combination_counts(variants) };
    size_t rare { count_rare_combinations(counts) };
    return { snapshot_tables(variants), rare, counts.size() };
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

  if (variants.size() < 2)
  {
    return; // no combinations to refine with a single variant
  }

  size_t dbg_initial_n { variants.front().m_token_table->size() };

  // Computed once and reused across every pass and both sweep directions:
  // AST identity (what grouping is keyed on) never changes as tables are
  // mutated/restored below, since only m_token_table is touched.
  auto groups { group_variants_by_ast(variants) };

  auto   before_counts { build_combination_counts(variants) };
  size_t before_rare { count_rare_combinations(before_counts) };
  size_t before_combos { before_counts.size() };

  // Strict phase: applied unconditionally, up front, with no comparison
  // against the pre-strict state at all. This is the floor -- everything
  // below only ever compares against *this*, never against pristine
  // original, so a strict-phase fix can never be reverted by anything that
  // happens afterwards.
  run_strict_phase(variants, groups);
  auto   strict_tables { snapshot_tables(variants) };
  auto   strict_counts { build_combination_counts(variants) };
  size_t strict_rare { count_rare_combinations(strict_counts) };
  size_t strict_combos { strict_counts.size() };

  // Exploratory phase: forward and backward, each restarting from the
  // strict baseline (never from pristine original), so it can only add to
  // the strict phase's gains, never take them away.
  auto forward_result { run_exploratory_direction(
      variants, /*forward=*/true, groups, strict_tables) };
  auto backward_result { run_exploratory_direction(
      variants, /*forward=*/false, groups, strict_tables) };

  enum class winner_t
  {
    strict,
    forward,
    backward
  };
  winner_t winner { winner_t::strict };
  size_t   best_rare { strict_rare };
  size_t   best_combos { strict_combos };

  auto consider = [&](winner_t candidate, size_t rare, size_t combos)
  {
    if (rare < best_rare || (rare == best_rare && combos < best_combos))
    {
      winner      = candidate;
      best_rare   = rare;
      best_combos = combos;
    }
  };

  consider(winner_t::forward, forward_result.rare, forward_result.combos);
  consider(winner_t::backward, backward_result.rare, backward_result.combos);

  log_event("[combination refinement] Scores: before_rare: "
            + std::to_string(before_rare)
            + ", before_combos: " + std::to_string(before_combos)
            + ", strict_rare: " + std::to_string(strict_rare)
            + ", strict_combos: " + std::to_string(strict_combos)
            + ", forward_rare: " + std::to_string(forward_result.rare)
            + ", forward_combos: " + std::to_string(forward_result.combos)
            + ", backward_rare: " + std::to_string(backward_result.rare)
            + ", backward_combos: " + std::to_string(backward_result.combos));

  switch (winner)
  {
    case winner_t::strict :
      restore_tables(variants, strict_tables);
      log_event("[combination refinement] kept strict-only result (no "
                "improving exploratory pass)");
      break;
    case winner_t::forward :
      restore_tables(variants, forward_result.tables);
      log_event("[combination refinement] kept forward exploratory result");
      break;
    case winner_t::backward :
      restore_tables(variants, backward_result.tables);
      log_event("[combination refinement] kept backward exploratory result");
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
              << " distinct file combinations, " << rare_remaining << " rare (<"
              << kRarityThreshold << ")";
  log_event(summary_msg.str());
}
