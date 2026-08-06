#include "argouml_benchmark_format.hpp"
#include "parser.hpp"
#include "tree.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <oneapi/tbb/global_control.h>
#include <regex>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <thread>
#include <utility>
#include <vector>

using namespace std::string_literals;

void replace_all(std::string       &str,
                 const std::string &from,
                 const std::string &to)
{
  if (from.empty())
  {
    return; // avoid infinite loop
  }

  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos)
  {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // move past the replaced part
  }
}

struct alignment_token_t
{
    enum class token_kind_t
    {
      node_t,
      filler_t
    } token_kind;
    std::shared_ptr<node_t> node { nullptr };

    bool is_filler() const
    {
      return token_kind == token_kind_t::filler_t;
    }

    bool is_node() const
    {
      return token_kind == token_kind_t::node_t;
    }
};

alignment_token_t make_node_token(std::weak_ptr<node_t> n)
{
  return { alignment_token_t::token_kind_t::node_t, n.lock() };
}

const alignment_token_t filler_t { alignment_token_t::token_kind_t::filler_t };

using spl_file_t = std::string;
using system_t   = size_t;

struct system_tokens_t
{
    std::shared_ptr<node_t>        root;
    std::vector<alignment_token_t> tokens;
};

struct msa_representation_t
{
    std::string                                               lang {};
    std::map<spl_file_t, std::map<system_t, system_tokens_t>> internal_rep {};
};

bool parse_system_msa(
    std::ifstream                                  &file,
    std::pair<system_t, system_tokens_t>           &out,
    const std::string                              &lang,
    std::map<std::string, std::shared_ptr<node_t>> &tree_cache,
    const std::set<std::string>                    &atomic_types,
    const std::map<std::string, size_t>            &variant_id_map)
{
  std::string system_name {};
  size_t      system_index {};
  if (file)
  {
    std::getline(file, system_name);
    if (system_name.empty())
    {
      return false;
    }
    if (!variant_id_map.empty())
    {
      auto it { variant_id_map.find(system_name) };
      if (it == variant_id_map.end())
      {
        std::cerr << "system id map: no entry for variant '" << system_name
                  << "'\n";
        exit(1);
      }
      system_index = it->second;
    }
    else
    {
      try
      {
        system_index = std::stoul(system_name);
      }
      catch (const std::invalid_argument &e)
      {
        return false;
      }
    }
  }
  else
  {
    return false;
  }

  std::string system_file {};
  if (file)
  {
    std::getline(file, system_file);
  }
  else
  {
    return false;
  }

  std::shared_ptr<node_t> tree;
  auto                    cache_it = tree_cache.find(system_file);
  if (cache_it != tree_cache.end())
  {
    tree = cache_it->second;
  }
  else
  {
    tree                    = parse_file(system_file, lang, atomic_types);
    tree_cache[system_file] = tree;
  }
  auto leaves { tree->get_leaves() };

  size_t                         i {};
  std::vector<alignment_token_t> tokens {};
  while (file && (file.peek() == '1' || file.peek() == '0'))
  {
    int is_node { file.get() };
    if (is_node == '0')
    {
      tokens.push_back(filler_t);
    }
    else if (is_node == '1')
    {
      tokens.push_back(make_node_token(leaves[i]));
      ++i;
    }
    else
    {
      std::cerr << "Unexpected Input " << file.peek() << std::endl;
      return false;
    }
  }

  if (file.peek() == '\n')
  {
    file.get();
  }

  out = std::make_pair(system_index,
                       system_tokens_t { std::move(tree), tokens });

  return true;
}

std::string get_lang_from_file_path(const std::filesystem::path &msa_file)
{
  auto suffix { msa_file.extension().string() };
  if (suffix == ".cpp"s || suffix == ".hpp"s)
  {
    return "cpp";
  }
  if (suffix == ".java"s)
  {
    return "java";
  }
  std::cerr << "Unknown extension: " << suffix << std::endl;
  return "Unknown language";
}

std::pair<spl_file_t, std::map<system_t, system_tokens_t>>
    parse_file_msa(const std::filesystem::path         &msa_file,
                   const std::set<std::string>         &atomic_types,
                   const std::map<std::string, size_t> &variant_id_map)
{
  std::ifstream file(msa_file); // open file for reading

  if (!file.is_open())
  {
    std::cerr << "Failed to open file.\n";
    exit(1);
  }

  spl_file_t spl_file {};
  std::getline(file, spl_file);

  std::string lang { get_lang_from_file_path(spl_file) };
  std::map<system_t, system_tokens_t>            systems {};
  std::map<std::string, std::shared_ptr<node_t>> tree_cache {};

  std::pair<system_t, system_tokens_t> system {};
  while (parse_system_msa(
      file, system, lang, tree_cache, atomic_types, variant_id_map))
  {
    systems.insert(std::move(system));
  }

  file.close();
  return std::make_pair(spl_file, std::move(systems));
}

