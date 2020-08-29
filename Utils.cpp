#include "Utils.h"
#include <regex>

std::string Utils::SanitizeName(const std::string& name)
{
    std::regex re("[\\/]");
    std::string sanitizedName = std::regex_replace(name, re, "_");
    return sanitizedName;
}

