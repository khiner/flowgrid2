#pragma once

#include "string"

struct Config {
    std::string faust_libraries_path{};
};

extern Config config; // Initialized in `main.cpp`