msa_representation_t
    parse_directory_msa(const std::filesystem::path         &msa_dir,
                        const std::set<std::string>         &atomic_types,
                        const std::map<std::string, size_t> &variant_id_map)
{
  msa_representation_t msa {};
  for (const auto &dir_entry : std::filesystem::directory_iterator(msa_dir))
  {
    if (dir_entry.path().extension() != ".output")
    {
      continue;
    }
    auto file_msa { parse_file_msa(dir_entry, atomic_types, variant_id_map) };
    if (msa.lang == "")
    {
      msa.lang = get_lang_from_file_path(file_msa.first);
    }
    msa.internal_rep.emplace(std::move(file_msa));
  }
  return msa;
}

msa_representation_t
    parse_msa(const std::filesystem::path         &msa_file,
              const std::set<std::string>         &atomic_types,
              const std::map<std::string, size_t> &variant_id_map)
{
  if (std::filesystem::is_directory(msa_file))
  {
    return parse_directory_msa(msa_file, atomic_types, variant_id_map);
  }
  else
  {
    msa_representation_t msa {};
    auto file_msa { parse_file_msa(msa_file, atomic_types, variant_id_map) };
    msa.lang = get_lang_from_file_path(file_msa.first);
    msa.internal_rep.emplace(std::move(file_msa));
    return msa;
  }
}

struct operation_t
{
    enum class operation_type_t
    {
      print_system_names,
      analyze,
      render
    } operation_type;
    std::filesystem::path              msa_path {};
    std::filesystem::path              isolation_executable {};
    std::filesystem::path              spl_specification_file {};
    size_t                             threads { 0 };
    std::set<std::string>              atomic_node_types {};
    std::map<std::string, std::vector<std::string>> feature_expression_lookup {};
    std::map<std::string, size_t>      variant_name_to_system_id {};
};

void argument_error(char *argv[])
{
  std::cerr
      << "Usage: \n"
      << argv[0]
      << " analyze <msa_outputs> <isolation_exe> <spl_spec> [--threads "
         "N] [--atomic-types-file FILE] [--feature-expressions-file FILE] "
         "[--system-id-map FILE]\n"
      << argv[0]
      << " render <msa_outputs> <isolation_exe> <spl_spec> "
         "[--atomic-types-file FILE] [--feature-expressions-file FILE] "
         "[--system-id-map FILE]\n"
      << argv[0] << " printSystemNames <msa_outputs>\n";
  exit(1);
}

size_t parse_threads_flag(int argc, char *argv[], int start)
{
  for (int i = start; i < argc - 1; ++i)
  {
    if (std::string(argv[i]) == "--threads")
    {
      try
      {
        return std::stoul(argv[i + 1]);
      }
      catch (...)
      {
        std::cerr << "--threads requires a positive integer\n";
        exit(1);
      }
    }
  }
  return std::thread::hardware_concurrency();
}

std::set<std::string> parse_atomic_types_file(const std::filesystem::path &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    std::cerr << "--atomic-types-file: could not open \"" << path.string()
              << "\"\n";
    exit(1);
  }

  std::set<std::string> atomic_types;
  std::string           line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    if (line.empty() || line.starts_with('#'))
    {
      continue;
    }
    atomic_types.insert(line);
  }
  return atomic_types;
}

std::set<std::string> parse_atomic_types_flag(int argc, char *argv[], int start)
{
  for (int i = start; i < argc - 1; ++i)
  {
    if (std::string(argv[i]) == "--atomic-types-file")
    {
      return parse_atomic_types_file(argv[i + 1]);
    }
  }
  return {};
}

std::map<std::string, size_t>
    parse_system_id_map_file(const std::filesystem::path &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    std::cerr << "--system-id-map: could not open \"" << path.string()
              << "\"\n";
    exit(1);
  }

  std::map<std::string, size_t> variant_id_map;
  std::string                   line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    if (line.empty() || line.starts_with('#'))
    {
      continue;
    }

    size_t comma_pos { line.find(',') };
    if (comma_pos == std::string::npos)
    {
      std::cerr << "--system-id-map: malformed line \"" << line << "\"\n";
      exit(1);
    }

    std::string variant_name { line.substr(0, comma_pos) };
    std::string id_part { line.substr(comma_pos + 1) };
    try
    {
      variant_id_map[variant_name] = std::stoul(id_part);
    }
    catch (const std::exception &e)
    {
      std::cerr << "--system-id-map: malformed line \"" << line << "\"\n";
      exit(1);
    }
  }
  return variant_id_map;
}

std::map<std::string, size_t>
    parse_system_id_map_flag(int argc, char *argv[], int start)
{
  for (int i = start; i < argc - 1; ++i)
  {
    if (std::string(argv[i]) == "--system-id-map")
    {
      return parse_system_id_map_file(argv[i + 1]);
    }
  }
  return {};
}

