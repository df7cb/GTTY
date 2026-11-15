#include "core/version.h"

#include "gtty_version.h"

namespace gtty {

// Kept in a file of its own: the generated header changes with every commit, and only
// this file is recompiled for it.
const char* version() { return GTTY_VERSION_STRING; }

}  // namespace gtty
