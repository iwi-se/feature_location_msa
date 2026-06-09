#include "tree.hpp"
#include <map>
#include <string>
#include <vector>

std::map<std::string, std::string> build_argouml_benchmark_format(
    std::map<std::string, std::vector<node_t*>> included_tokens_per_file);
