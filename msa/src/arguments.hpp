#pragma once
#include <filesystem>
#include <string>

enum class language
{
  cpp,
  java
};

struct options
{
    language              language;
    std::filesystem::path path;
    bool                  fast { true };
    size_t                n_gram_size { 6 };
};

options parse_cli_arguments(int argc, char* argv[]);

std::string render_language(const language& lang);
