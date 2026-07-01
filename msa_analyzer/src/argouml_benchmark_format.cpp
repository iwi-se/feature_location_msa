#include "argouml_benchmark_format.hpp"
#include "tree.hpp"
#include <algorithm>
#include <set>
#include <stack>
#include <string>
#include <vector>

const std::string refinement_suffix { "Refinement" };

enum class trace_extent_t
{
  none,
  refinement,
  full
};

trace_extent_t is_trace(std::shared_ptr<node_t>              node,
                        const std::vector<std::shared_ptr<node_t>> &other_nodes)
{
  auto result { trace_extent_t::full };
  for (const auto &other_node : other_nodes)
  {
    if (node == other_node)
    {
      return trace_extent_t::none;
    }
    if (other_node->is_ancestor_of(node))
    {
      return trace_extent_t::none;
    }
    if (node->is_ancestor_of(other_node))
    {
      result = trace_extent_t::refinement;
    }
  }
  return result;
}

bool is_method_declaration(std::shared_ptr<node_t> node)
{
  return node != nullptr
         && (node->get_tag() == "method_declaration"
             || node->get_tag() == "constructor_declaration");
}

bool is_class_declaration(std::shared_ptr<node_t> node)
{
  return node != nullptr
         && (node->get_tag() == "class_declaration"
             || node->get_tag() == "interface_declaration");
}

bool is_import_declaration(std::shared_ptr<node_t> node)
{
  return node != nullptr && node->get_tag() == "import_declaration";
}

bool is_comment(std::shared_ptr<node_t> node)
{
  return node != nullptr
         && (node->get_tag() == "block_comment"
             || node->get_tag() == "line_comment");
}

std::string get_identifier(std::shared_ptr<node_t> node)
{
  auto identifier { node->get_child_by_tag("identifier") };
  if (identifier == nullptr)
  {
    return "";
  }
  return identifier->get_ts_text();
}

std::shared_ptr<node_t> get_parent_method_node(std::shared_ptr<node_t> node)
{
  std::shared_ptr<node_t> current_method_node { nullptr };
  while (node != nullptr)
  {
    if (is_method_declaration(node))
    {
      current_method_node = node;
    }
    node = node->get_parent();
  }
  return current_method_node;
}

std::shared_ptr<node_t> get_parent_class_node(std::shared_ptr<node_t> node)
{
  while (node != nullptr && !is_class_declaration(node))
  {
    node = node->get_parent();
  }
  return node;
}

std::vector<std::shared_ptr<node_t>> get_top_level_class_nodes(std::shared_ptr<node_t> node)
{
  std::vector<std::shared_ptr<node_t>> result;
  std::stack<std::shared_ptr<node_t>>  stack;
  stack.push(node);
  while (!stack.empty())
  {
    auto current { stack.top() };
    stack.pop();
    if (is_class_declaration(current))
    {
      result.push_back(current);
    }
    else
    {
      for (const auto &child : current->get_children())
      {
        stack.push(child);
      }
    }
  }
  return result;
}

std::string get_class_fqn(std::shared_ptr<node_t> node)
{
  if (!is_class_declaration(node))
  {
    return "";
  }
  // Get to the program node
  std::vector<std::string> class_fqns;
  while (node != nullptr && node->get_tag() != "program")
  {
    if (is_class_declaration(node))
    {
      class_fqns.push_back(get_identifier(node));
    }
    node = node->get_parent();
  }

  std::string identifier {};
  for (auto it = class_fqns.rbegin(); it != class_fqns.rend(); ++it)
  {
    if (it != class_fqns.rbegin())
    {
      identifier += ".";
    }
    identifier += *it;
  }
  if (node == nullptr)
  {
    return identifier;
  }

  // Find package declaration
  auto package_declaration = node->get_child_by_tag("package_declaration");
  if (package_declaration == nullptr)
  {
    return identifier;
  }

  // Get all identifiers from package declaration
  std::vector<std::string>  package_parts;
  std::shared_ptr<node_t>   current = package_declaration;
  while (current != nullptr)
  {
    auto                    &children { current->get_children() };
    std::vector<std::string> this_level_parts;
    for (const auto &child : children)
    {
      if (child->get_tag() == "identifier")
      {
        this_level_parts.push_back(child->get_ts_text());
      }
    }
    package_parts.insert(package_parts.begin(),
                         this_level_parts.begin(),
                         this_level_parts.end());
    current = current->get_child_by_tag("scoped_identifier");
  }

  // Combine package parts with dots
  std::string package_name;
  for (size_t i = 0; i < package_parts.size(); ++i)
  {
    if (i > 0)
    {
      package_name += ".";
    }
    package_name += package_parts[i];
  }

  return package_name.empty() ? identifier : package_name + "." + identifier;
}

