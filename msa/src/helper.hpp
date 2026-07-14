#include "core.hpp"
#include <unordered_map>
#include <vector>

void print_token_vector(const std::vector<alignment_token>& vec);

size_t count_common_ngrams(const std::vector<size_t>& a,
                           const std::vector<size_t>& b);

void print_guide_tree(const guide_tree& tree, const file_family& family);

// Groups rows of a file_family's variants by AST pointer identity. Rows
// whose source files are byte-identical share the same AST (load_asts
// interns them), so grouping this way finds duplicate rows without a
// content comparison.
struct variant_dedup_groups
{
    // One entry per distinct AST, ascending original-row-index order.
    std::vector<size_t> distinct_indices;
    // row -> representative row (identity for representatives themselves).
    std::unordered_map<size_t, size_t> representative_of;
    // representative row -> all rows in its group (representative included).
    std::unordered_map<size_t, std::vector<size_t>> members_of;
};

variant_dedup_groups
    group_variants_by_ast(const std::vector<file_variant>& variants);
