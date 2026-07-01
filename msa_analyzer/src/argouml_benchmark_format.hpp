#pragma once
#include "tree.hpp"
#include <set>
#include <string>
#include <vector>

struct output_line_t
{
    bool is_class_line() const
    {
      return method_fqn.empty() && !is_refinement;
    }

    bool is_method_line() const
    {
      return !class_fqn.empty() && !method_fqn.empty() && !is_refinement;
    }

    std::string class_fqn;
    std::string method_fqn;
    bool        is_refinement;
};

bool operator== (const output_line_t &a, const output_line_t &b);
bool operator< (const output_line_t &a, const output_line_t &b);

class output_lines_t
{
  public:
    void        insert(const output_line_t &line);
    void        insert_many(const output_lines_t &other);
    void        remove_superfluous_lines();
    std::string render();

    std::set<output_line_t> class_lines;
    std::set<output_line_t> method_lines;
    std::set<output_line_t> refinement_lines;
};

output_lines_t build_argouml_benchmark_format_for_file(
    std::vector<std::shared_ptr<node_t>> included_tokens);