std::string get_method_fqn(std::shared_ptr<node_t> node)
{
  if (!is_method_declaration(node))
  {
    return "";
  }
  auto identifier { get_identifier(node) };

  // Get parameter types
  std::vector<std::string> param_types;
  auto formal_params = node->get_child_by_tag("formal_parameters");
  if (formal_params != nullptr)
  {
    for (const auto &child : formal_params->get_children())
    {
      if (child->get_tag() == "formal_parameter")
      {
        auto type_identifier = child->get_children()[0];
        if (type_identifier->get_tag() == "modifiers")
        {
          type_identifier = child->get_children()[1];
        }
        if (type_identifier != nullptr)
        {
          param_types.push_back(type_identifier->get_ts_text());
        }
      }
    }
  }

  std::string method_fqn = identifier + "(";
  for (size_t i = 0; i < param_types.size(); ++i)
  {
    if (i > 0)
    {
      method_fqn += ",";
    }
    param_types[i].erase(
        std::remove(param_types[i].begin(), param_types[i].end(), ' '),
        param_types[i].end());
    method_fqn += param_types[i];
  }
  method_fqn += ")";

  return method_fqn;
}

bool operator== (const output_line_t &a, const output_line_t &b)
{
  return a.class_fqn == b.class_fqn && a.method_fqn == b.method_fqn
         && a.is_refinement == b.is_refinement;
}

bool operator< (const output_line_t &a, const output_line_t &b)
{
  return a.class_fqn < b.class_fqn
         || (a.class_fqn == b.class_fqn && a.method_fqn < b.method_fqn);
}

void output_lines_t::insert(const output_line_t &line)
{
  if (line.is_class_line())
  {
    class_lines.insert(line);
  }
  else if (line.is_method_line())
  {
    method_lines.insert(line);
  }
  else
  {
    refinement_lines.insert(line);
  }
}

void output_lines_t::insert_many(const output_lines_t &other)
{
  for (const auto &line : other.class_lines)
  {
    insert(line);
  }
  for (const auto &line : other.method_lines)
  {
    insert(line);
  }
  for (const auto &line : other.refinement_lines)
  {
    insert(line);
  }
}

void output_lines_t::remove_superfluous_lines()
{
  std::set<output_line_t> new_output {};
  for (const auto &class_line : class_lines)
  {
    bool all_true { true };
    for (const auto &classline_ : class_lines)
    {
      if (class_line.class_fqn.starts_with(classline_.class_fqn + "."))
      {
        all_true = false;
        break;
      }
    }
    if (all_true)
    {
      new_output.insert(class_line);
    }
  }
  class_lines = new_output;

  for (const auto &class_line : class_lines)
  {
    std::erase_if(method_lines,
                  [&class_line](const output_line_t &line)
                  {
                    return line.class_fqn.starts_with(class_line.class_fqn
                                                      + ".")
                           || line.class_fqn == class_line.class_fqn;
                  });
    std::erase_if(refinement_lines,
                  [&class_line](const output_line_t &line)
                  {
                    return line.class_fqn.starts_with(class_line.class_fqn
                                                      + ".")
                           || line.class_fqn == class_line.class_fqn;
                  });
  }

  for (const auto &method_line : method_lines)
  {
    std::erase_if(refinement_lines,
                  [&method_line](const output_line_t &line)
                  {
                    return line.class_fqn == method_line.class_fqn
                           && line.method_fqn == method_line.method_fqn;
                  });
  }
}

std::string output_lines_t::render()
{
  remove_superfluous_lines();
  std::string output;
  for (auto &line : class_lines)
  {
    output += line.class_fqn + "\n";
  }
  for (const auto &line : method_lines)
  {
    output += line.class_fqn + " " + line.method_fqn + "\n";
  }
  for (const auto &line : refinement_lines)
  {
    output += line.class_fqn
              + (line.method_fqn.empty() ? "" : " " + line.method_fqn) + " "
              + refinement_suffix + "\n";
  }
  return output;
}

std::vector<std::shared_ptr<node_t>> find_all_class_nodes(std::shared_ptr<node_t> root)
{
  std::vector<std::shared_ptr<node_t>> class_nodes;
  std::stack<std::shared_ptr<node_t>>  stack;
  stack.push(root);
  while (!stack.empty())
  {
    auto current { stack.top() };
    stack.pop();
    if (is_class_declaration(current))
    {
      class_nodes.push_back(current);
    }
    for (const auto &child : current->get_children())
    {
      stack.push(child);
    }
  }
  return class_nodes;
}

// Checks if a leaf is the identifier of a class node
bool is_class_identifier(std::shared_ptr<node_t> n)
{
  return n->get_tag() == "identifier" && is_class_declaration(n->get_parent());
}

bool is_method_identifier(std::shared_ptr<node_t> n)
{
  return n->get_tag() == "identifier" && is_method_declaration(n->get_parent());
}

