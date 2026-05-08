#include "arguments.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

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
  if (argc == 3)
  {
    std::string           lang_s { argv[1] };
    std::string           path_s { argv[2] };
    auto                  lang { parse_language(lang_s) };
    std::filesystem::path path { path_s };
    return options { lang, path };
  }
  else
  {
    std::cerr << "Usage: prog <lang> <path>";
    exit(1);
  }
}
