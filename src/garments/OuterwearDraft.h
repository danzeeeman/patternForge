#pragma once
#include "GarmentModule.h"
#include "GarmentCommon.h"
#include "BlockMath.h"

// Overcoat, peacoat and trench coat: three heavy outer garments drafted
// from one routine, because underneath they ARE one garment -- a lapelled
// torso block with a set-in sleeve, cut long, over real ease.
//
// What separates them is specific and worth stating, since it is what the
// parameters below control:
//
//   Overcoat   Long, single-breasted by default, a modest button wrap and
//              a plain notched lapel. A coat to wear over a suit, so it
//              carries the most ease through the chest of the three.
//   Peacoat    Short -- it ends at the hip. Double-breasted with a wide
//              wrap, and a broad collar that can be turned right up; that
//              width is the point of the garment, not decoration.
//   Trench     Long and double-breasted, plus the pieces that make it a
//              trench rather than a long overcoat: a storm flap over the
//              right chest, a belt, and epaulettes at the shoulder.
//
// A back vent is cut into the coats long enough to need one -- without it
// a long coat binds across the seat when you walk.

namespace pf { namespace outerwear {

enum class Kind { Overcoat = 0, Peacoat, Trench };

struct Spec {
    double bust = 86.4, waist = 68.6, hip = 94.0;
    double backWaistLength = 40.0, hipDepth = 20.0;
    double shoulder = 13.5, neck = 36.0, bicep = 30.0;
    double ease = 20.0;
    // The shared waist controls, carried through to the torso block.
    double waistShaping = 1.0, waistRise = 0.0;

    Kind kind = Kind::Overcoat;
    double length = 110.0;      // nape to hem
    double hemFlare = 10.0;

    bool doubleBreasted = false;
    double wrap = 3.0;          // front extension past CF for the buttons
    block::LapelStyle lapel = block::LapelStyle::Notched;
    double lapelWidth = 9.0;
    double breakY = 46.0;
    double facingWidth = 11.0;
    double collarDepth = 9.0;

    double armholeDepth = 24.0, armholeScoop = 1.0;
    double sleeveLength = 62.0, cuffWidth = 36.0;

    double ventLength = 30.0;   // 0 for no back vent
    double ventWidth = 5.0;

    bool stormFlap = false, belt = false, epaulettes = false;
    double beltWidth = 5.0;
};

// Adds the back vent: a rectangular extension past the center-back seam
// below the vent's top, which is the underlap the two halves of the vent
// overlap on. A long coat without one binds across the seat.
Ring withBackVent(const Ring& backHalf, double ventLength, double ventWidth);

common::Draft draft(const Spec& spec);

// The defaults that make each kind itself, applied over a base spec.
void applyKindDefaults(Spec& spec, Kind kind);

} } // namespace pf::outerwear
