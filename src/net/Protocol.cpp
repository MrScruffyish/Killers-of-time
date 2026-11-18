// Protocol constants live in Protocol.h; this file keeps the library non-empty
// and checks a few compile-time assumptions.
#include "net/Protocol.h"

static_assert(kot::net::kMaxFrameBytes > 1024, "frames must be able to carry real messages");
