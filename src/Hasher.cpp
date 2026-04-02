#include <Hasher.h>
#include <picosha2.h>

#include <istream>
#include <string>
#include <array>

namespace hashing
{
  std::string sha256FileHex(std::istream& in)
  {
    // std::ifstream requires std::ios::binary mode
    // using one_by_one instead of regular binary file hash
    // because parameter cannot guarantee ios::binary mode

    picosha2::hash256_one_by_one hasher;

    std::array<char, 4096> buffer{}; // 4KB

    while (in)
    {
      in.read(buffer.data(), buffer.size());

      if (const std::streamsize bytesRead = in.gcount(); bytesRead > 0)
      {
        hasher.process(buffer.begin(), buffer.begin() + bytesRead);
      }
    }

    hasher.finish();

    return picosha2::get_hash_hex_string(hasher);
  }
}