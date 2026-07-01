#include "arguments.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

const std::map<std::string, language> string_language_map {
  {  "cpp",  language::cpp },
  { "java", language::java }
};

language parse_language(const std::string &lang)
{
  try
  {
    return string_language_map.at(lang);
  }
  catch (std::out_of_range)
  {
    std::cerr << "Language \"" << lang << "\" unknown, stopping." << std::endl;
    std::exit(1);
  }
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

options parse_cli_arguments(int argc, char *argv[])
{
  if (argc < 3)
  {
    std::cerr << "Usage: " << argv[0]
               << " <lang> <path> [--threads N] [--atomic-types-file FILE]\n";
    exit(1);
  }

  std::string           lang_s { argv[1] };
  std::string           path_s { argv[2] };
  auto                  lang { parse_language(lang_s) };
  std::filesystem::path path { path_s };
  size_t                threads { std::thread::hardware_concurrency() };
  std::set<std::string> atomic_node_types {};

  for (int i = 3; i < argc - 1; ++i)
  {
    if (std::string(argv[i]) == "--threads")
    {
      try
      {
        threads = std::stoul(argv[i + 1]);
        ++i;
      }
      catch (...)
      {
        std::cerr << "--threads requires a positive integer\n";
        exit(1);
      }
    }
    else if (std::string(argv[i]) == "--atomic-types-file")
    {
      atomic_node_types = parse_atomic_types_file(argv[i + 1]);
      ++i;
    }
  }

  return options { lang, path, 6, "output", threads, atomic_node_types };
}

std::string render_language(const language &lang)
{
  switch (lang)
  {
    case language::cpp :
      return "cpp";
    case language::java :
      return "java";
    default :
      return "unknown";
  };
}
