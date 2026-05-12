#include "alignment.hpp"
#include "arguments.hpp"
#include <unordered_set>

size_t count_common_ngrams(const file_variant& a, const file_variant& b)
{
  std::unordered_set<size_t> ngram_hashes_set { (*a.hashed_ngrams).begin(),
                                                (*a.hashed_ngrams).end() };

  size_t count {};
  for (const auto& ngram_hash : *b.hashed_ngrams)
  {
    if (ngram_hashes_set.count(ngram_hash))
    {
      ++count;
    }
  }
  return count;
}

std::pair<file_variant&, file_variant&>
    find_most_similar_pair(std::vector<file_variant>& variants,
                           const options&             options)
{
  size_t max_common {};
  size_t best_i { 0 }, best_j { 0 };

  for (size_t i = 0; i < variants.size(); ++i)
  {
    for (size_t j = i + 1; j < variants.size(); ++j)
    {
      size_t common = count_common_ngrams(variants[i], variants[j]);
      if (common > max_common)
      {
        max_common = common;
        best_i     = i;
        best_j     = j;
      }
    }
  }

  return { variants[best_i], variants[best_j] };
}

alignment align_file_variants(std::vector<file_variant>& variants)
{
  // find most similar pair
  // align it
  // in a loop:
  // -- find next most similar pair
  // -- align it
}
