#include "guide_tree.hpp"
#include "core.hpp"
#include "helper.hpp"
#include <cmath>
#include <memory>
#include <unordered_set>
#include <vector>

struct cluster
{
    std::shared_ptr<guide_tree_node> node;
    std::vector<size_t>              members; // variant indices
    std::vector<size_t> merged_ngrams;        // cached union representation
};

std::vector<size_t> merge_ngrams(const std::vector<size_t>& a,
                                 const std::vector<size_t>& b)
{
  std::unordered_set<size_t> s(a.begin(), a.end());
  s.insert(b.begin(), b.end());
  return std::vector<size_t>(s.begin(), s.end());
}

double cluster_similarity(const cluster& a, const cluster& b)
{
  return (a.merged_ngrams == b.merged_ngrams
              ? std::numeric_limits<double>::max()
              : static_cast<double>(
                    count_common_ngrams(a.merged_ngrams, b.merged_ngrams)));
}

void build_guide_tree(file_family& family, const options& options)
{
  const size_t n = family.variants.size();
  if (n == 0)
  {
    family.m_guide_tree = guide_tree {};
    return;
  }

  // ---- init clusters (leaves) ----
  std::vector<cluster> clusters;
  clusters.reserve(n);

  std::vector<std::shared_ptr<guide_tree_node>> leaf_nodes;
  leaf_nodes.reserve(n);

  for (size_t i = 0; i < n; ++i)
  {
    auto node           = std::make_shared<guide_tree_node>();
    node->variant_index = i;
    node->size          = 1;
    node->similarity    = 1.0;

    clusters.push_back(
        cluster { .node          = node,
                  .members       = { i },
                  .merged_ngrams = family.variants[i].hashed_ngrams.value() });

    leaf_nodes.push_back(node);
  }

  size_t next_id = n;

  // ---- agglomerative clustering ----
  while (clusters.size() > 1)
  {
    double best_sim = -1.0;
    size_t best_i = 0, best_j = 1;

    for (size_t i = 0; i < clusters.size(); ++i)
    {
      for (size_t j = i + 1; j < clusters.size(); ++j)
      {
        double sim = cluster_similarity(clusters[i], clusters[j]);
        if (sim > best_sim)
        {
          best_sim = sim;
          best_i   = i;
          best_j   = j;
        }
      }
    }

    auto& A = clusters[best_i];
    auto& B = clusters[best_j];

    // ---- create new internal node ----
    auto parent        = std::make_shared<guide_tree_node>();
    parent->id         = next_id++;
    parent->left       = A.node;
    parent->right      = B.node;
    parent->similarity = best_sim;
    parent->size       = A.node->size + B.node->size;

    // ---- merge cluster metadata ----
    cluster merged;
    merged.node    = parent;
    merged.members = A.members;
    merged.members.insert(
        merged.members.end(), B.members.begin(), B.members.end());

    merged.merged_ngrams = merge_ngrams(A.merged_ngrams, B.merged_ngrams);

    // erase in correct order
    if (best_i > best_j)
    {
      std::swap(best_i, best_j);
    }

    clusters.erase(clusters.begin() + best_j);
    clusters.erase(clusters.begin() + best_i);

    clusters.push_back(std::move(merged));
  }

  // ---- final tree ----
  guide_tree tree;
  tree.root   = clusters.front().node;
  tree.leaves = leaf_nodes;

  for (size_t i = 0; i < leaf_nodes.size(); ++i)
  {
    tree.leaf_map[i] = leaf_nodes[i];
  }

  family.m_guide_tree = std::move(tree);
}