std::string trim(std::string s)
{
  s.erase(0, s.find_first_not_of(" \t"));
  s.erase(s.find_last_not_of(" \t") + 1);
  return s;
}

std::string canonical_systems_key(std::vector<size_t> systems)
{
  std::sort(systems.begin(), systems.end());
  std::string result {};
  for (size_t i {}; i < systems.size(); ++i)
  {
    if (i > 0)
    {
      result += ",";
    }
    result += std::to_string(systems[i]);
  }
  return result;
}

std::map<std::string, std::vector<std::string>>
    parse_feature_expression_file(const std::filesystem::path &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    std::cerr << "--feature-expressions-file: could not open \""
              << path.string() << "\"\n";
    exit(1);
  }

  std::map<std::string, std::vector<std::string>> lookup;
  std::string                                     line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    if (line.empty())
    {
      continue;
    }

    size_t colon_pos { line.find(": ") };
    if (colon_pos == std::string::npos)
    {
      continue;
    }

    std::string         key_part { line.substr(0, colon_pos) };
    std::string         value_part { trim(line.substr(colon_pos + 2)) };
    std::vector<size_t> systems {};
    std::istringstream  key_stream(key_part);
    std::string         id_str {};
    while (std::getline(key_stream, id_str, ','))
    {
      id_str = trim(id_str);
      if (!id_str.empty())
      {
        systems.push_back(std::stoul(id_str));
      }
    }

    lookup[canonical_systems_key(systems)].push_back(value_part);
  }
  return lookup;
}

std::map<std::string, std::vector<std::string>>
    parse_feature_expressions_flag(int argc, char *argv[], int start)
{
  for (int i = start; i < argc - 1; ++i)
  {
    if (std::string(argv[i]) == "--feature-expressions-file")
    {
      return parse_feature_expression_file(argv[i + 1]);
    }
  }
  return {};
}

operation_t cli_arguments(int argc, char *argv[])
{
  if (argc < 3)
  {
    argument_error(argv);
  }

  std::string operation_type = argv[1];
  if (operation_type == "printSystemNames")
  {
    std::string msa_path { argv[2] };
    return operation_t { operation_t::operation_type_t::print_system_names,
                         msa_path,
                         {} };
  }
  else if (operation_type == "render")
  {
    if (argc < 5)
    {
      argument_error(argv);
    }
    std::string           msa_path { argv[2] };
    std::filesystem::path isolation_executable { argv[3] };
    std::filesystem::path spl_specification_file { argv[4] };
    std::set<std::string> atomic_node_types { parse_atomic_types_flag(
        argc, argv, 5) };
    std::map<std::string, std::vector<std::string>> feature_expression_lookup {
      parse_feature_expressions_flag(argc, argv, 5)
    };
    std::map<std::string, size_t> variant_name_to_system_id {
      parse_system_id_map_flag(argc, argv, 5)
    };
    return operation_t { operation_t::operation_type_t::render,
                         msa_path,
                         isolation_executable,
                         spl_specification_file,
                         0,
                         atomic_node_types,
                         feature_expression_lookup,
                         variant_name_to_system_id };
  }
  else if (operation_type == "analyze")
  {
    if (argc < 5)
    {
      argument_error(argv);
    }
    std::string           msa_path { argv[2] };
    std::filesystem::path isolation_executable { argv[3] };
    std::filesystem::path spl_specification_file { argv[4] };
    size_t                threads { parse_threads_flag(argc, argv, 5) };
    std::set<std::string> atomic_node_types { parse_atomic_types_flag(
        argc, argv, 5) };
    std::map<std::string, std::vector<std::string>> feature_expression_lookup {
      parse_feature_expressions_flag(argc, argv, 5)
    };
    std::map<std::string, size_t> variant_name_to_system_id {
      parse_system_id_map_flag(argc, argv, 5)
    };
    return operation_t { operation_t::operation_type_t::analyze,
                         msa_path,
                         isolation_executable,
                         spl_specification_file,
                         threads,
                         atomic_node_types,
                         feature_expression_lookup,
                         variant_name_to_system_id };
  }
  argument_error(argv);
  return operation_t {};
}

void print_systems(operation_t /*op*/) { }

std::vector<std::string> parse_block(const std::string &block,
                                     const char        &separator)
{
  std::vector<std::string> result {};
  char                     token {};
  std::string              current_token {};
  std::istringstream       iss(block);
  while (iss >> token)
  {
    if (token == '{' || token == ' ')
    {
      continue; // skip delimiters
    }
    if (token == separator || token == '}')
    {
      if (!current_token.empty())
      {
        result.push_back(current_token);
      }
      current_token = "";
      continue;
    }
    current_token.push_back(token);
  }
  return result;
}

