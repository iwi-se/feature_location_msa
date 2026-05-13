#include "alignment.hpp"
#include "arguments.hpp"
#include <functional>
#include <unordered_set>

size_t count_common_ngrams(const std::vector<size_t>& a,
                           const std::vector<size_t>& b)
{
  std::unordered_set<size_t> ngram_hashes_set { a.begin(), a.end() };

  size_t count {};
  for (const auto& ngram_hash : b)
  {
    if (ngram_hashes_set.contains(ngram_hash))
    {
      ++count;
    }
  }
  return count;
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

void align_pairwise

    (std::vector<alignment_token>&       a,
     std::vector<alignment_token>&       b,
     const hash_count&                   hashCount,
     std::unordered_map<size_t, double>& cache)
{
  if (a == b)
  {
    return;
  }
  else
  {
    const auto& seq1 = a;
    const auto& seq2 = b;

    size_t len1 = seq1.size();
    size_t len2 = seq2.size();

    vector<vector<double>> dp(len1 + 1, vector<double>(len2 + 1, 0));

    // Initialize the scoring matrix
    for (size_t k = 0; k <= len1; ++k)
    {
      dp[k][0] = GAP * k;
    }
    for (size_t k = 0; k <= len2; ++k)
    {
      dp[0][k] = GAP * k;
    }

    // Fill the scoring matrix
    for (size_t k = 1; k <= len1; ++k)
    {
      for (size_t l = 1; l <= len2; ++l)
      {
        double matchScore = dp[k - 1][l - 1]
                            + score(seq1[k - 1], seq2[l - 1], hashCount, cache);
        double deleteScore = dp[k - 1][l] + GAP;
        double insertScore = dp[k][l - 1] + GAP;

        dp[k][l] = std::max({ matchScore, deleteScore, insertScore });
        // std::cout << matchScore << " ";
      }
      // std::cout << std::endl;
    }

    // Backtrack to find the alignment
    vector<AlignmentToken> alignedSeq1;
    vector<AlignmentToken> alignedSeq2;

    size_t k = len1;
    size_t l = len2;

    while (k > 0 || l > 0)
    {
      if (k > 0 && l > 0
          && dp[k][l]
                 == dp[k - 1][l - 1]
                        + score(seq1[k - 1], seq2[l - 1], hashCount, cache))
      {
        alignedSeq1.push_back(seq1[k - 1]);
        alignedSeq2.push_back(seq2[l - 1]);
        --k;
        --l;
      }
      else if (k > 0 && dp[k][l] == dp[k - 1][l] + GAP)
      {
        alignedSeq1.push_back(seq1[k - 1]);
        alignedSeq2.push_back(FILLER);
        --k;
      }
      else if (l > 0 && dp[k][l] == dp[k][l - 1] + GAP)
      {
        alignedSeq1.push_back(FILLER);
        alignedSeq2.push_back(seq2[l - 1]);
        --l;
      }
    }
    std::reverse(alignedSeq1.begin(), alignedSeq1.end());
    std::reverse(alignedSeq2.begin(), alignedSeq2.end());

    return make_pair(alignedSeq1, alignedSeq2);
  }
}

alignment align_file_variants(std::vector<file_variant>& variants,
                              const options&             options)
{
  std::vector<std::reference_wrapper<const std::vector<size_t>>> ngram_hashes;
  for (const auto& variant : variants)
  {
    ngram_hashes.push_back(*variant.hashed_ngrams);
  }

  auto most_similar_pair_indices { find_most_similar_pair(ngram_hashes,
                                                          options) };

  // align it
  // in a loop:
  // -- find next most similar pair
  // -- align it
}
