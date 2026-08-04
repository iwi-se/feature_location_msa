#include "helper.hpp"
#include <algorithm>
#include <iostream>
#include <unordered_set>

void print_token_vector(const std::vector<alignment_token>& vec)
{
  for (const auto& tok : vec)
  {
    if (tok.is_filler())
    {
      std::cout << "FILLER";
    }
    else
    {
      std::cout << tok.node->get_ts_text();
    }
  }
  std::cout << "\n" << std::endl;
}

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

double file_similarity(const std::vector<size_t>& a,
                       const std::vector<size_t>& b)
{
  double ngrams { static_cast<double>(count_common_ngrams(a, b)) };
  return ngrams / static_cast<double>(std::max(a.size(), b.size()));
}

variant_dedup_groups
    group_variants_by_ast(const std::vector<file_variant>& variants)
{
  variant_dedup_groups                groups;
  std::unordered_map<node_t*, size_t> representative_for_ast;

  for (size_t i {}; i < variants.size(); ++i)
  {
    node_t* ast_ptr { variants[i].ast->get() };
    auto [it, inserted] { representative_for_ast.try_emplace(ast_ptr, i) };
    if (inserted)
    {
      groups.distinct_indices.push_back(i);
    }
    groups.representative_of[i] = it->second;
    groups.members_of[it->second].push_back(i);
  }

  return groups;
}
