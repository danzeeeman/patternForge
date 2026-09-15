#include "Preview3D.h"

namespace pf { namespace ui {

void Preview3D::computeNormals(ofMesh& mesh) {
    const auto& v = mesh.getVertices();
    const auto& idx = mesh.getIndices();
    std::vector<glm::vec3> n(v.size(), glm::vec3(0));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const glm::vec3& a = v[idx[i]];
        const glm::vec3& b = v[idx[i + 1]];
        const glm::vec3& c = v[idx[i + 2]];
        // Un-normalised cross product, so bigger triangles count for more.
        glm::vec3 f = glm::cross(b - a, c - a);
        n[idx[i]] += f; n[idx[i + 1]] += f; n[idx[i + 2]] += f;
    }
    mesh.clearNormals();
    for (auto& q : n) {
        float l = glm::length(q);
        mesh.addNormal(l > 1e-8f ? q / l : glm::vec3(0, 0, 1));
    }
}

void Preview3D::setFabric(int fabric) {
    struct Look { float r, g, b, spec, shine; };
    static const Look looks[] = {
        { 0.88f, 0.86f, 0.82f, 0.06f,  8.f },   // cotton poplin: matte
        { 0.46f, 0.47f, 0.52f, 0.10f, 14.f },   // wool suiting
        { 0.84f, 0.80f, 0.86f, 0.45f, 64.f },   // silk crepe: lustrous
        { 0.28f, 0.36f, 0.52f, 0.05f,  6.f },   // denim
        { 0.80f, 0.78f, 0.76f, 0.08f, 10.f },   // jersey
        { 0.30f, 0.24f, 0.21f, 0.30f, 40.f },   // leather
    };
    const Look& l = looks[std::max(0, std::min(fabric, 5))];
    clothMat_.setDiffuseColor(ofFloatColor(l.r, l.g, l.b));
    clothMat_.setSpecularColor(ofFloatColor(l.spec));
    clothMat_.setShininess(l.shine);
    clothMat_.setAmbientColor(ofFloatColor(l.r * 0.18f, l.g * 0.18f, l.b * 0.2f));
}

void Preview3D::setupLights(const ofRectangle& viewport) {
    if (!lightsReady_) {
        // Three-point: a key to give the garment form, a softer fill so
        // the shadow side is still readable, and a rim behind to separate
        // it from the black background.
        key_.setDirectional();
        key_.setDiffuseColor(ofFloatColor(1.0f, 0.97f, 0.92f));
        key_.setSpecularColor(ofFloatColor(0.35f));
        key_.setOrientation(glm::vec3(-35, -40, 0));

        fill_.setDirectional();
        fill_.setDiffuseColor(ofFloatColor(0.34f, 0.36f, 0.42f));
        fill_.setSpecularColor(ofFloatColor(0.f));
        fill_.setOrientation(glm::vec3(-10, 130, 0));

        rim_.setDirectional();
        rim_.setDiffuseColor(ofFloatColor(0.55f, 0.57f, 0.65f));
        rim_.setOrientation(glm::vec3(15, 195, 0));

        // Cloth is lit from behind as well as in front, so the far side of
        // a fold is not simply black.
        clothMat_.setAmbientColor(ofFloatColor(0.16f, 0.16f, 0.18f));

        bodyMat_.setDiffuseColor(ofFloatColor(0.38f, 0.36f, 0.35f));
        bodyMat_.setSpecularColor(ofFloatColor(0.04f));
        bodyMat_.setShininess(4.f);
        bodyMat_.setAmbientColor(ofFloatColor(0.10f, 0.10f, 0.11f));
        lightsReady_ = true;
    }
    ofSetGlobalAmbientColor(ofColor(30, 30, 34));
    key_.enable(); fill_.enable(); rim_.enable();
}

void Preview3D::setActive(bool active) {
    active_ = active;
    if (active_) cam_.enableMouseInput(); else cam_.disableMouseInput();
}

void Preview3D::frame(const render3d::Garment3D& garment, float azimuthDeg) {
    float h = garment.worldYTop - garment.worldYBottom;
    float size = std::max(20.f, std::max(h, garment.worldXHalf * 2.f * 0.85f));
    cam_.reset();
    glm::vec3 target(0.f, (garment.worldYTop + garment.worldYBottom) * 0.5f, 0.f);
    cam_.setTarget(target);
    cam_.setNearClip(0.1f);
    cam_.setFarClip(size * 20.f);
    // A slight 3/4 turn by default, so the garment reads in the round.
    cam_.orbitDeg(azimuthDeg, 8.f, size * 1.6f, target);
}

void Preview3D::draw(const render3d::Garment3D& garment, const ofRectangle& viewport) {
    ofPushStyle();
    ofEnableDepthTest();
    cam_.begin(viewport);

    // Unlit: panel tones come from per-vertex colors (front light, back
    // darker), and the cut outlines carry the shapes.
    ofSetColor(255);
    garment.mesh.drawFaces();
    ofSetColor(60, 58, 55);
    ofSetLineWidth(1.5f);
    garment.outlines.draw();

    cam_.end();
    ofDisableDepthTest();
    ofPopStyle();
}

void Preview3D::drawMesh(const ofMesh& mesh, const ofRectangle& viewport) {
    ofPushStyle();
    ofEnableDepthTest();
    cam_.begin(viewport);

    ofSetColor(255);
    mesh.drawFaces();
    // Wireframe over the faces: on simulated cloth the triangles are what
    // show where it is pulling, and a flat-shaded surface hides that.
    ofSetColor(70, 68, 64, 90);
    mesh.drawWireframe();

    cam_.end();
    ofDisableDepthTest();
    ofPopStyle();
}

void Preview3D::drawDrape(const ofMesh& cloth, const std::vector<pf::sim::Capsule>& body,
                          const ofRectangle& viewport, const ofMesh* avatar) {
    ofPushStyle();
    ofEnableDepthTest();
    cam_.begin(viewport);
    setupLights(viewport);
    ofEnableLighting();

    // The body first, darker and solid, so the cloth reads as sitting on
    // something rather than floating.
    if (avatar) {
        bodyMat_.begin();
        avatar->drawFaces();
        bodyMat_.end();
    }
    bodyMat_.begin();
    for (auto& cap : body) {
        if (avatar) break;
        glm::vec3 ab = cap.b - cap.a;
        float len = glm::length(ab);
        ofPushMatrix();
        ofTranslate((cap.a + cap.b) * 0.5f);
        // ofDrawCylinder runs along Y, so turn it onto the capsule's axis.
        if (len > 1e-4f) {
            glm::vec3 axis = glm::normalize(ab);
            glm::vec3 up(0, 1, 0);
            glm::vec3 cross = glm::cross(up, axis);
            float dot = glm::clamp(glm::dot(up, axis), -1.f, 1.f);
            if (glm::length(cross) > 1e-5f)
                ofRotateRad(std::acos(dot), cross.x, cross.y, cross.z);
            ofDrawCylinder(cap.r, len);
        }
        ofPopMatrix();
        ofDrawSphere(cap.a, cap.r);
        ofDrawSphere(cap.b, cap.r);
    }
    bodyMat_.end();

    // Cloth is a thin sheet seen from both sides, so light its back faces
    // too -- otherwise the inside of every fold reads as a hole.
    glEnable(GL_LIGHT_MODEL_TWO_SIDE);
    clothMat_.begin();
    cloth.drawFaces();
    clothMat_.end();

    ofDisableLighting();
    key_.disable(); fill_.disable(); rim_.disable();

    cam_.end();
    ofDisableDepthTest();
    ofPopStyle();
}

} } // namespace pf::ui