// Main function: parse "block \ block"
std::pair<std::vector<std::string>, std::vector<std::string>>
    parse_expression(const std::string &input)
{
  std::string left, right;
  size_t      pos = input.find('\\'); // find backslash

  if (pos == std::string::npos)
  {
    left  = input;
    right = "{}"; // treat as empty right-hand side
  }
  else
  {
    left  = input.substr(0, pos);
    right = input.substr(pos + 1);
  }

  left  = trim(left);
  right = trim(right);

  return { parse_block(left, '&'), parse_block(right, '|') };
}

std::vector<std::vector<alignment_token_t>>
    intersect_tokens(std::vector<std::vector<alignment_token_t>>        a,
                     const std::vector<std::vector<alignment_token_t>> &b)
{
  if (a.empty() || b.empty())
  {
    return {};
  }

  std::vector<std::vector<alignment_token_t>> res {};
  for (size_t i {}; i < a.size(); ++i)
  {
    if (b[i].empty() || a[i].empty() || b[i][0].is_filler()
        || a[i][0].is_filler())
    {
      res.push_back({});
    }
    else
    {
      std::vector<alignment_token_t> temp;
      temp.insert(temp.end(), a[i].begin(), a[i].end());
      temp.insert(temp.end(), b[i].begin(), b[i].end());
      res.push_back(temp);
    }
  }
  return res;
}

std::vector<std::vector<alignment_token_t>>
    subtract_tokens(std::vector<std::vector<alignment_token_t>>        a,
                    const std::vector<std::vector<alignment_token_t>> &b)
{
  if (a.empty())
  {
    return {};
  }

  if (b.empty())
  {
    return a;
  }

  for (size_t i {}; i < a.size(); ++i)
  {
    if (!b[i].empty() && b[i][0].is_node())
    {
      a[i] = {};
    }
  }
  return a;
}

std::vector<std::vector<alignment_token_t>> get_system_tokens_or_empty(
    const std::map<std::string, system_tokens_t> &systems_for_file,
    const std::string                            &key)
{
  if (systems_for_file.find(key) != systems_for_file.end())
  {
    auto toks { systems_for_file.at(key).tokens };
    std::vector<std::vector<alignment_token_t>> result;
    for (auto &token : toks)
    {
      result.push_back({ token });
    }
    return result;
  }
  else
  {
    return {};
  }
}

std::vector<std::vector<alignment_token_t>> evaluate_expression_file(
    const std::pair<std::vector<std::string>, std::vector<std::string>>
                                                 &parsed_expression,
    const std::map<std::string, system_tokens_t> &systems_for_file)
{
  std::stack<std::string> lhs_stack {};
  for (const auto &s : parsed_expression.first)
  {
    lhs_stack.push(s);
  }

  std::string sys_name { lhs_stack.top() };
  lhs_stack.pop();
  std::vector<std::vector<alignment_token_t>> result {
    get_system_tokens_or_empty(systems_for_file, sys_name)
  };

  while (!lhs_stack.empty())
  {
    auto sys_name { lhs_stack.top() };
    lhs_stack.pop();
    result = intersect_tokens(
        result, get_system_tokens_or_empty(systems_for_file, sys_name));
  }

  std::stack<std::string> rhs_stack {};
  for (const auto &s : parsed_expression.second)
  {
    rhs_stack.push(s);
  }

  while (!rhs_stack.empty())
  {
    auto sys_name { rhs_stack.top() };
    rhs_stack.pop();
    result = subtract_tokens(
        result, get_system_tokens_or_empty(systems_for_file, sys_name));
  }

  return result;
}

std::vector<std::shared_ptr<node_t>>
    alignment_tokens_to_nodes(std::vector<alignment_token_t> tokens)
{
  std::vector<std::shared_ptr<node_t>> res;
  for (const auto &tok : tokens)
  {
    if (tok.is_node())
    {
      res.push_back(tok.node);
    }
  }
  return res;
}

// std::map<std::string, std::vector<node_t *>> evaluate_expression(
//     const std::pair<std::vector<std::string>, std::vector<std::string>>
//                                &parsed_expression,
//     const msa_representation_t &msa)
// {
//   std::map<std::string, std::vector<node_t *>> result;
//   for (const auto &spl_file : msa.internal_rep)
//   {
//     auto res { evaluate_expression_file(parsed_expression, spl_file.second)
//     }; std::vector<alignment_token_t> single_tokens {}; for (const auto &n :
//     res)
//     {
//       if (!n.empty())
//       {
//         single_tokens.push_back(n[0]);
//       }
//     }
//     auto nodes { alignment_tokens_to_nodes(single_tokens) };
//     result.insert(std::make_pair(spl_file.first, nodes));
//   }
//   return result;
// }

