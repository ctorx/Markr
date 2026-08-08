#pragma once

#include "../src/md_types.h"
#include "test_framework.h"

#include <string>

// Parse markdown and return the structural dump used by every parser test.
inline std::string P(const std::string& src) {
    return md::dump(md::parse(src));
}
