#include "tree.hpp"
#include <filesystem>
#include <string>
#include <vector>

template<class T> struct file_variant final
{
    std::string variant;
    T           filepath;
};

template<class T> struct file_family final
{
    std::string                  name {};
    std::vector<file_variant<T>> variants;
};

template<class T> using file_families = std::vector<file_family<T>>;

using file_variant_path = file_variant<std::filesystem::path>;
using file_variant_ast  = file_variant<std::shared_ptr<Node>>;

using file_family_path = file_family<std::filesystem::path>;
using file_family_ast  = file_family<std::shared_ptr<Node>>;

using file_families_path = file_families<std::filesystem::path>;
using file_families_ast  = file_families<std::shared_ptr<Node>>;
