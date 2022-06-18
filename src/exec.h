#pragma once

#include "db.h"
#include "serde.h"

namespace redispp {
auto Execute(DB &db, Client &client, Message query) -> Message;
}  // namespace redispp