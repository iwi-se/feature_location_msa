#include "alignment.hpp"
#include "core.hpp"
#include "event_sink.hpp"
#include "helper.hpp"
#include "preprocessing.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

size_t find_most_similar_element(
    const std::vector<size_t>& source,
    const std::vector<std::reference_wrapper<const std::vector<size_t>>>&
                            hashed_ngrams,
    const std::set<size_t>& ignore_indices)
{
  double max_common {};
  size_t max_index {};

  for (size_t i {}; i < hashed_ngrams.size(); ++i)
  {
    if (!ignore_indices.contains(i))
    {
      double common = file_similarity(source, hashed_ngrams[i]);
      if (common > max_common)
      {
        max_common = common;
        max_index  = i;
      }
    }
  }
  return max_index;
}

std::pair<size_t, size_t> find_most_similar_pair(
    const std::vector<std::reference_wrapper<const std::vector<size_t>>>&
                   hashed_ngrams,
    const options& options)
{
  size_t max_common {};
  size_t best_i { 0 }, best_j { 0 };

  for (size_t i = 0; i < hashed_ngrams.size(); ++i)
  {
    for (size_t j = i + 1; j < hashed_ngrams.size(); ++j)
    {
      size_t common = count_common_ngrams(hashed_ngrams[i], hashed_ngrams[j]);
      if (common > max_common)
      {
        max_common = common;
        best_i     = i;
        best_j     = j;
      }
    }
  }

  return { best_i, best_j };
}

size_t scoreLcsCount(const std::set<size_t>& lcs, const hash_count& hash_count)
{
  size_t score {};
  for (auto& tok : lcs)
  {
    score
        += hash_count.max + 1 - std::min(hash_count.m.at(tok), hash_count.max);
  }
  return score;
}

// Same rarity weighting as scoreLcsCount's per-token term, for a single
// subtree hash: rarer tokens (lower corpus-wide count) score higher, common/
// boilerplate tokens score lower. Falls back to a count of 0 (max weight) if
// the hash wasn't seen while building hash_count (shouldn't normally happen
// for a real token, but avoids an out_of_range throw either way).
double token_rarity_weight(size_t hash, const hash_count& hash_count)
{
  auto   it { hash_count.m.find(hash) };
  size_t count { it != hash_count.m.end() ? it->second : 0 };
  return static_cast<double>(hash_count.max + 1
                             - std::min(count, hash_count.max));
}

