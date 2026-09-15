#pragma once
#include "ofMain.h"
#include <string>
#include <vector>

// A real body to drape over, loaded from an OBJ.
//
// The capsule body was only ever a stand-in: it stops cloth passing
// through the wearer but it has no shoulders to speak of, no bust, no
// seat -- and those are exactly the shapes a garment hangs from. A real
// avatar mesh changes what the drape is worth looking at.
//
// The CLO avatars are in MILLIMETRES with Y up from the floor; this app
// works in centimetres measured down from the nape. Both conversions
// happen on load, so everything downstream stays in one system.

namespace pf { namespace sim {

class Avatar {
public:
    // Returns false (and leaves the avatar empty) when the file is missing
    // or unreadable, so the caller can fall back to capsules rather than
    // draping over nothing.
    // `simplifyCm` merges vertices onto a grid of that size before use.
    // A body is a smooth, closed shape, so clustering nearby vertices
    // costs very little silhouette and saves most of the triangles -- and
    // both the collision grid and the draw call scale with that count.
    // Zero keeps the mesh exactly as loaded.
    bool load(const std::string& objPath, double napeFromTopCm = 23.0,
              double simplifyCm = 1.6);

    // Scales the body's GIRTH to the size being drafted.
    //
    // The avatar is one fixed person; the pattern is drafted for whatever
    // size is selected. If the body is wider than the garment, the cloth
    // starts inside it and the collision ejects it -- 15 cm in a single
    // step, which shreds the mesh before anything drapes. Matching the two
    // is what makes the drape mean anything.
    //
    // Only x and z are scaled: heights stay put, because the pattern's
    // vertical landmarks (nape to waist, waist to hip) are measured from
    // the same body and should not move.
    void fitGirthTo(double hipCm, double bustCm);

    // What the body actually measures, after any fitting.
    double girthAt(double cmBelowNape, double bandCm = 1.5) const;

    // How wide and how deep the body is at a level, arms excluded.
    //
    // A torso is not round: across the shoulders it is nearly three times
    // as wide as it is deep. Wrapping a panel on a circle of the right
    // PERIMETER still puts the shoulder seam and the armhole in the wrong
    // places by a hand's width, which is what the sleeves then have to
    // stretch across. Returns false above or below the body.
    bool sectionAt(double cmBelowNape, float& halfWidthCm, float& halfDepthCm,
                   double bandCm = 2.0) const;

    // Where the RIGHT arm (+x) actually is: a point at the shoulder end of
    // the arm's centre line, and the unit direction running down it.
    //
    // A sleeve is a tube around the arm, so it has to be placed on the arm.
    // Guessing a fixed angle puts it somewhere near the arm of whatever
    // body the guess was tuned on and across the chest of every other; the
    // avatars are posed differently (A-pose, T-pose) and the whole sleeve
    // lands in the wrong place when the pose changes. Returns false when
    // the mesh has no arms to find.
    bool armAxis(glm::vec3& shoulder, glm::vec3& dir, float& radiusCm) const;

    size_t originalTriangleCount() const { return origTris_; }

    bool valid() const { return !tris_.empty(); }
    const ofMesh& mesh() const { return mesh_; }
    const std::string& name() const { return name_; }
    size_t triangleCount() const { return tris_.size() / 3; }

    // Pushes `p` out to the surface if it is inside the body or within
    // `marginCm` of it. Returns true when it moved.
    bool pushOut(glm::vec3& p, float marginCm) const;

    // Height of the loaded body in cm, for reporting.
    float heightCm() const { return maxY_ - minY_; }

private:
    // Triangles are bucketed into a uniform grid so a cloth vertex only
    // tests the handful near it. Brute force over 76 000 triangles for
    // every vertex of every substep is not a close call -- it is minutes
    // per frame.
    int cellOf(const glm::vec3& p) const;
    void buildGrid();
    void addNormals();

    std::vector<glm::vec3> verts_;
    std::vector<int> tris_;
    ofMesh mesh_;
    std::string name_;

    glm::vec3 gridMin_{ 0, 0, 0 }, gridMax_{ 0, 0, 0 };
    float cell_ = 5.f;
    int nx_ = 1, ny_ = 1, nz_ = 1;
    std::vector<std::vector<int>> buckets_;   // triangle indices per cell
    float minY_ = 0.f, maxY_ = 0.f;
    size_t origTris_ = 0;
};

} } // namespace pf::sim
