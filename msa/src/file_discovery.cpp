#include "file_discovery.hpp"
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

const std::map<language, std::set<std::string>> language_to_file_suffix_map {
  {  language::cpp, { "cpp", "hpp" } },
  { language::java,       { "java" } }
};

void add_file_to_map(const std::string           &file_family_name,
                     const std::string           &variant_name,
                     const std::filesystem::path &absolute_path,
                     std::map<std::string, std::vector<file_variant>>
                         &file_family_name_to_variant_paths_map)
{
  file_family_name_to_variant_paths_map[file_family_name].emplace_back(
      variant_name, absolute_path);
}

std::vector<file_family>
    transform_to_vector(std::map<std::string, std::vector<file_variant>>
                            &file_family_name_to_variant_paths_map)
{
  std::vector<file_family> data;
  data.reserve(file_family_name_to_variant_paths_map.size());
  for (auto &key_value : file_family_name_to_variant_paths_map)
  {
    data.push_back(
        file_family { key_value.first, std::move(key_value.second) });
  }
  return data;
}

bool is_revelant_file(const fs::directory_entry &file_entry,
                      const language            &language)
{
  if (!file_entry.is_regular_file())
  {
    return false;
  }

  const auto  file_suffixes { language_to_file_suffix_map.at(language) };
  std::string suffix_without_dot {
    file_entry.path().extension().string().substr(1)
  };
  if (file_suffixes.contains(suffix_without_dot))
  {
    return true;
  }
  return false;
}

std::string
    compute_family_name_from_relative_path(const std::filesystem::path &p)
{
  // Find in path - / and \\ and replace with _
  std::string modifiedPath { p.string() };
  std::replace(modifiedPath.begin(), modifiedPath.end(), '/', '_');
  std::replace(modifiedPath.begin(), modifiedPath.end(), '\\', '_');

  std::string filename { modifiedPath };

  size_t dot_pos = filename.find_last_of('.');
  if (dot_pos == std::string::npos)
  {
    return filename; // No extension found
  }
  return filename.substr(0, dot_pos);
}

file_families discover_files(const options &options)
{
  std::map<std::string, std::vector<file_variant>>
      file_family_name_to_variant_paths_map {};
  for (const auto &directory_entry : fs::directory_iterator(options.path))
  {
    // directory_entry contains an SPL variant, directory name is used as
    // variant identifier
    if (!directory_entry.is_directory())
    {
      continue;
    }

    for (const auto &file_entry :
         fs::recursive_directory_iterator(directory_entry))
    {
      if (!is_revelant_file(file_entry, options.m_language))
      {
        continue;
      }

      fs::path relative_path { fs::relative(file_entry.path(),
                                            directory_entry.path()) };
      fs::path absolute_path { fs::absolute(file_entry.path()) };

      add_file_to_map(compute_family_name_from_relative_path(relative_path),
                      directory_entry.path().stem().string(),
                      absolute_path,
                      file_family_name_to_variant_paths_map);
    }
  }

  return transform_to_vector(file_family_name_to_variant_paths_map);
}