std::vector<std::shared_ptr<node_t>> find_all_method_nodes(std::shared_ptr<node_t> root)
{
  std::vector<std::shared_ptr<node_t>> class_nodes;
  std::stack<std::shared_ptr<node_t>>  stack;
  stack.push(root);
  while (!stack.empty())
  {
    auto current { stack.top() };
    stack.pop();
    if (is_method_declaration(current))
    {
      class_nodes.push_back(current);
    }
    for (const auto &child : current->get_children())
    {
      stack.push(child);
    }
  }
  return class_nodes;
}

std::vector<std::shared_ptr<node_t>> find_all_import_nodes(std::shared_ptr<node_t> root)
{
  std::vector<std::shared_ptr<node_t>> class_nodes;
  std::stack<std::shared_ptr<node_t>>  stack;
  stack.push(root);
  while (!stack.empty())
  {
    auto current { stack.top() };
    stack.pop();
    if (is_import_declaration(current))
    {
      class_nodes.push_back(current);
    }
    for (const auto &child : current->get_children())
    {
      stack.push(child);
    }
  }
  return class_nodes;
}

output_lines_t find_full_traces(const std::vector<std::shared_ptr<node_t>> &included_nodes)
{
  output_lines_t output_lines {};

  if (included_nodes.empty())
  {
    return output_lines;
  }

  auto class_nodes { find_all_class_nodes(included_nodes[0]->get_root()) };

  for (const auto &class_node : class_nodes)
  {
    bool is_fully_included { false };
    for (auto &leaf_weak : class_node->get_leaves())
    {
      auto leaf = leaf_weak.lock();
      if (!leaf)
        continue;
      if (is_class_identifier(leaf)
          && std::find(included_nodes.begin(), included_nodes.end(), leaf)
                 != included_nodes.end())
      {
        is_fully_included = true;
        break;
      }
    }
    if (is_fully_included)
    {
      auto class_identifier { get_class_fqn(class_node) };
      output_lines.insert({ class_identifier, "", false });
    }
  }

  auto method_nodes { find_all_method_nodes(included_nodes[0]->get_root()) };

  for (const auto &method_node : method_nodes)
  {
    bool is_fully_included { false };
    for (auto &leaf_weak : method_node->get_leaves())
    {
      auto leaf = leaf_weak.lock();
      if (!leaf)
        continue;
      if (is_method_identifier(leaf) && leaf->get_parent() == method_node
          && is_class_declaration(
              leaf->get_parent()->get_parent()->get_parent())
          && std::find(included_nodes.begin(), included_nodes.end(), leaf)
                 != included_nodes.end())
      {
        is_fully_included = true;
        break;
      }
    }
    if (is_fully_included)
    {
      auto class_identifier { get_class_fqn(
          get_parent_class_node(method_node)) };
      auto method_identifier { get_method_fqn(method_node) };
      output_lines.insert({ class_identifier, method_identifier, false });
    }
  }

  return output_lines;
}

output_lines_t
    find_refinement_traces(const std::vector<std::shared_ptr<node_t>> &included_tokens)
{
  output_lines_t output_lines;

  if (included_tokens.empty())
  {
    return output_lines;
  }

  auto import_declarations { find_all_import_nodes(
      included_tokens[0]->get_root()) };
  for (auto &import_declaration : import_declarations)
  {
    auto &leaves { import_declaration->get_leaves() };
    bool  is_trace_l { false };
    for (auto &leaf_weak : leaves)
    {
      auto leaf = leaf_weak.lock();
      if (!leaf)
        continue;
      if (std::find(included_tokens.begin(), included_tokens.end(), leaf)
          != included_tokens.end())
      {
        is_trace_l = true;
        break;
      }
    }

    if (is_trace_l)
    {
      auto class_nodes { get_top_level_class_nodes(
          included_tokens[0]->get_root()) };

      for (const auto &class_node : class_nodes)
      {
        output_lines.insert({ get_class_fqn(class_node), "", true });
      }
      break;
    }
  }

  for (const auto &token : included_tokens)
  {
    if (!is_comment(token))
    {
      auto method_node { get_parent_method_node(token) };
      auto class_node { get_parent_class_node(token) };

      std::string method_identifier {};
      if (method_node != nullptr)
      {
        method_identifier = get_method_fqn(method_node);
      }
      std::string class_identifier { get_class_fqn(class_node) };
      if (!class_identifier.empty() || !method_identifier.empty())
      {
        output_lines.insert({ class_identifier, method_identifier, true });
      }
    }
  }
  // checkForImpreciseClassTraces(nodes, subtractionNodes, outputLines);
  return output_lines;
}

output_lines_t build_argouml_benchmark_format_for_file(
    std::vector<std::shared_ptr<node_t>> included_tokens)
{
  output_lines_t full_trace_output_lines { find_full_traces(included_tokens) };
  output_lines_t refinement_output_lines { find_refinement_traces(
      included_tokens) };
  full_trace_output_lines.insert_many(refinement_output_lines);
  return full_trace_output_lines;
}
