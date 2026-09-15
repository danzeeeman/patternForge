#pragma once
#include "ofMain.h"
#include "ofxImGui.h"
#include "garments/GarmentModule.h"
#include "ui/ParamPanel.h"
#include "ui/PatternCanvas.h"
#include "sim/ClothSim.h"
#include "ui/Preview3D.h"
#include "render/Pattern3D.h"
#include "geometry/EditablePath.h"
#include "geometry/OutlineEdits.h"
#include <map>
#include <set>

// patternForge: an interactive, perimetric pattern-design tool. Every
// garment is one GarmentModule (Dress/Coat/Shirt/Pants); picking a size
// preset and moving the style sliders rebuilds every piece's perimeter
// live on PatternCanvas, and "Export package" writes the same seven-
// artifact folder shape as the two reference coat packages in this repo
// (see src/export/PackageWriter.h).
class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    // Dropping a design-project.json on the window loads it. This is the
    // route that involves no native dialog at all, which matters because
    // the dialog is the part that hangs.
    void dragEvent(ofDragInfo info) override;
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
    void keyPressed(int key) override;

private:
    void rebuild();
    void doExport();
    void doLoadDesign();
    void loadDesignFile(const std::string& path);

    // Opening a native file dialog from inside the ImGui frame deadlocks
    // on macOS: the modal panel runs its own event loop while the GL
    // context is mid-render and the frame never ends. The button only
    // raises this flag; update() opens the dialog, outside the frame.
    // How closely the editable path follows the drafted outline. Smaller
    // keeps more points, so there is more to grab -- at the cost of the
    // points sitting closer together than the mouse can separate them.
    float editDetailCm_ = 0.12f;
    // The drape simulation. It was reachable only from a headless test
    // hook, which meant it may as well not have existed for anyone using
    // the app -- a subsystem nobody can run is not a feature.
    bool simulate_ = false;
    pf::sim::SimGarment sim_;
    pf::sim::SimSettings simSettings_;
    ofMesh simMesh_;
    int simSteps_ = 0;
    pf::sim::Avatar avatar_;
    int avatarChoice_ = 1;
    float avatarDetailCm_ = 1.6f;
    int fabric_ = 0;
    float startInflate_ = 6.f;        // 0 none (capsules), 1 female, 2 male
    void loadAvatar(int which);
    void startSim();
    void stepSim();

    bool wantLoadDialog_ = false;
    char loadPathBuf_[512] = { 0 };  // typed-in path, as a dialog-free fallback

    // Outline editing: `paths_` holds an editable Bezier path per piece,
    // refreshed from the generated pattern each rebuild -- except for the
    // pieces in `editedCodes_`, whose paths the designer now owns and
    // which are applied back over the generated geometry.
    void applyEdits();
    ofRectangle canvasViewport() const;
    void resetEdit(const std::string& code);
    void constrainEdit(const std::string& code);
    void ensureEditRecord(const std::string& code);
    void syncEditFromLive(const std::string& code, int anchor);
    // Front and back are sewn to each other down the side seam, so an
    // edit to one panel's side seam has to carry to the other.
    void mirrorSeamEdit(const std::string& code, int anchor);
    void toggleSelected(int anchor);
    void recordInsertedPoint(const std::string& code, int seg);
    void subdivideOutline(const std::string& code);
    std::string seamPartner(const std::string& code) const;
    ofJson editsToJson() const;
    void editsFromJson(const ofJson& j);

    std::vector<pf::GarmentModulePtr> modules_;
    pf::ui::ParamPanelState paramState_;
    pf::DesignResult design_;
    pf::ui::PatternCanvas canvas_;

    bool show3D_ = false;
    pf::render3d::Garment3D garment3D_;
    pf::ui::Preview3D preview3D_;

    bool editOutlines_ = false;
    std::map<std::string, pf::EditablePath> paths_;      // live, what you drag
    std::map<std::string, pf::EditablePath> basePaths_;  // as generated, before edits
    std::map<std::string, pf::OutlineEdits> edits_;      // the edits themselves
    pf::ui::EditView editView_;
    bool draggingPoint_ = false;

    ofxImGui::Gui gui_;
    std::string exportRootDir_;
    std::string lastExportMessage_;

    int lastMouseX_ = 0, lastMouseY_ = 0;
    float lastClickTime_ = -10.f;   // for spotting a double-click
    glm::vec2 lastClickPos_{ 0.f, 0.f };
};
