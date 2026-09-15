#pragma once
#include "../geometry/Checks.h"

// Renders a Checks result list as a pass/fail panel -- the interactive
// replacement for the Python pipeline's hard `assert`s: a failing check
// is flagged in the UI instead of crashing the build.
namespace pf { namespace ui {

void drawChecksPanel(const Checks& checks);

} } // namespace pf::ui
