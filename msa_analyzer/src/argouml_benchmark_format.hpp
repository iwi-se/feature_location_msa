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
    // True iff every piece of evidence for this line came from a node whose
    // *own* feature is a disjunction (e.g. "A or B") that merely contains
    // the target feature as one clause, rather than a node genuinely and
    // exclusively belonging to the target feature. Such a line must not be
    // treated as redundant just because a coarser (e.g. class-level) line
    // covers it, since it still carries information not implied by the
    // coarser line. See output_lines_t::remove_superfluous_lines().
    bool or_derived_only { false };
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

std::shared_ptr<node_t> get_parent_method_node(std::shared_ptr<node_t> node);
std::shared_ptr<node_t> get_parent_class_node(std::shared_ptr<node_t> node);
std::string             get_identifier(std::shared_ptr<node_t> node);
std::string             get_method_fqn(std::shared_ptr<node_t> node);
bool                    is_class_identifier(std::shared_ptr<node_t> n);
bool                    is_method_identifier(std::shared_ptr<node_t> n);
std::vector<std::shared_ptr<node_t>>
    get_top_level_class_nodes(std::shared_ptr<node_t> node);

output_lines_t build_argouml_benchmark_format_for_file(
    std::vector<std::shared_ptr<node_t>> included_tokens,
    std::vector<std::shared_ptr<node_t>> all_roots,
    const std::string                   &target_feature);
