#include "core.hpp"
#include <vector>

void print_token_vector(const std::vector<alignment_token>& vec);

size_t count_common_ngrams(const std::vector<size_t>& a,
                           const std::vector<size_t>& b);

void print_guide_tree(const guide_tree& tree, const file_family& family);
