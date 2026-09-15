#pragma once
#include "../garments/GarmentModule.h"
#include "Triangulate.h"
#include "Avatar.h"
#include <map>
#include <string>
#include <vector>

// A cloth simulation of the actual pattern.
//
// Everything else in this app reasons about the garment flat. This hangs
// the real panels on a body and lets them fall, which is the only way to
// see what a pattern DOES -- whether a sleeve binds, whether a hem swings,
// whether the ease that reads as generous on paper is generous on a body.
//
// How it works, and where each part came from:
//
//   1. Each panel is triangulated (Triangulate.h) with its boundary
//      sampled at a fixed spacing.
//   2. Panels are placed around the body using each piece's Placement.
//   3. Seams are made by WELDING boundary vertices: the two edges being
//      sewn have their vertices merged into shared ones, so the garment
//      becomes a single connected mesh rather than separate sheets pulled
//      together by springs. (GarmentCode does this; it is much better
//      behaved than spring seams, which stretch and buzz.)
//   4. XPBD relaxes stretch and bend constraints under gravity, against a
//      body built of capsules from the size preset's measurements.
//
// It is a drape preview, not a material simulation: no friction model, no
// measured fabric parameters, no self-collision. It will show you a
// silhouette and where cloth pulls. It will not tell you how silk behaves.

namespace pf { namespace sim {

struct SimSettings {
    double resolutionCm = 2.2;   // triangle size; smaller is slower
    // Passes the solver makes per frame. More is stiffer and steadier:
    // cloth that "breaks apart" is usually cloth whose constraints never
    // converged, not cloth that was pulled too hard. Cost is linear in
    // both, and the constraint pass is only ~4 ms, so these are cheap to
    // raise -- collision dominates the frame, and it runs once per
    // substep, not per iteration.
    int iterations = 30;         // constraint passes per substep
    int substeps = 6;            // physics steps per frame
    double gravity = -980.0;     // cm/s^2
    double stretchCompliance = 1e-7;
    double bendCompliance = 5e-4;
    // A sewn seam is nearly rigid -- thread does not stretch, and a seam
    // that opens under the garment's own weight is not a seam.
    //
    // It was made soft earlier for the wrong reason: assembly used to
    // start the two sides 12 cm apart, and a stiff constraint closing that
    // gap tore the cloth. Now assembly closes them to 0.00 cm before
    // gravity is ever applied, so the seam only has to HOLD, and holding
    // is exactly what it should do. Soft seams simply came apart.
    double seamCompliance = 1e-8;
    double damping = 0.985;
    double bodyMarginCm = 0.4;
    // How far off the body the panels start.
    //
    // A garment is not put on by materialising around someone. The panels
    // are held off the body, clear of each other, and then drawn together
    // and sewn -- and that is also the only arrangement a solver can get
    // right, because it never has to push cloth out through a body it has
    // already been placed inside.
    //
    // Standing a panel off does NOT stretch it: the arc it covers is
    // measured on the tube it is actually placed on, so it keeps its
    // drafted size and simply spans less of the way round. What opens up
    // instead are the seams, which is what the sewing phase is for.
    double startInflateCm = 6.0;

    // Frames spent drawing the seams together before gravity is applied.
    //
    // Sewing and draping at once is what tore the early garments: the
    // seams had to haul the panels round the body while gravity was
    // already pulling the result down, and the mesh recorded the damage.
    // Sew first, in the order a garment is actually made, and the drape
    // starts from a garment rather than from a wreck.
    int assemblySteps = 90;
    // How much of the cloth's slide along the body is taken out on
    // contact. Zero makes every garment fall off; this is the single
    // number that decides whether a dress stays on a shoulder.
    double bodyFriction = 0.6;
};

// Fabrics. A material is not only how cloth LOOKS -- it is how it hangs,
// and the two have to agree or the picture lies about the garment. Denim
// resists bending and falls in wide planes; silk barely resists it and
// breaks into small folds; jersey stretches where a woven will not.
//
// These numbers are the solver's, not a textile lab's: they are chosen so
// the drape reads correctly, which is what this preview is for.
enum class Fabric { Cotton = 0, Wool, Silk, Denim, Jersey, Leather };
const char* fabricName(Fabric f);
void applyFabric(SimSettings& s, int fabric);

// A capsule of the body the garment hangs on. Crude on purpose: the point
// is to stop cloth passing through the wearer, not to model anatomy.
struct Capsule {
    glm::vec3 a, b;
    float r = 10.f;
};

struct SimGarment {
    std::vector<glm::vec3> pos, prev, vel;
    std::vector<float> invMass;
    std::vector<int> tris;

    struct Dist { int a, b; float rest; };
    std::vector<Dist> stretch;
    struct Bend { int a, b; float rest; };   // across a shared edge
    std::vector<Bend> bend;

    std::vector<Capsule> body;
    // When set, cloth collides against this mesh instead of the capsules.
    const Avatar* avatar = nullptr;
    std::vector<int> panelOf;                // which piece each vertex came from
    std::vector<std::string> panelNames;

    // Matched vertex pairs along every seam. Pulled together by the
    // solver rather than merged outright.
    std::vector<std::pair<int, int>> seamPairs;
    // How far apart each pair started. The sewing phase walks its target
    // from this down to zero, so the panels travel to meet each other
    // instead of being snapped together in one step.
    std::vector<float> seamGap0;
    // The way in. Each vertex's offset from where it starts, held off the
    // body, to where the same panel sits wrapped ON it.
    //
    // Sewing the panels to each other is only half of getting dressed. A
    // garment that starts 20 cm clear and is only ever pulled toward its
    // own seams closes into a sack of exactly the right measurements,
    // hanging in the air around the wearer -- which is what "come together
    // AROUND the body" was asking for and what was missing.
    std::vector<glm::vec3> deflate;
    int assemblyStep = 0;               // frames of sewing done so far
    bool assembling() const { return assemblyStep < assemblyTotal; }
    int assemblyTotal = 0;
    float seamGapAfterAssembly = 0.f;   // how well the sewing closed
    int weldedPairs = 0;
    float weldGapAvg = 0.f, weldGapMax = 0.f;  // how far apart the seam sides started
    int seamsApplied = 0, seamsSkipped = 0, seamsReversed = 0;

    // One row per physical join, so a seam that starts badly placed can be
    // named rather than hidden in a garment-wide average.
    struct SeamRow { std::string name; float avgGap, maxGap; int pairs; bool reversed; };
    std::vector<SeamRow> seamReport;

    bool empty() const { return pos.empty() || tris.empty(); }
};

// Builds the simulatable garment from a finished design.
SimGarment build(const DesignResult& design, const SimSettings& s,
                 const Avatar* avatar = nullptr);

// Where the time is spent, accumulated across steps.
struct SimProfile {
    double constraintsMs = 0, collisionMs = 0;
    void reset() { constraintsMs = collisionMs = 0; }
};
extern SimProfile gProfile;

// Advances the simulation by `dt` seconds.
void step(SimGarment& g, const SimSettings& s, double dt);

// Body capsules from a size preset -- torso, arms and legs.
std::vector<Capsule> bodyFromSize(const SizePreset& size);

} } // namespace pf::sim
