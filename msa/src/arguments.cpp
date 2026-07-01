#include "arguments.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
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

options parse_cli_arguments(int argc, char *argv[])
{
  if (argc < 3)
  {
    std::cerr << "Usage: " << argv[0] << " <lang> <path> [--threads N]\n";
    exit(1);
  }

  std::string           lang_s { argv[1] };
  std::string           path_s { argv[2] };
  auto                  lang { parse_language(lang_s) };
  std::filesystem::path path { path_s };
  size_t                threads { std::thread::hardware_concurrency() };

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
  }

  return options { lang, path, 6, "output", threads };
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
