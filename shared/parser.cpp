#include "parser.hpp"
#include "tree_sitter/api.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

extern "C"
{
  struct TSLanguage;
  const TSLanguage *tree_sitter_java(void);
  const TSLanguage *tree_sitter_cpp(void);
}

std::string get_node_text(const TSNode &ts_node, const std::string &file_contents)
{
  uint32_t start_byte = ts_node_start_byte(ts_node);
  uint32_t end_byte   = ts_node_end_byte(ts_node);
  return file_contents.substr(start_byte, end_byte - start_byte);
}

std::shared_ptr<node_t>
    convert_ts_node_to_node(TSNode                       ts_node,
                            const std::filesystem::path &filepath,
                            const std::string           &file_contents,
                            const std::set<std::string> &atomic_types)
{
  const char *type     = ts_node_type(ts_node);
  bool        is_named = ts_node_is_named(ts_node);

  node_position_t node_position {
    filepath,
    { ts_node_start_point(ts_node).row, ts_node_start_point(ts_node).column },
    {   ts_node_end_point(ts_node).row,   ts_node_end_point(ts_node).column }
  };

  std::string text = get_node_text(ts_node, file_contents);

  std::shared_ptr<node_t> n
      = std::make_shared<node_t>(type, text, type, is_named, node_position);

  uint32_t child_count
      = atomic_types.contains(type) ? 0 : ts_node_child_count(ts_node);
  for (uint32_t i = 0; i < child_count; i++)
  {
    TSNode child_ts_node = ts_node_child(ts_node, i);
    auto   child_node = convert_ts_node_to_node(
        child_ts_node, filepath, file_contents, atomic_types);
    n->add_child(child_node);
  }

  return n;
}

std::shared_ptr<node_t> parse_file(const std::filesystem::path &file_path,
                                   const std::string           &language,
                                   const std::set<std::string> &atomic_types)
{
  TSParser *parser = ts_parser_new();
  if (language == "java")
  {
    if (!ts_parser_set_language(parser, tree_sitter_java()))
    {
      std::cerr << "Error setting language" << std::endl;
    };
  }
  else if (language == "cpp")
  {
    if (!ts_parser_set_language(parser, tree_sitter_cpp()))
    {
      std::cerr << "Error setting language" << std::endl;
    };
  }
  else
  {
    throw std::runtime_error("Unsupported language: " + language);
    return nullptr;
  }

  std::ifstream file(file_path);
  std::string   code((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());

  TSTree *tree
      = ts_parser_parse_string(parser, nullptr, code.c_str(), code.size());
  TSNode root_node = ts_tree_root_node(tree);

  auto root = convert_ts_node_to_node(root_node, file_path, code, atomic_types);
  root->calculate_subtree_hashes();

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  ts_language_delete(tree_sitter_cpp());
  ts_language_delete(tree_sitter_java());

  return root;
}
