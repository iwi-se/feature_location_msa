#include "argouml_benchmark_format.hpp"
#include "parser.hpp"
#include "tree.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stack>
#include <string>
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
    node_t *node { nullptr };

    bool is_filler() const
    {
      return token_kind == token_kind_t::filler_t;
    }

    bool is_node() const
    {
      return token_kind == token_kind_t::node_t;
    }
};

alignment_token_t make_node_token(node_t *n)
{
  return { alignment_token_t::token_kind_t::node_t, n };
}

const alignment_token_t filler_t { alignment_token_t::token_kind_t::filler_t };

using spl_file_t = std::string;
using system_t   = std::string;

struct system_tokens_t
{
    std::unique_ptr<node_t>        root;
    std::vector<alignment_token_t> tokens;
};

struct msa_representation_t
{
    std::vector<system_t> get_system_names() const
    {
      std::vector<system_t> system_names {};
      for (const auto &system_name : (internal_rep.begin()->second))
      {
        system_names.push_back(system_name.first);
      }
      return system_names;
    }

    std::string                                               lang {};
    std::map<spl_file_t, std::map<system_t, system_tokens_t>> internal_rep {};
};

bool parse_system_msa(std::ifstream                        &file,
                      std::pair<system_t, system_tokens_t> &out,
                      const std::string                    &lang)
{
  std::string system_name {};
  if (file)
  {
    std::getline(file, system_name);
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

  auto tree { parse_file(system_file, lang) };
  auto leaves { tree->get_leafs() };

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

  out = std::make_pair(system_name,
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
    parse_file_msa(const std::filesystem::path &msa_file)
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
  std::map<system_t, system_tokens_t> systems {};

  std::pair<system_t, system_tokens_t> system {};
  while (parse_system_msa(file, system, lang))
  {
    systems.insert(std::move(system));
  }

  file.close();
  return std::make_pair(spl_file, std::move(systems));
}

msa_representation_t parse_directory_msa(const std::filesystem::path &msa_dir)
{
  msa_representation_t msa {};
  for (const auto &dir_entry : std::filesystem::directory_iterator(msa_dir))
  {
    auto file_msa { parse_file_msa(dir_entry) };
    if (msa.lang == "")
    {
      msa.lang = get_lang_from_file_path(file_msa.first);
    }
    msa.internal_rep.emplace(std::move(file_msa));
  }
  return msa;
}

msa_representation_t parse_msa(const std::filesystem::path &msa_file)
{
  if (std::filesystem::is_directory(msa_file))
  {
    return parse_directory_msa(msa_file);
  }
  else
  {
    msa_representation_t msa {};
    auto                 file_msa { parse_file_msa(msa_file) };
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
    std::filesystem::path msa_path {};
    std::filesystem::path expressions_file {};
};

void argument_error(char *argv[])
{
  std::cerr << "Usage: \n"
            << argv[0] << "analyze <msa_outputs> <expressions_file>\n"
            << argv[0] << "printSystemNames <msa_outputs>\n";
  exit(1);
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
    if (argc < 4)
    {
      argument_error(argv);
    }
    std::string msa_path { argv[2] };
    std::string expressions_file { argv[3] };
    return operation_t { operation_t::operation_type_t::render,
                         msa_path,
                         expressions_file };
  }
  else if (operation_type == "analyze")
  {
    if (argc < 4)
    {
      argument_error(argv);
    }
    std::string msa_path { argv[2] };
    std::string expressions_file { argv[3] };
    return operation_t { operation_t::operation_type_t::analyze,
                         msa_path,
                         expressions_file };
  }
  argument_error(argv);
  return operation_t {};
}

void print_systems(const msa_representation_t &msa)
{
  std::cout << "Found " << msa.get_system_names().size() << " systems"
            << std::endl;
  for (const auto &system_name : msa.get_system_names())
  {
    std::cout << system_name << std::endl;
  }
}

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

  // Trim spaces
  auto trim = [](std::string &s)
  {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
  };
  trim(left);
  trim(right);

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

std::vector<node_t *>
    alignment_tokens_to_nodes(std::vector<alignment_token_t> tokens)
{
  std::vector<node_t *> res;
  for (const auto &tok : tokens)
  {
    if (tok.is_node())
    {
      res.push_back(tok.node);
    }
  }
  return res;
}

std::map<std::string, std::vector<node_t *>> evaluate_expression(
    const std::pair<std::vector<std::string>, std::vector<std::string>>
                               &parsed_expression,
    const msa_representation_t &msa)
{
  std::map<std::string, std::vector<node_t *>> result;
  for (const auto &spl_file : msa.internal_rep)
  {
    auto res { evaluate_expression_file(parsed_expression, spl_file.second) };
    std::vector<alignment_token_t> single_tokens {};
    for (const auto &n : res)
    {
      if (!n.empty())
      {
        single_tokens.push_back(n[0]);
      }
    }
    auto nodes { alignment_tokens_to_nodes(single_tokens) };
    result.insert(std::make_pair(spl_file.first, nodes));
  }
  return result;
}

void print_nodes(const std::vector<node_t *> &nodes)
{
  for (const auto &node : nodes)
  {
    std::cout << node->get_ts_text() << " ";
  }
  std::cout << std::endl;
}

void print_results_per_file(
    const std::map<std::string, std::vector<node_t *>> &results_per_file)
{
  for (const auto &file_result : results_per_file)
  {
    std::cout << file_result.first << std::endl;
    print_nodes(file_result.second);
  }
}

std::vector<std::pair<std::string, std::string>>
    read_expressions_from_file(std::filesystem::path fp)
{
  std::vector<std::pair<std::string, std::string>> result;
  std::ifstream                                    file { fp };

  if (!file.is_open())
  {
    std::cerr << "Failed to open file.\n";
    exit(1);
  }

  std::string line {};
  while (std::getline(file, line))
  {
    if (line.empty())
    {
      break;
    }

    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos)
    {
      std::string key = line.substr(0, colon_pos);

      std::string value = line.substr(colon_pos + 1);
      if (!value.empty() && value[0] == ' ')
      {
        value = value.substr(1);
      }
      result.push_back({ key, value });
    }
    else
    {
      std::cerr << "Error in expressions file" << std::endl;
    }
  }
  file.close();
  return result;
}

void analyze(operation_t op, const msa_representation_t &msa)
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
      // Check if it's a file (not a subdirectory)
      if (std::filesystem::is_regular_file(entry.path()))
      {
        // Delete the file
        std::filesystem::remove(entry.path());
        std::cout << "Deleted: " << entry.path().filename() << "\n";
      }
    }
  }

  auto expressions { read_expressions_from_file(op.expressions_file) };

  for (const auto &expression : expressions)
  {
    auto parsed_expression { parse_expression(expression.second) };
    auto results_per_file { evaluate_expression(parsed_expression, msa) };

    if (msa.lang == "java")
    {
      auto benchmark_format_per_file { build_argouml_benchmark_format(
          results_per_file) };

      auto output_file { std::ofstream(output_directory + "/" + expression.first
                                       + ".txt") };
      for (const auto &res : benchmark_format_per_file)
      {
        output_file << res.second;
      }

      output_file.close();
    }
  }
}

