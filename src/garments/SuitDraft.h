#pragma once
#include "GarmentModule.h"
#include "BlockMath.h"
#include "GarmentCommon.h"

// The jacket and trouser drafts behind both suit garments.
//
// A pant suit is a jacket and a pair of trousers cut to the same body and
// exported as one package, so the two drafts live here and both modules
// call them. Keeping one copy is the point: if a suit's jacket drifted
// from the standalone Suit Jacket's, "the same jacket" would quietly stop
// being true.
//
// Each draft takes a code prefix, because a package holding both needs
// piece codes that don't collide ("F" the jacket front vs "PF" the
// trouser front).

namespace pf { namespace suit {

struct JacketSpec {
    double bust = 86.4, waist = 68.6, hip = 94.0;
    double backWaistLength = 40.0, hipDepth = 20.0;
    double shoulder = 13.0, neck = 36.0, bicep = 30.0;
    double ease = 10.0;
    // The shared waist controls, carried through to the torso block.
    double waistShaping = 1.0, waistRise = 0.0;
    double jacketLength = 70.0;      // nape to hem
    double hemFlare = 0.0;

    block::LapelStyle lapel = block::LapelStyle::Notched;
    double wrap = 2.0;               // front extension past CF, for the buttons
    double breakY = 46.0;            // where the lapel stops rolling open
    double lapelWidth = 8.0;
    double facingWidth = 9.0;

    double armholeDepth = 22.0, armholeScoop = 1.0;
    double sleeveLength = 60.0, cuffWidth = 28.0;
    double backNeckDepth = 2.5, backNeckWidth = 12.0;
    bool centerBackSeam = true;
};

// The shared partial-garment type; suit drafts are nothing special.
using Draft = common::Draft;

// Jacket: front (with lapel), back, sleeve, under collar, front facing.
Draft draftJacket(const JacketSpec& spec, const std::string& codePrefix);

struct TrouserSpec {
    double waist = 68.6, hip = 94.0, thigh = 55.0;
    double rise = 27.0, inseam = 74.0;
    double kneeWidth = 42.0, ankleWidth = 42.0;
    double easeWaist = 2.0, easeHip = 4.0;
};

// Trousers: front leg, back leg, waistband.
Draft draftTrousers(const TrouserSpec& spec, const std::string& codePrefix);

} } // namespace pf::suit
