#pragma once
#include "GarmentModule.h"

// The fitting muslin (toile): the test garment you sew in cheap cloth to
// check fit BEFORE cutting the real cloth. Every module in this app calls
// its output a development draft and tells you to sew a toile -- this is
// the pattern for that toile.
//
// It is not a second design. It is the same garment with everything that
// only finishes it stripped away, and with seam allowances wide enough to
// let out at a fitting, which is the whole reason a muslin exists: you pin
// and re-cut it on a body, then carry what you learned back to the
// pattern. A muslin cut with ordinary 1 cm allowances cannot be let out,
// so it can only tell you a garment is too small, never by how much.

namespace pf { namespace muslin {

// Wider than the 1 cm the real pattern carries, so a seam can be released
// at a fitting: the muslin's purpose is to be altered.
constexpr double kMuslinSeamAllowanceCm = 2.5;

// The fitting-muslin version of a design. Keeps the pieces that carry the
// fit (fronts, backs, sleeves, skirts, legs, gussets), drops the ones that
// only finish the garment (collars, cuffs, plackets, waistbands, facings),
// and re-cuts what remains with muslin seam allowances.
//
// Returns a design with no pieces when a garment is all finishing and no
// shell, which the caller should treat as "nothing to sew a muslin from"
// rather than as an error.
DesignResult muslinFrom(const DesignResult& design);

} } // namespace pf::muslin