void render(operation_t op, const msa_representation_t &msa)
{
  auto expressions { read_expressions_from_file(op.expressions_file) };

  for (const auto &expression : expressions)
  {
    auto parsed_expression { parse_expression(expression.second) };
    for (const auto &spl_file : msa.internal_rep)
    {
      auto res { evaluate_expression_file(parsed_expression, spl_file.second) };
      for (auto &tok : res)
      {
        for (auto &single_tok : tok)
        {
          if (single_tok.is_node() && single_tok.node->feature.empty())
          {
            single_tok.node->feature = expression.first;
          }
        }
      }
    }
  }

  std::ostringstream content {};

  std::ifstream file("../template.html");
  std::string   templ(std::istreambuf_iterator<char> { file }, {});

  std::vector<std::string> features;
  for (const auto &pair : expressions)
  {
    features.push_back(pair.first);
  }

  for (const auto &file_pair : msa.internal_rep)
  {
    for (const auto &system_pair : file_pair.second)
    {
      content << "<pre>";
      int line { 0 };
      int col { 0 };
      for (const auto &tok : system_pair.second.tokens)
      {
        if (tok.is_node())
        {
          while (line < tok.node->get_source_position().get_start_line())
          {
            content << "\n";
            line++;
            col = 0;
          }
          while (col < tok.node->get_source_position().get_start_column())
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
          line += tok.node->get_source_position().get_end_line()
                  - tok.node->get_source_position().get_start_line();
          col += tok.node->get_source_position().get_end_column()
                 - tok.node->get_source_position().get_start_column();
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
  operation_t operation { cli_arguments(argc, argv) };
  auto        msa { parse_msa(operation.msa_path) };

  switch (operation.operation_type)
  {
    case operation_t::operation_type_t::print_system_names :
      print_systems(msa);
      break;
    case operation_t::operation_type_t::analyze :
      analyze(operation, msa);
      break;
    case operation_t::operation_type_t::render :
      render(operation, msa);
      break;
  }

  return 0;
}