constexpr size_t kAncestorProximitySalt { 0x9E37'79B9'7F4A'7C15ULL };

size_t
    calculateCommonAncestorProximity(std::shared_ptr<node_t>             node1,
                                     std::shared_ptr<node_t>             node2,
                                     std::unordered_map<size_t, double>& cache)
{
  size_t key { (node1->get_subtree_hash()
                ^ (node2->get_subtree_hash() * kAncestorProximitySalt))
               ^ kAncestorProximitySalt };
  if (auto it = cache.find(key); it != cache.end())
  {
    return static_cast<size_t>(it->second);
  }

  size_t distance { 0 };
  while (node1 != nullptr)
  {
    node1 = node1->get_parent();
    size_t innerDistance { 0 };
    auto   tempNode2 { node2 };
    while (tempNode2 != nullptr)
    {
      tempNode2 = tempNode2->get_parent();
      if (node1 != nullptr && tempNode2 != nullptr
          && node1->get_tag() == tempNode2->get_tag())
      {
        size_t result { innerDistance + distance };
        cache.insert({ key, static_cast<double>(result) });
        return result;
      }
      innerDistance++;
    }
    distance++;
  }
  throw std::logic_error("Nodes have no common ancestor");
}

std::set<size_t> commonTokens(const std::set<size_t>& a,
                              const std::set<size_t>& b)
{
  if (a == b)
  {
    return a;
  }
  std::set<size_t> result;
  for (const auto& tok : a)
  {
    if (b.find(tok) != b.end())
    {
      result.insert(tok);
    }
  }
  return result;
}

double subtreeSimilarity(const std::shared_ptr<node_t>&      n1,
                         const std::shared_ptr<node_t>&      n2,
                         const hash_count&                   hashCount,
                         std::unordered_map<size_t, double>& cache)
{
  size_t subtreeHashPair { n1->get_subtree_hash() ^ n2->get_subtree_hash() };

  if (cache.find(subtreeHashPair) != cache.end())
  {
    return cache[subtreeHashPair];
  }

  double result {};
  if (n1->get_subtree_hash() == n2->get_subtree_hash())
  {
    result += 1;
  }
  else
  {
    auto             n1Leaves { n1->get_leaf_hashes() };
    auto             n2Leaves { n2->get_leaf_hashes() };
    auto             n1LeavesScore { scoreLcsCount(n1Leaves, hashCount) };
    auto             n2LeavesScore { scoreLcsCount(n2Leaves, hashCount) };
    std::set<size_t> lcsResult {};
    if (FAST)
    {
      lcsResult = commonTokens(n1Leaves, n2Leaves);
    }
    else
    {
      // lcsResult = lcs(n1Leaves, n2Leaves).lcs;
    }
    auto lcsScore { scoreLcsCount(lcsResult, hashCount) };
    result = (static_cast<double>(lcsScore)
              / static_cast<double>(std::max(n1LeavesScore, n2LeavesScore)));
  }

  cache.insert({ subtreeHashPair, result });
  return result;
}

double ancestorSimilarity(std::shared_ptr<node_t>             n1,
                          std::shared_ptr<node_t>             n2,
                          const hash_count&                   hashCount,
                          std::unordered_map<size_t, double>& cache)
{
  if (n1 == nullptr || n2 == nullptr || n1->get_parent() == nullptr
      || n2->get_parent() == nullptr)
  {
    return 0;
  }

  auto   n1Orig { n1 };
  double result { 1.0 };
  double level { 1 };
  if (n1->get_parent()->get_tag() != n2->get_parent()->get_tag())
  {
    result += std::pow(
        static_cast<double>(calculateCommonAncestorProximity(n1, n2, cache)),
        -2);
  }
  else
  {
    while (level < 5)
    {
      n1        = n1->get_parent();
      n2        = n2->get_parent();
      auto temp = (std::pow(level, -0.5))
                  * subtreeSimilarity(n1, n2, hashCount, cache) * 10;

      result += temp;

      ++level;

      if (n1->get_parent() == nullptr || n2->get_parent() == nullptr)
      {
        break;
      }
    }
  }
  return result;
}

constexpr size_t kMaxContextDistance { 10 };

// Scores how similar the immediate context of two candidate tokens is:
// `row_table[row_pos]` and `candidate_table[candidate_pos]` are the two
// tokens being compared (already known to match, per score_column's caller);
// this walks up to kMaxContextDistance steps backward and forward from them,
// and for each step where both sides' token shares the same subtree_hash,
// adds that token's rarity weight (see token_rarity_weight - rarer tokens
// count for more, common/boilerplate tokens count for less), stopping at
// the first mismatch (a FILLER never matches) in each direction. Returns an
// unnormalized score.
double contextSimilarity(const token_table& row_table,
                         size_t             row_pos,
                         const token_table& candidate_table,
                         size_t             candidate_pos,
                         const hash_count&  hashCount)
{
  double score {};

  long a { static_cast<long>(row_pos) - 1 };
  long b { static_cast<long>(candidate_pos) - 1 };
  for (size_t step {}; step < kMaxContextDistance && a >= 0 && b >= 0; ++step)
  {
    const auto& ta { row_table[static_cast<size_t>(a)] };
    const auto& tb { candidate_table[static_cast<size_t>(b)] };
    if (ta.is_filler() || tb.is_filler())
    {
      break;
    }
    size_t hash { ta.node->get_subtree_hash() };
    if (hash != tb.node->get_subtree_hash())
    {
      break;
    }
    score += token_rarity_weight(hash, hashCount);
    --a;
    --b;
  }

  size_t fa { row_pos + 1 };
  size_t fb { candidate_pos + 1 };
  for (size_t step {}; step < kMaxContextDistance && fa < row_table.size()
                       && fb < candidate_table.size();
       ++step)
  {
    const auto& ta { row_table[fa] };
    const auto& tb { candidate_table[fb] };
    if (ta.is_filler() || tb.is_filler())
    {
      break;
    }
    size_t hash { ta.node->get_subtree_hash() };
    if (hash != tb.node->get_subtree_hash())
    {
      break;
    }
    score += token_rarity_weight(hash, hashCount);
    ++fa;
    ++fb;
  }

  return score;
}

// Experimentation flag: how to combine scores across multiple profile rows
// in a column (see score_column).
enum class column_score_mode
{
  best,
  average,
  worst
};
constexpr column_score_mode kColumnScoreMode { column_score_mode::worst };

// Experimentation flag: when to stop the iterative refinement loop in
// align_file_variants. score_based = stop once a pass no longer improves
// compute_alignment_score (reverting that non-improving pass); no_change =
// stop once a pass leaves every variant's token table unchanged.
enum class refinement_stop_mode
{
  score_based,
  no_change
};
constexpr refinement_stop_mode kRefinementStopMode {
  refinement_stop_mode::score_based
};

// One distinct non-filler subtree hash seen in a profile column, plus the
// row indices (into whatever `column` vector this index was built from)
// that carry it.
struct column_hash_group
{
    size_t              hash {};
    std::vector<size_t> rows {};
};

// Groups a profile column's non-filler tokens by exact subtree_hash equality
// via a small linear-scan build, mirroring how the old ancestor-fingerprint
// merge/dedup grouped column reps (a plain vector, no heap-allocating hash
// map) - the number of distinct shapes in a column is normally tiny, so this
// beats an unordered_map in practice despite being O(rows * groups) to
// build. Scoring only ever needs rows whose hash equals the candidate's, so
// building this once per column (rather than rescanning per DP cell) turns
// each score_column() call into an O(matches) lookup instead of an O(rows)
// scan.
std::vector<column_hash_group>
    build_column_hash_index(const std::vector<const alignment_token*>& column)
{
  std::vector<column_hash_group> groups;
  for (size_t i {}; i < column.size(); ++i)
  {
    if (column[i]->token_kind == alignment_token::token_kind::filler)
    {
      continue;
    }
    size_t hash { column[i]->node->get_subtree_hash() };
    bool   found { false };
    for (auto& group : groups)
    {
      if (group.hash == hash)
      {
        group.rows.push_back(i);
        found = true;
        break;
      }
    }
    if (!found)
    {
      groups.push_back({ hash, { i } });
    }
  }
  return groups;
}

// Scores `candidate` against a profile column's tokens, using a precomputed
// hash_index (see build_column_hash_index) to find only the rows that can
// possibly match, without ever materializing a merged pseudo-token.
// `exclude_index`, if set, skips that row (used by compute_alignment_score
// for leave-one-out scoring). `row_tables`/`column_pos` and
// `candidate_table`/`candidate_pos` locate `column`'s rows and `candidate`
// within their real token_table + position, so the match score can consult
// each side's neighboring tokens (see contextSimilarity).
double score_column(const std::vector<column_hash_group>&      hash_index,
                    const std::vector<const alignment_token*>& column,
                    const std::vector<token_table*>&           row_tables,
                    size_t                                     column_pos,
                    std::optional<size_t>                      exclude_index,
                    const alignment_token&                     candidate,
                    const token_table&                         candidate_table,
                    size_t                                     candidate_pos,
                    const hash_count&                          hashCount,
                    std::unordered_map<size_t, double>&        cache)
{
  if (candidate.token_kind == alignment_token::token_kind::filler)
  {
    return -100.0;
  }

  size_t                     target_hash { candidate.node->get_subtree_hash() };
  const std::vector<size_t>* rows { nullptr };
  for (const auto& group : hash_index)
  {
    if (group.hash == target_hash)
    {
      rows = &group.rows;
      break;
    }
  }
  if (rows == nullptr)
  {
    return -100.0;
  }

  // Leave-one-out normally skips `exclude_index`, but if that row is the
  // *only* one carrying this hash (a token genuinely unique to it - common
  // for variant-specific code), skipping it would leave nothing to compare
  // against and wrongly score legitimately unique code as a total mismatch.
  // Fall back to comparing it against itself in that case, matching what
  // scoring against the full (self-inclusive) profile always did.
  bool skip_exclusion { exclude_index.has_value() && rows->size() == 1
                        && (*rows)[0] == *exclude_index };

  double worst { std::numeric_limits<double>::max() };
  double best { -100.0 };
  double sum { 0.0 };
  size_t matches { 0 };
  for (size_t row : *rows)
  {
    if (!skip_exclusion && exclude_index.has_value() && row == *exclude_index)
    {
      continue;
    }
    double s { ancestorSimilarity(
        column[row]->node, candidate.node, hashCount, cache) };
    // Experimental: context-based scoring plugged in here in place of
    // ancestorSimilarity - see contextSimilarity above.
    // double s { contextSimilarity(*row_tables[row],
    //                             column_pos,
    //                             candidate_table,
    //                             candidate_pos,
    //                             hashCount)
    //};
    best   = std::max(best, s);
    worst  = std::min(worst, s);
    sum   += s;
    ++matches;
  }

  if (matches == 0)
  {
    return -100.0;
  }

  switch (kColumnScoreMode)
  {
    case column_score_mode::best :
      return best;
    case column_score_mode::worst :
      return worst;
    case column_score_mode::average :
      return sum / static_cast<double>(matches);
  }

  return sum / static_cast<double>(matches);
}

std::pair<size_t, size_t> calculate_l_range(size_t k, size_t len1, size_t len2)
{
  double k_rel { static_cast<double>(k) / static_cast<double>(len1) };
  double l_begin { std::max(1.0, (k_rel - 0.3) * len2) };
  double l_end { std::min(static_cast<double>(len2), (k_rel + 0.3) * len2) };
  return { l_begin, l_end };
}

// Builds a plain (non-profile) representative token_table out of a profile,
// picking each column's first non-filler row (skipping all-filler columns
// entirely). Used both for the n-gram similarity lookup and as the cheap
// content-equality check in align_profile_to_sequence.
token_table
    build_consensus_sequence(const std::vector<token_table*>& profile_rows)
{
  token_table consensus {};
  if (profile_rows.empty())
  {
    return consensus;
  }

  size_t length { profile_rows[0]->size() };
  consensus.reserve(length);
  for (size_t pos {}; pos < length; ++pos)
  {
    for (auto* row : profile_rows)
    {
      const auto& tok { (*row)[pos] };
      if (!tok.is_filler())
      {
        consensus.push_back(tok);
        break;
      }
    }
  }
  return consensus;
}

// Aligns a profile (multiple already column-aligned rows, each possibly
// containing FILLER) against a single new `sequence`, column by column.
// Mutates every row pointed to by `profile_rows` and `sequence` in place so
// they all end up the same, gap-extended length. A lone file is just a
// 1-row profile, so this also covers plain file-vs-file alignment.
void align_profile_to_sequence(const std::vector<token_table*>&    profile_rows,
                               token_table&                        sequence,
                               const hash_count&                   hash_count,
                               std::unordered_map<size_t, double>& cache)
{
  if (profile_rows.empty())
  {
    return;
  }

  // Cheap content-equality shortcut, mirroring the old align_pairwise's
  // `seq1 == seq2` check (there, seq1 was always the merged/deduped
  // profile). Skips the whole O(n*m) DP whenever re-aligning would be a
  // no-op - which is the common case once a profile has converged, e.g. on
  // later passes of the iterative refinement loop.
  if (build_consensus_sequence(profile_rows) == sequence)
  {
    return;
  }

  size_t n { profile_rows[0]->size() };
  size_t m { sequence.size() };

  // Precompute the column-token view and hash index for each profile column
  // once; reused across every candidate sequence position in the DP fill and
  // backtrack.
  std::vector<std::vector<const alignment_token*>> columns(n);
  std::vector<std::vector<column_hash_group>>      column_hash_indices(n);
  for (size_t k {}; k < n; ++k)
  {
    columns[k].reserve(profile_rows.size());
    for (auto* row : profile_rows)
    {
      columns[k].push_back(&(*row)[k]);
    }
    column_hash_indices[k] = build_column_hash_index(columns[k]);
  }

  auto match_score = [&](size_t k, size_t l)
  {
    return score_column(column_hash_indices[k - 1],
                        columns[k - 1],
                        profile_rows,
                        k - 1,
                        std::nullopt,
                        sequence[l - 1],
                        sequence,
                        l - 1,
                        hash_count,
                        cache);
  };

  std::vector<std::vector<double>> dp(n + 1, std::vector<double>(m + 1, 0));

  for (size_t k = 1; k <= n; ++k)
  {
    std::pair<size_t, size_t> l_range;
    if (std::abs(1.0 - (static_cast<double>(n) / static_cast<double>(m))) < 0.2)
    {
      l_range = calculate_l_range(k, n, m);
    }
    else
    {
      l_range = { 1, m };
    }

    for (size_t l = l_range.first; l <= l_range.second; ++l)
    {
      double matchScore  = dp[k - 1][l - 1] + match_score(k, l);
      double deleteScore = dp[k - 1][l];
      double insertScore = dp[k][l - 1];

      dp[k][l] = std::max({ matchScore, deleteScore, insertScore });
    }
  }

  size_t                   max_len { n + m };
  std::vector<token_table> aligned_rows(profile_rows.size(),
                                        token_table(max_len));
  token_table              aligned_sequence(max_len);

  size_t k { n };
  size_t l { m };
  size_t pos { max_len };

  while (k > 0 || l > 0)
  {
    --pos;
    if (k > 0 && l > 0 && dp[k][l] == dp[k - 1][l - 1] + match_score(k, l))
    {
      for (size_t r {}; r < profile_rows.size(); ++r)
      {
        aligned_rows[r][pos] = (*profile_rows[r])[k - 1];
      }
      aligned_sequence[pos] = sequence[l - 1];
      --k;
      --l;
    }
    else if (k > 0 && dp[k][l] == dp[k - 1][l])
    {
      for (size_t r {}; r < profile_rows.size(); ++r)
      {
        aligned_rows[r][pos] = (*profile_rows[r])[k - 1];
      }
      aligned_sequence[pos] = FILLER;
      --k;
    }
    else if (l > 0 && dp[k][l] == dp[k][l - 1])
    {
      for (size_t r {}; r < profile_rows.size(); ++r)
      {
        aligned_rows[r][pos] = FILLER;
      }
      aligned_sequence[pos] = sequence[l - 1];
      --l;
    }
  }

  for (size_t r {}; r < profile_rows.size(); ++r)
  {
    std::move(aligned_rows[r].begin() + pos,
              aligned_rows[r].end(),
              aligned_rows[r].begin());
    aligned_rows[r].resize(aligned_rows[r].size() - pos);
    *profile_rows[r] = std::move(aligned_rows[r]);
  }

  std::move(aligned_sequence.begin() + pos,
            aligned_sequence.end(),
            aligned_sequence.begin());
  aligned_sequence.resize(aligned_sequence.size() - pos);
  sequence = std::move(aligned_sequence);
}

// Total alignment quality across all variants: for each column, scores
// every row's non-filler token against the *other* rows at that column
// (leave-one-out), summed over every column and row. Used as the
// convergence measure for iterative refinement.
double compute_alignment_score(std::vector<file_variant>&          variants,
                               const hash_count&                   hash_count,
                               std::unordered_map<size_t, double>& cache)
{
  std::vector<token_table*> all_sequences;
  all_sequences.reserve(variants.size());
  for (auto& variant : variants)
  {
    all_sequences.push_back(&(*variant.m_token_table));
  }

  if (all_sequences.empty())
  {
    return 0.0;
  }

  size_t length { all_sequences[0]->size() };
  double total {};
  for (size_t pos {}; pos < length; ++pos)
  {
    std::vector<const alignment_token*> column;
    column.reserve(all_sequences.size());
    for (auto* sequence : all_sequences)
    {
      column.push_back(&(*sequence)[pos]);
    }
    auto hash_index { build_column_hash_index(column) };

    for (size_t r {}; r < column.size(); ++r)
    {
      if (column[r]->is_filler())
      {
        continue;
      }
      total += score_column(hash_index,
                            column,
                            all_sequences,
                            pos,
                            r,
                            *column[r],
                            *all_sequences[r],
                            pos,
                            hash_count,
                            cache);
    }
  }
  return total;
}

std::vector<token_table>
    snapshot_token_tables(const std::vector<file_variant>& variants)
{
  std::vector<token_table> snapshot;
  snapshot.reserve(variants.size());
  for (const auto& variant : variants)
  {
    snapshot.push_back(*variant.m_token_table);
  }
  return snapshot;
}

void restore_token_tables(std::vector<file_variant>& variants,
                          std::vector<token_table>&  snapshot)
{
  for (size_t i {}; i < variants.size(); ++i)
  {
    *variants[i].m_token_table = std::move(snapshot[i]);
  }
}

void align_file_variants(std::vector<file_variant>& variants,
                         const options&             options)
{
  std::vector<std::reference_wrapper<const std::vector<size_t>>> ngram_hashes;
  for (const auto& variant : variants)
  {
    ngram_hashes.push_back(*variant.hashed_ngrams);
  }
  // Scoring stays based on every variant (including duplicates), so
  // deduplication below only changes which variants get pairwise-aligned,
  // not how similarity is scored.
  auto                               hash_count { build_hash_count(variants) };
  std::unordered_map<size_t, double> cache {};

  // Variants that are byte-identical share the same AST pointer (load_asts
  // dedupes them). Group variants by AST identity so the alignment
  // algorithm below only ever runs over one representative per distinct
  // file; duplicates are copied from their representative's finished table
  // at the end instead of being independently (and redundantly) aligned.
  auto         groups { group_variants_by_ast(variants) };
  const auto&  distinct_indices { groups.distinct_indices };
  const size_t distinct_variant_count { distinct_indices.size() };
  report_variant_counts(variants.size(), distinct_variant_count);

  const size_t total_alignments { distinct_variant_count == 0
                                      ? 0
                                      : distinct_variant_count - 1 };
  size_t       completed_alignments { 0 };

  if (distinct_variant_count >= 2)
  {
    std::vector<std::reference_wrapper<const std::vector<size_t>>>
        distinct_ngram_hashes;
    for (size_t idx : distinct_indices)
    {
      distinct_ngram_hashes.push_back(*variants[idx].hashed_ngrams);
    }

    auto   seed_local_pair { find_most_similar_pair(distinct_ngram_hashes,
                                                  options) };
    size_t seed_first { distinct_indices[seed_local_pair.first] };
    size_t seed_second { distinct_indices[seed_local_pair.second] };

    align_profile_to_sequence({ &(*variants[seed_first].m_token_table) },
                              *variants[seed_second].m_token_table,
                              hash_count,
                              cache);
    ++completed_alignments;
    report_progress(pipeline_stage::align_file_variants,
                    completed_alignments,
                    total_alignments);

    std::vector<token_table*> aligned_sequences {
      &(*variants[seed_first].m_token_table),
      &(*variants[seed_second].m_token_table)
    };
    std::set<size_t> used_local_indices { seed_local_pair.first,
                                          seed_local_pair.second };

    while (used_local_indices.size() < distinct_variant_count)
    {
      auto   consensus { build_consensus_sequence(aligned_sequences) };
      auto   merged_ngram_hashes { hash_ngrams(
          calculate_ngrams(consensus, options.n_gram_size)) };
      size_t next_local_index { find_most_similar_element(
          merged_ngram_hashes, distinct_ngram_hashes, used_local_indices) };
      size_t next_variant_index { distinct_indices[next_local_index] };

      align_profile_to_sequence(aligned_sequences,
                                *variants[next_variant_index].m_token_table,
                                hash_count,
                                cache);
      ++completed_alignments;
      report_progress(pipeline_stage::align_file_variants,
                      completed_alignments,
                      total_alignments);

      used_local_indices.insert(next_local_index);

      aligned_sequences.push_back(
          &(*variants[next_variant_index].m_token_table));
    }
  }

  // Duplicates never went through the alignment loop above, so their token
  // tables are still at their original (pre-alignment) length; copy the
  // finished, filler-padded table from their representative.
  for (const auto& [row, rep] : groups.representative_of)
  {
    if (row != rep)
    {
      variants[row].m_token_table = variants[rep].m_token_table;
    }
  }

  if (distinct_indices.size() < 3)
  {
    return; // refine_alignment needs >=2 "other" variants to realign against
  }

  constexpr size_t kMaxRefinementIterations { 50 };
  double           current_score {};
  if constexpr (kRefinementStopMode == refinement_stop_mode::score_based)
  {
    current_score = compute_alignment_score(variants, hash_count, cache);
  }
  size_t executed_iterations { 0 };
  auto   refinement_start { std::chrono::steady_clock::now() };
  stage_timer refinement_stage_timer(pipeline_stage::iterative_refinement);
  for (size_t iteration {}; iteration < kMaxRefinementIterations; ++iteration)
  {
    auto pass_start { std::chrono::steady_clock::now() };
    auto snapshot { snapshot_token_tables(variants) };

    refine_alignment(variants, hash_count, cache);

    ++executed_iterations;
    auto pass_ms { std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - pass_start)
                       .count() };

    if constexpr (kRefinementStopMode == refinement_stop_mode::no_change)
    {
      bool changed { false };
      for (size_t i {}; i < variants.size(); ++i)
      {
        if (*variants[i].m_token_table != snapshot[i])
        {
          changed = true;
          break;
        }
      }
      {
        std::ostringstream msg;
        msg << "pass took " << pass_ms
            << "ms, changed=" << (changed ? "yes" : "no");
        report_progress(pipeline_stage::iterative_refinement,
                        iteration + 1,
                        kMaxRefinementIterations,
                        msg.str());
      }
      if (!changed)
      {
        break;
      }
    }
    else
    {
      double new_score { compute_alignment_score(variants, hash_count, cache) };
      {
        std::ostringstream msg;
        msg << "pass took " << pass_ms << "ms, score " << current_score
            << " -> " << new_score;
        report_progress(pipeline_stage::iterative_refinement,
                        iteration + 1,
                        kMaxRefinementIterations,
                        msg.str());
      }
      if (new_score <= current_score)
      {
        restore_token_tables(variants, snapshot);
        break;
      }
      current_score = new_score;
    }
  }
  {
    auto total_ms { std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - refinement_start)
                        .count() };
    std::ostringstream msg;
    msg << "refinement loop finished after " << executed_iterations << "/"
        << kMaxRefinementIterations << " iterations, total " << total_ms
        << "ms";
    log_event(msg.str());
  }
}

