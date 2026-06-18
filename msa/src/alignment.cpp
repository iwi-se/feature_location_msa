#include "alignment.hpp"
#include "core.hpp"
#include "helper.hpp"
#include "preprocessing.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <unordered_set>
#include <utility>

size_t find_most_similar_element(
    const std::vector<size_t>& source,
    const std::vector<std::reference_wrapper<const std::vector<size_t>>>&
                            hashed_ngrams,
    const std::set<size_t>& ignore_indices)
{
  size_t max_common {};
  size_t max_index {};

  for (size_t i {}; i < hashed_ngrams.size(); ++i)
  {
    if (!ignore_indices.contains(i))
    {
      size_t common = count_common_ngrams(source, hashed_ngrams[i]);
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

std::set<size_t> getHashTokens(const std::shared_ptr<node_t>& n)
{
  std::set<size_t> result {};
  for (auto& l : n->get_leaves())
  {
    result.emplace(l.lock()->get_subtree_hash());
  }
  return result;
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

size_t calculateCommonAncestorProximity(std::shared_ptr<node_t> node1,
                                        std::shared_ptr<node_t> node2)
{
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
        return innerDistance + distance;
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
    auto             n1Leaves { getHashTokens(n1) };
    auto             n2Leaves { getHashTokens(n2) };
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
  double result {};
  double level { 1 };
  if (n1->get_parent()->get_tag() != n2->get_parent()->get_tag())
  {
    result += std::pow(
        static_cast<double>(calculateCommonAncestorProximity(n1, n2)) + 1.0,
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

double score(const alignment_token&              a,
             const alignment_token&              b,
             const hash_count&                   hashCount,
             std::unordered_map<size_t, double>& cache)
{
  if (a.token_kind == alignment_token::token_kind::filler
      || b.token_kind == alignment_token::token_kind::filler)
  {
    return -100.0;
  }
  if (a.node->get_subtree_hash() == b.node->get_subtree_hash())
  {
    return ancestorSimilarity(a.node, b.node, hashCount, cache);
  };
  return -100.0;
}

std::pair<size_t, size_t> calculate_l_range(size_t k, size_t len1, size_t len2)
{
  double k_rel { static_cast<double>(k) / static_cast<double>(len1) };
  double l_begin { std::max(1.0, (k_rel - 0.3) * len2) };
  double l_end { std::min(static_cast<double>(len2), (k_rel + 0.3) * len2) };
  return { l_begin, l_end };
}

void align_pairwise(std::vector<alignment_token>&       seq1,
                    std::vector<alignment_token>&       seq2,
                    const hash_count&                   hashCount,
                    std::unordered_map<size_t, double>& cache)
{
  if (seq1 == seq2)
  {
    return;
  }
  else
  {
    size_t len1 = seq1.size();
    size_t len2 = seq2.size();

    std::vector<std::vector<double>> dp(len1 + 1,
                                        std::vector<double>(len2 + 1, 0));

    for (size_t k = 1; k <= len1; ++k)
    {
      std::pair<size_t, size_t> l_range;
      if (std::abs(1.0
                   - (static_cast<double>(seq1.size())
                      / static_cast<double>(seq2.size())))
          < 0.2)
      {
        l_range = calculate_l_range(k, len1, len2);
      }
      else
      {
        l_range = { 1, len2 };
      }

      for (size_t l = l_range.first; l <= l_range.second; ++l)
      {
        double matchScore = dp[k - 1][l - 1]
                            + score(seq1[k - 1], seq2[l - 1], hashCount, cache);
        double deleteScore = dp[k - 1][l];
        double insertScore = dp[k][l - 1];

        dp[k][l] = std::max({ matchScore, deleteScore, insertScore });
      }
    }

    size_t                       max_len { seq1.size() + seq2.size() };
    std::vector<alignment_token> alignedSeq1(max_len);
    std::vector<alignment_token> alignedSeq2(max_len);

    size_t k { len1 };
    size_t l { len2 };
    size_t m { max_len };

    while (k > 0 || l > 0)
    {
      --m;
      if (k > 0 && l > 0
          && dp[k][l]
                 == dp[k - 1][l - 1]
                        + score(seq1[k - 1], seq2[l - 1], hashCount, cache))
      {
        alignedSeq1[m] = seq1[k - 1];
        alignedSeq2[m] = seq2[l - 1];
        --k;
        --l;
      }
      else if (k > 0 && dp[k][l] == dp[k - 1][l])
      {
        alignedSeq1[m] = seq1[k - 1];
        alignedSeq2[m] = FILLER;
        --k;
      }
      else if (l > 0 && dp[k][l] == dp[k][l - 1])
      {
        alignedSeq1[m] = FILLER;
        alignedSeq2[m] = seq2[l - 1];
        --l;
      }
    }

    std::move(alignedSeq1.begin() + m, alignedSeq1.end(), alignedSeq1.begin());
    alignedSeq1.resize(alignedSeq1.size() - m);

    std::move(alignedSeq2.begin() + m, alignedSeq2.end(), alignedSeq2.begin());
    alignedSeq2.resize(alignedSeq2.size() - m);

    seq1 = std::move(alignedSeq1);
    seq2 = std::move(alignedSeq2);
  }
}

std::vector<alignment_token> merge_aligned_sequences(
    const std::vector<std::vector<alignment_token>*>& sequences)
{
  if (sequences.empty())
  {
    return {};
  }

  size_t num_sequences = sequences.size();
  size_t length        = sequences[0]->size();

  // Ensure all sequences are the same length
  for (const auto& seq : sequences)
  {
    if (seq->size() != length)
    {
      throw std::invalid_argument("All sequences must have the same length");
    }
  }

  std::vector<alignment_token> merged;
  merged.reserve(length);

  // Iterate column by column
  for (size_t pos = 0; pos < length; ++pos)
  {
    for (size_t seq_idx = 0; seq_idx < num_sequences; ++seq_idx)
    {
      const auto& token { (*sequences[seq_idx])[pos] };
      if (!token.is_filler())
      {
        merged.push_back(token);
        break; // move to next column
      }
    }
  }

  return merged;
}

void realign_aligned_sequence(
    std::vector<std::vector<alignment_token>*>& aligned_sequences,
    const std::vector<alignment_token>&         merged_sequence)
{
  for (auto sequence : aligned_sequences)
  {
    std::vector<alignment_token> realigned_sequence {};
    realigned_sequence.reserve(merged_sequence.size());
    size_t k {};
    for (size_t i {}; i < merged_sequence.size(); ++i)
    {
      if (merged_sequence[i].is_filler())
      {
        realigned_sequence.push_back(FILLER);
      }
      else
      {
        realigned_sequence.push_back((*sequence)[k]);
        ++k;
      }
    }
    *sequence = std::move(realigned_sequence);
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
  auto                               hash_count { build_hash_count(variants) };
  std::unordered_map<size_t, double> cache {};

  auto most_similar_pair_indices { find_most_similar_pair(ngram_hashes,
                                                          options) };

  align_pairwise(*variants[most_similar_pair_indices.first].m_token_table,
                 *variants[most_similar_pair_indices.second].m_token_table,
                 hash_count,
                 cache);

  std::vector<std::vector<alignment_token>*> aligned_sequences {
    &(*variants[most_similar_pair_indices.first].m_token_table),
    &(*variants[most_similar_pair_indices.second].m_token_table)
  };
  std::set<size_t> used_indices { most_similar_pair_indices.first,
                                  most_similar_pair_indices.second };

  while (used_indices.size() < variants.size())
  {
    auto   merged { merge_aligned_sequences(aligned_sequences) };
    auto   merged_ngram_hashes { hash_ngrams(
        calculate_ngrams(merged, options.n_gram_size)) };
    size_t next_most_similar_index { find_most_similar_element(
        merged_ngram_hashes, ngram_hashes, used_indices) };

    align_pairwise(merged,
                   *variants[next_most_similar_index].m_token_table,
                   hash_count,
                   cache);

    used_indices.insert(next_most_similar_index);

    realign_aligned_sequence(aligned_sequences, merged);

    aligned_sequences.push_back(
        &(*variants[next_most_similar_index].m_token_table));
  }
}

std::vector<std::vector<alignment_token>*>
    align_guide_tree_node(guide_tree_node&                    node,
                          file_family&                        family,
                          const hash_count&                   hash_count,
                          std::unordered_map<size_t, double>& cache)
{
  //
  // Leaf
  //
  if (node.is_leaf())
  {
    return { &(*family.variants[node.variant_index.value()].m_token_table) };
  }

  //
  // Recursively align children first
  //
  auto left_sequences
      = align_guide_tree_node(*node.left, family, hash_count, cache);

  auto right_sequences
      = align_guide_tree_node(*node.right, family, hash_count, cache);

  //
  // Build profiles for both subtrees
  //
  auto left_profile = merge_aligned_sequences(left_sequences);

  auto right_profile = merge_aligned_sequences(right_sequences);

  //
  // Align the profiles
  //
  align_pairwise(left_profile, right_profile, hash_count, cache);

  //
  // Push gaps back into descendants
  //
  realign_aligned_sequence(left_sequences, left_profile);

  realign_aligned_sequence(right_sequences, right_profile);

  //
  // Return all sequences belonging to this subtree
  //
  left_sequences.insert(
      left_sequences.end(), right_sequences.begin(), right_sequences.end());

  return left_sequences;
}

void align_guide_tree(file_family& family, const options& options)
{
  if (!family.m_guide_tree.has_value() || !family.m_guide_tree->root)
  {
    return;
  }

  auto hash_count = build_hash_count(family.variants);

  std::unordered_map<size_t, double> cache {};

  align_guide_tree_node(*family.m_guide_tree->root, family, hash_count, cache);
}