std::string transform_and_feature(const std::string &feat)
{
  const std::string        sep { " \xe2\x88\xa7 " };
  std::vector<std::string> parts {};
  size_t                   start {};
  size_t                   pos {};
  while ((pos = feat.find(sep, start)) != std::string::npos)
  {
    parts.push_back(feat.substr(start, pos - start));
    start = pos + sep.size();
  }
  parts.push_back(feat.substr(start));
  for (auto &p : parts)
  {
    replace_all(p, "\xc2\xac", "not_");
  }
  std::sort(parts.begin(), parts.end());
  std::string result {};
  for (size_t i {}; i < parts.size(); ++i)
  {
    if (i > 0)
    {
      result += "_and_";
    }
    result += parts[i];
  }
  return result;
}

std::vector<std::string> expand_or_feature(const std::string &feat)
{
  const std::string        sep { " \xe2\x88\xa8 " };
  std::vector<std::string> parts {};
  size_t                   start {};
  size_t                   pos {};
  while ((pos = feat.find(sep, start)) != std::string::npos)
  {
    parts.push_back(feat.substr(start, pos - start));
    start = pos + sep.size();
  }
  parts.push_back(feat.substr(start));
  return parts;
}

std::string strip_parens(const std::string &clause)
{
  std::string s { clause };
  s.erase(0, s.find_first_not_of(" \t"));
  s.erase(s.find_last_not_of(" \t") + 1);
  if (s.size() >= 2 && s.front() == '(' && s.back() == ')')
  {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

std::string transform_dnf_feature(const std::string &raw)
{
  std::vector<std::string> clauses { expand_or_feature(raw) };
  for (auto &clause : clauses)
  {
    clause = transform_and_feature(strip_parens(clause));
  }
  std::string result {};
  for (size_t i {}; i < clauses.size(); ++i)
  {
    if (i > 0)
    {
      result += " \xe2\x88\xa8 ";
    }
    result += clauses[i];
  }
  return result;
}

struct and_clause_literals_t
{
    std::set<std::string> positive {};
    size_t                negated_count {};
};

// Splits a single AND-clause that has already been canonicalized by
// transform_and_feature (literals joined by "_and_", negated literals
// prefixed with "not_") into its positive (non-negated) literal names, plus
// a count of how many literals in the clause were negated.
and_clause_literals_t
    literals_of_transformed_and_clause(const std::string &clause)
{
  const std::string        sep { "_and_" };
  std::vector<std::string> parts {};
  size_t                   start {};
  size_t                   pos {};
  while ((pos = clause.find(sep, start)) != std::string::npos)
  {
    parts.push_back(clause.substr(start, pos - start));
    start = pos + sep.size();
  }
  parts.push_back(clause.substr(start));

  and_clause_literals_t result {};
  for (auto &p : parts)
  {
    if (p.starts_with("not_"))
    {
      ++result.negated_count;
      continue;
    }
    result.positive.insert(p);
  }
  return result;
}

// Parses a transform_dnf_feature-canonicalized DNF string into one
// and_clause_literals_t per OR-clause.
std::vector<and_clause_literals_t>
    clause_literal_sets(const std::string &transformed_dnf)
{
  std::vector<and_clause_literals_t> result {};
  for (const auto &clause : expand_or_feature(transformed_dnf))
  {
    result.push_back(literals_of_transformed_and_clause(clause));
  }
  return result;
}

// Union of the positive literals across all OR-clauses of a
// transform_dnf_feature-canonicalized DNF string.
std::set<std::string> literal_union(const std::string &transformed_dnf)
{
  std::set<std::string> result {};
  for (const auto &clause_literals : clause_literal_sets(transformed_dnf))
  {
    result.insert(clause_literals.positive.begin(),
                  clause_literals.positive.end());
  }
  return result;
}

// Picks the raw candidate whose transformed literal set best matches
// parent_context: prefer a candidate with an OR-clause whose positive
// literals are a superset of parent_context, breaking ties by simplicity —
// fewest negated literals first (a "not" makes a clause less simple, even if
// it has fewer positive literals), then fewest extra positive literals
// (most specific match). Falls back to the first candidate if
// parent_context is empty or no candidate qualifies.
size_t pick_best_candidate_index(const std::vector<std::string> &raw_candidates,
                                 const std::set<std::string>    &parent_context)
{
  if (parent_context.empty())
  {
    return 0;
  }

  bool   found_best { false };
  size_t best_index { 0 };
  size_t best_negated { 0 };
  size_t best_extra { 0 };
  for (size_t i {}; i < raw_candidates.size(); ++i)
  {
    for (const auto &clause_literals :
        clause_literal_sets(transform_dnf_feature(raw_candidates[i])))
    {
      if (!std::includes(clause_literals.positive.begin(),
                         clause_literals.positive.end(),
                         parent_context.begin(),
                         parent_context.end()))
      {
        continue;
      }
      size_t extra { clause_literals.positive.size() - parent_context.size() };
      size_t negated { clause_literals.negated_count };
      if (!found_best || negated < best_negated
          || (negated == best_negated && extra < best_extra))
      {
        found_best   = true;
        best_index   = i;
        best_negated = negated;
        best_extra   = extra;
      }
    }
  }
  return found_best ? best_index : 0;
}

void print_nodes(const std::vector<std::shared_ptr<node_t>> &nodes)
{
  for (const auto &node : nodes)
  {
    std::cout << node->get_ts_text() << " ";
  }
  std::cout << std::endl;
}

void print_results_per_file(
    const std::map<std::string, std::vector<std::shared_ptr<node_t>>>
        &results_per_file)
{
  for (const auto &file_result : results_per_file)
  {
    std::cout << file_result.first << std::endl;
    print_nodes(file_result.second);
  }
}

std::string exec_and_capture(const std::string &command)
{
  std::array<char, 4096> buffer;
  std::string            result;

  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
  {
    throw std::runtime_error("popen() failed");
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
  {
    result += buffer.data();
  }

  int rc = pclose(pipe);
  (void)rc; // Optional: inspect exit status

  return result;
}

std::string build_isolation_call(const operation_t &operation,
                                 const std::string &systems_hash)
{
  std::string result {};

  result += operation.isolation_executable;
  result += " ";

  result += "expr ";
  result += operation.spl_specification_file;
  result += " ";
  result += systems_hash;

  return result;
}

std::string hash_systems(const std::vector<size_t> &systems)
{
  std::string result {};
  for (const auto &system : systems)
  {
    result += std::to_string(system);
    result += " ";
  }
  return result;
}

std::vector<std::string>
    get_feature_candidates_from_systems(const std::vector<size_t> &systems,
                                        const operation_t         &operation)
{
  static std::map<std::string, std::vector<std::string>> system_feature_map {};
  static std::mutex                                       cache_mutex {};
  std::string systems_hash { hash_systems(systems) };

  {
    std::lock_guard lock { cache_mutex };
    if (system_feature_map.contains(systems_hash))
    {
      return system_feature_map[systems_hash];
    }
  }

  std::vector<std::string> result {};
  auto                     lookup_it { operation.feature_expression_lookup.find(
      canonical_systems_key(systems)) };
  if (lookup_it != operation.feature_expression_lookup.end())
  {
    result = lookup_it->second;
  }
  else
  {
    std::string isolation_call { build_isolation_call(operation,
                                                      systems_hash) };
    std::string exec_result { exec_and_capture(isolation_call) };
    exec_result.pop_back();
    result.push_back(exec_result);
  }

  {
    std::lock_guard lock { cache_mutex };
    system_feature_map.insert(std::make_pair(systems_hash, result));
    return result;
  }
}

std::string get_feature_from_systems(const std::vector<size_t> &systems,
                                     const operation_t         &operation)
{
  return get_feature_candidates_from_systems(systems, operation).front();
}

void analyze(operation_t op)
{
  std::string output_directory { "output" };
  if (!std::filesystem::exists(output_directory))
  {
    std::filesystem::create_directory(output_directory);
  }
  else
  {
    for (const auto &entry :
         std::filesystem::directory_iterator(output_directory))
    {
      if (std::filesystem::is_regular_file(entry.path()))
      {
        std::filesystem::remove(entry.path());
        std::cout << "Deleted: " << entry.path().filename() << "\n";
      }
    }
  }

  std::map<std::string, output_lines_t> accumulator {};
  std::mutex                            accumulator_mutex {};

  auto process_one_file = [op, &accumulator, &accumulator_mutex](
                              const std::filesystem::path &msa_file_path)
  {
    auto [spl_file, systems] = parse_file_msa(
        msa_file_path, op.atomic_node_types, op.variant_name_to_system_id);

    if (systems.empty())
    {
      return;
    }

    std::vector<std::shared_ptr<node_t>> all_roots {};
    for (const auto &[sys_id, sys_tok] : systems)
    {
      all_roots.push_back(sys_tok.root);
    }

    std::map<const node_t *, size_t> method_fqn_len_cache {};
    auto score_of = [&](const std::shared_ptr<node_t> &node) -> size_t
    {
      auto method_node { get_parent_method_node(node) };
      if (method_node == nullptr)
      {
        return 0;
      }
      auto [it, inserted]
          = method_fqn_len_cache.try_emplace(method_node.get(), 0);
      if (inserted)
      {
        it->second = get_method_fqn(method_node).size();
      }
      return it->second;
    };

    std::map<std::string, std::vector<std::shared_ptr<node_t>>>
        nodes_by_feature {};

    struct column_info_t
    {
        size_t                    col;
        std::vector<size_t>       present;
        std::vector<std::string>  candidates;
        std::shared_ptr<node_t>   owner_node;
        size_t                    owner_sys;
    };

    std::vector<column_info_t> columns_info {};
    const size_t                col_count { systems.begin()->second.tokens.size() };
    for (size_t col {}; col < col_count; ++col)
    {
      std::vector<size_t> present {};
      for (const auto &[sys_id, sys_tok] : systems)
      {
        if (sys_tok.tokens[col].is_node())
        {
          present.push_back(sys_id);
        }
      }
      if (present.empty())
      {
        continue;
      }
      std::vector<std::string> candidates { get_feature_candidates_from_systems(
          present, op) };

      bool                    has_owner { false };
      size_t                  best_score {};
      size_t                  owner_sys {};
      std::shared_ptr<node_t> owner_node {};
      for (auto sys_id : present)
      {
        auto  &node { systems.at(sys_id).tokens[col].node };
        size_t score { score_of(node) };
        if (!has_owner || score > best_score
            || (score == best_score && sys_id < owner_sys))
        {
          best_score = score;
          owner_sys  = sys_id;
          owner_node = node;
          has_owner  = true;
        }
      }

      columns_info.push_back({ col,
                               std::move(present),
                               std::move(candidates),
                               owner_node,
                               owner_sys });
    }

    auto finalize_column
        = [&](const column_info_t &info, const std::string &chosen_raw)
    {
      const std::string feat { transform_dnf_feature(chosen_raw) };
      for (auto sys_id : info.present)
      {
        systems.at(sys_id).tokens[info.col].node->feature = feat;
      }
      for (const auto &f : expand_or_feature(feat))
      {
        nodes_by_feature[f].push_back(info.owner_node);
      }
    };

    // Disambiguate ambiguous system-combinations using the ArgoUML trace
    // hierarchy. A class trace has no parent and is never disambiguated; a
    // method trace is disambiguated against its enclosing class's trace;
    // anything else (refinement-line-contributing tokens) is disambiguated
    // against its enclosing method's trace, falling back to its enclosing
    // class's trace. Column order is source-document order, but modifiers,
    // annotations, and keywords that precede a class/method's own identifier
    // token are still structurally inside that class/method and would be
    // visited *before* the identifier's column resolves it — so identifier
    // columns are fully resolved for the whole file first (class before
    // method), and every other column is only resolved afterwards, once all
    // identifier context is available regardless of relative column order.
    for (auto &info : columns_info)
    {
      if (is_class_identifier(info.owner_node))
      {
        finalize_column(info, info.candidates.front());
      }
    }
    for (auto &info : columns_info)
    {
      if (!is_method_identifier(info.owner_node))
      {
        continue;
      }
      std::string chosen_raw { info.candidates.front() };
      if (info.candidates.size() > 1)
      {
        std::set<std::string> parent_context {};
        auto class_node { get_parent_class_node(info.owner_node) };
        auto class_id { class_node == nullptr
                            ? nullptr
                            : class_node->get_child_by_tag("identifier") };
        if (class_id != nullptr && !class_id->feature.empty())
        {
          parent_context = literal_union(class_id->feature);
        }
        chosen_raw = info.candidates[pick_best_candidate_index(
            info.candidates, parent_context)];
      }
      finalize_column(info, chosen_raw);
    }
    for (auto &info : columns_info)
    {
      if (is_class_identifier(info.owner_node)
          || is_method_identifier(info.owner_node))
      {
        continue;
      }
      std::string chosen_raw { info.candidates.front() };
      if (info.candidates.size() > 1)
      {
        std::set<std::string> parent_context {};
        auto method_node { get_parent_method_node(info.owner_node) };
        auto method_id { method_node == nullptr
                             ? nullptr
                             : method_node->get_child_by_tag("identifier") };
        if (method_id != nullptr && !method_id->feature.empty())
        {
          parent_context = literal_union(method_id->feature);
        }
        else
        {
          auto class_node { get_parent_class_node(info.owner_node) };
          auto class_id { class_node == nullptr
                              ? nullptr
                              : class_node->get_child_by_tag("identifier") };
          if (class_id != nullptr && !class_id->feature.empty())
          {
            parent_context = literal_union(class_id->feature);
          }
          else if (class_node == nullptr)
          {
            // Genuinely top-level token (e.g. an import declaration) with no
            // enclosing class or method at all. find_refinement_traces'
            // import special-case broadcasts such a token's resolved feature
            // to every top-level class in the file, so use the union of
            // those classes' already-resolved traces as context.
            for (const auto &top_level_class : get_top_level_class_nodes(
                     systems.at(info.owner_sys).root))
            {
              auto top_level_id {
                top_level_class->get_child_by_tag("identifier")
              };
              if (top_level_id != nullptr && !top_level_id->feature.empty())
              {
                auto literals { literal_union(top_level_id->feature) };
                parent_context.insert(literals.begin(), literals.end());
              }
            }
          }
        }
        chosen_raw = info.candidates[pick_best_candidate_index(
            info.candidates, parent_context)];
      }
      finalize_column(info, chosen_raw);
    }

    for (auto &[feat, nodes] : nodes_by_feature)
    {
      output_lines_t  lines { build_argouml_benchmark_format_for_file(
          nodes, all_roots, feat) };
      std::lock_guard lock { accumulator_mutex };
      accumulator[feat].insert_many(lines);
    }
  };

  if (std::filesystem::is_directory(op.msa_path))
  {
    std::vector<std::filesystem::path> files {};
    for (const auto &entry : std::filesystem::directory_iterator(op.msa_path))
    {
      if (entry.path().extension() == ".output")
      {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(),
              files.end(),
              [](const std::filesystem::path &a, const std::filesystem::path &b)
              {
                return std::filesystem::file_size(a)
                       > std::filesystem::file_size(b);
              });
    const size_t        total { files.size() };
    std::atomic<size_t> progress { 0 };
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, files.size(), 1),
        [&](const tbb::blocked_range<size_t> &range)
        {
          for (size_t i = range.begin(); i != range.end(); ++i)
          {
            size_t n { ++progress };
            std::cout << "Processing file " << n << "/" << total << "\n";
            process_one_file(files[i]);
          }
        },
        tbb::simple_partitioner());
  }
  else
  {
    process_one_file(op.msa_path);
  }

  for (auto &[feat, lines] : accumulator)
  {
    std::string   sanitized { std::regex_replace(feat, std::regex(" "), "_") };
    std::ofstream out(output_directory + "/" + sanitized + ".txt");
    out << lines.render();
  }
}

void render(operation_t op)
{
  auto                  msa { parse_msa(
      op.msa_path, op.atomic_node_types, op.variant_name_to_system_id) };
  std::set<std::string> features;

  for (const auto &file : msa.internal_rep)
  {
    for (size_t token_index { 0 };
         token_index < file.second.begin()->second.tokens.size();
         ++token_index)
    {
      std::vector<size_t> systems;
      for (const auto &system_id_and_tokens : file.second)
      {
        if (system_id_and_tokens.second.tokens[token_index].is_node())
        {
          systems.push_back(system_id_and_tokens.first);
        }
      }
      const std::string feature { transform_dnf_feature(
          get_feature_from_systems(systems, op)) };
      features.insert(feature);
      for (const auto &system_id_and_tokens : file.second)
      {
        if (system_id_and_tokens.second.tokens[token_index].is_node())
        {
          system_id_and_tokens.second.tokens[token_index].node->feature
              = feature;
        }
      }
    }
  }

  std::ostringstream content {};

  std::ifstream templ_file_name("../template.html");
  std::string   templ(std::istreambuf_iterator<char> { templ_file_name }, {});

  for (const auto &file_pair : msa.internal_rep)
  {
    for (const auto &system_id_and_tokens : file_pair.second)
    {
      content << "<pre>";
      int line { 0 };
      int col { 0 };
      for (const auto &tok : system_id_and_tokens.second.tokens)
      {
        if (tok.is_node())
        {
          while (line < tok.node->get_node_position().get_start_line())
          {
            content << "\n";
            line++;
            col = 0;
          }
          while (col < tok.node->get_node_position().get_start_column())
          {
            content << "&nbsp;";
            col++;
          }

          std::string token_text { tok.node->get_ts_text() };
          replace_all(token_text, "<", "&lt;");
          replace_all(token_text, ">", "&gt;");
          std::string f {
            ((tok.node->feature == ""s)
                 ? "Unknown"
                 : std::regex_replace(tok.node->feature, std::regex(" "), "_"))
          };

          content << "<span title=\"" << f << "\" class=\"" << f << "\">"
                  << token_text << "</span>";
          line += tok.node->get_node_position().get_end_line()
                  - tok.node->get_node_position().get_start_line();
          col += tok.node->get_node_position().get_end_column()
                 - tok.node->get_node_position().get_start_column();
        }
      }
      content << "</pre>";
      content << "<hr>";
    }
    break;
  }

  std::string from { "___code___" };

  size_t start_pos = templ.find(from);
  if (start_pos != std::string::npos)
  {
    templ.replace(start_pos, from.length(), content.str());
  }

  std::string feature_comma_list {};
  for (const auto &f : features)
  {
    feature_comma_list.append(std::regex_replace(f, std::regex(" "), "_"));
    feature_comma_list.append(",");
  }
  feature_comma_list.append("Unknown");
  std::string from2 { "___features___" };
  size_t      start_pos2 = templ.find(from2);
  templ.replace(start_pos2, from2.length(), feature_comma_list);

  std::ofstream ofile("output.html");

  ofile << templ;
}

int main(int argc, char *argv[])
{
  operation_t         operation { cli_arguments(argc, argv) };
  tbb::global_control gc(tbb::global_control::max_allowed_parallelism,
                         operation.threads > 0
                             ? operation.threads
                             : std::thread::hardware_concurrency());

  switch (operation.operation_type)
  {
    case operation_t::operation_type_t::print_system_names :
      print_systems(operation);
      break;
    case operation_t::operation_type_t::analyze :
      analyze(operation);
      break;
    case operation_t::operation_type_t::render :
      render(operation);
      break;
  }

  return 0;
}