token_table extract_non_filler_tokens(const token_table& sequence)
{
  token_table result {};
  result.reserve(sequence.size());
  for (const auto& token : sequence)
  {
    if (!token.is_filler())
    {
      result.push_back(token);
    }
  }
  return result;
}

std::vector<bool>
    find_non_empty_columns(const std::vector<token_table*>& sequences)
{
  if (sequences.empty())
  {
    return {};
  }

  size_t            length = sequences[0]->size();
  std::vector<bool> keep(length, false);

  for (size_t pos = 0; pos < length; ++pos)
  {
    for (const auto* sequence : sequences)
    {
      if (!(*sequence)[pos].is_filler())
      {
        keep[pos] = true;
        break;
      }
    }
  }

  return keep;
}

void refine_alignment(std::vector<file_variant>&          variants,
                      const hash_count&                   hash_count,
                      std::unordered_map<size_t, double>& cache)
{
  for (size_t i {}; i < variants.size(); ++i)
  {
    std::vector<token_table*> other_sequences {};
    other_sequences.reserve(variants.size() - 1);
    for (size_t j {}; j < variants.size(); ++j)
    {
      if (j != i)
      {
        other_sequences.push_back(&(*variants[j].m_token_table));
      }
    }

    auto keep_column { find_non_empty_columns(other_sequences) };

    // Compact every other variant to only the columns where at least one of
    // the remaining variants has a token.
    std::vector<token_table> compacted_others(other_sequences.size());
    for (auto& seq : compacted_others)
    {
      seq.reserve(keep_column.size());
    }

    for (size_t pos {}; pos < keep_column.size(); ++pos)
    {
      if (keep_column[pos])
      {
        for (size_t seq_idx {}; seq_idx < other_sequences.size(); ++seq_idx)
        {
          compacted_others[seq_idx].push_back((*other_sequences[seq_idx])[pos]);
        }
      }
    }

    auto original_tokens { extract_non_filler_tokens(
        *variants[i].m_token_table) };

    std::vector<token_table*> compacted_pointers {};
    compacted_pointers.reserve(compacted_others.size());
    for (auto& seq : compacted_others)
    {
      compacted_pointers.push_back(&seq);
    }

    align_profile_to_sequence(
        compacted_pointers, original_tokens, hash_count, cache);

    for (size_t seq_idx {}; seq_idx < other_sequences.size(); ++seq_idx)
    {
      *other_sequences[seq_idx] = std::move(compacted_others[seq_idx]);
    }

    *variants[i].m_token_table = std::move(original_tokens);
  }
}
