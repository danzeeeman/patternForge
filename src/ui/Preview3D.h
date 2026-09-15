#pragma once
#include "ofMain.h"
#include "../render/Pattern3D.h"
#include "../sim/ClothSim.h"

// An orbit-camera viewport for a Pattern3D mesh: scroll to zoom, drag to
// orbit (ofEasyCam's own mouse handling, enabled only while this view is
// active so it doesn't fight PatternCanvas's 2D pan/zoom).
namespace pf { namespace ui {

class Preview3D {
public:
    void setActive(bool active);
    // Points the camera at a newly built mesh. `azimuthDeg` turns it
    // around the garment: 0 is the front, 180 the back.
    void frame(const render3d::Garment3D& garment, float azimuthDeg = -20.f);
    void draw(const render3d::Garment3D& garment, const ofRectangle& viewport);
    // Draws a bare mesh in the same view -- the simulated cloth, which is
    // not a Garment3D and has no precomputed outlines.
    void drawMesh(const ofMesh& mesh, const ofRectangle& viewport);
    // The cloth AND the body it is draping over. Without the body the
    // garment floats in space and there is no way to judge whether it is
    // hanging correctly or merely hanging.
    void drawDrape(const ofMesh& cloth, const std::vector<pf::sim::Capsule>& body,
                   const ofRectangle& viewport, const ofMesh* avatar = nullptr);

    // Area-weighted vertex normals. Lighting is a no-op on a mesh with
    // no normals, and neither the cloth (which deforms every frame) nor
    // the loaded avatar arrives with any.
    static void computeNormals(ofMesh& mesh);
    // Appearance follows the chosen fabric: a silk should not be rendered
    // with the sheen of leather, or the picture contradicts the drape.
    void setFabric(int fabric);

private:
    void setupLights(const ofRectangle& viewport);

    ofEasyCam cam_;
    ofLight key_, fill_, rim_;
    ofMaterial clothMat_, bodyMat_;
    bool lightsReady_ = false;
    bool active_ = false;
};

} } // namespace pf::ui
