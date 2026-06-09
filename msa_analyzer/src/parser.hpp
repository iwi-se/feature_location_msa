#ifndef parser_hpp_t
#define parser_hpp_t

#include "tree.hpp"
#include <filesystem>

std::unique_ptr<node_t> parse_file(const std::filesystem::path &file_path,
                                   const std::string           &language);

#endif // PARSER_HPP
