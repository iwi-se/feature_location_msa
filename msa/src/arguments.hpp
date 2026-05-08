
#pragma once
#include <filesystem>

enum class language
{
  cpp,
  java
};

struct options
{
    language              language;
    std::filesystem::path path;
};

options parse_cli_arguments(int argc, char *argv[]);
