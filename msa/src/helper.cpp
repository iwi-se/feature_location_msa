#include "helper.hpp"
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

void print_guide_tree_node(const std::shared_ptr<guide_tree_node>& node,
                           const file_family&                      family,
                           const std::string&                      prefix,
                           bool                                    is_last)
{
  std::cout << prefix << (is_last ? "└── " : "├── ");

  if (node->is_leaf())
  {
    size_t idx = node->variant_index.value();
    std::cout << "leaf[" << family.variants[idx].variant << "]";
  }
  else
  {
    std::cout << "node";
  }

  std::cout << " (size=" << node->size << ", sim=" << node->similarity << ")\n";

  std::string child_prefix = prefix + (is_last ? "    " : "│   ");

  if (node->left)
  {
    print_guide_tree_node(
        node->left, family, child_prefix, node->right == nullptr);
  }

  if (node->right)
  {
    print_guide_tree_node(node->right, family, child_prefix, true);
  }
}

void print_guide_tree(const guide_tree& tree, const file_family& family)
{
  if (!tree.root)
  {
    std::cout << "<empty guide tree>\n";
    return;
  }

  print_guide_tree_node(tree.root, family, "", true);
}

variant_dedup_groups
    group_variants_by_ast(const std::vector<file_variant>& variants)
{
  variant_dedup_groups groups;
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
