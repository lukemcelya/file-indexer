#pragma once

#include <istream>
#include <string>

namespace hashing
{
  std::string sha256FileHex(std::istream& in);
}
