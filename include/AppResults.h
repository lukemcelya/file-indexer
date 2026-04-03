#pragma once

#include "DbResults.h"

namespace app
{

  struct Error
  {
    enum class Type
    {
      InvalidPath,
      AlreadyIndexed,
      NotIndexed,
      Database
    };

    Type type;
    std::string message;
    std::optional<db::Error> dbError;
  };
}
