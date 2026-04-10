// the cursor is always a "valid" target — there's always exactly one of
// it. this provider exists mainly so the orchestrator can ask "what's
// the cursor target right now" and get back something with the same
// shape as a window target.
#pragma once

#include "targets/target_provider.h"

namespace cr {

class CursorTargetProvider
{
public:
    HeistTarget current() const;
};

} // namespace cr
