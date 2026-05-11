#pragma once

#include <iosfwd>

namespace alignx::cli {

int run(int argc, char** argv, std::ostream& out, std::ostream& err);

} // namespace alignx::cli
