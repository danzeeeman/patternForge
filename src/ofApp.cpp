#include "ofApp.h"
#include "garments/GarmentRegistry.h"
#include "garments/GarmentCommon.h"
#include "sim/ClothSim.h"
#include "ui/ChecksPanel.h"
#include "export/PackageWriter.h"
#include "imgui.h"
#include <algorithm>
#include <iostream>
#include <functional>

using namespace pf;

void ofApp::setup() {
    ofSetWindowTitle("patternForge");
    // Black: a garment reads better against it, and the 2D canvas colours
    // below are chosen to stay legible on a dark ground.
    ofBackground(0, 0, 0);
    gui_.setup();
    modules_ = allGarmentModules();
    exportRootDir_ = ofFilePath::join(ofFilePath::getUserHomeDir(), "Desktop/patternForge-exports");

    // Debug hook shared by PATTERNFORGE_3D_SNAPSHOT: pick the starting
    // garment and any style values so a specific combination can be
    // screenshotted without driving the GUI
    // (e.g. `Dress:sleeveLengthCm=45,silhouette=3`).
    if (const char* sel = std::getenv("PATTERNFORGE_3D_GARMENT")) {
        std::string s(sel);
        std::string key = s.substr(0, s.find(':'));
        for (int i = 0; i < (int)modules_.size(); ++i) {
            if (modules_[i]->key() == key) {
                paramState_.moduleIndex = i;
                pf::ui::setDefaultsForModule(modules_[i], paramState_);
                paramState_.initialized = true; // else drawParamPanel's lazy-init overwrites this with module[0]'s defaults on frame 1
                auto colon = s.find(':');
                if (colon != std::string::npos) {
                    for (auto& kv : ofSplitString(s.substr(colon + 1), ",", true, true)) {
                        auto eq = kv.find('=');
                        if (eq != std::string::npos) paramState_.style.set(kv.substr(0, eq), std::stof(kv.substr(eq + 1)));
                    }
                }
                break;
            }
        }
    }
    if (const char* tol = std::getenv("PATTERNFORGE_EDIT_TOL")) editDetailCm_ = (float)std::atof(tol);
    rebuild();


    // Headless check of the outline editor: seeds a path, moves a point,
    // bends a handle, adds and deletes a point -- through the same calls
    // the mouse uses -- then reports the resulting geometry.
    if (const char* t = std::getenv("PATTERNFORGE_EDIT_TEST")) {
        if (std::string(t) == "1") {
            const std::string code = "F";
            auto pieceByCode = [&](const std::string& c) -> const pf::Piece* {
                for (auto& p : design_.pieces) if (p.code == c) return &p;
                return nullptr;
            };
            auto sideSeamAt = [&](const std::string& c, double y) {
                const pf::Piece* p = pieceByCode(c);
                return p ? geo::maxXAtY(p->sewing, y) : 0.0;
            };
            auto seamCheck = [&]() {
                for (auto& c : design_.checks.results())
                    if (c.name.find("side seams") != std::string::npos)
                        return c.detail + (c.pass ? " [PASS]" : " [FAIL]");
                return std::string("(no side-seam check)");
            };

            // Grab a point on the front's side seam, below the armhole.
            editOutlines_ = true;
            editView_.paths = &paths_;
            ensureEditRecord(code);
            int seamAnchor = -1;
            double seamY = 0;
            for (size_t i = 0; i < paths_[code].anchors.size(); ++i) {
                const pf::Anchor& a = paths_[code].anchors[i];
                double sx = geo::maxXAtY(pieceByCode(code)->sewing, a.pos.y);
                if (a.pos.x > sx - 0.6 && a.pos.y > design_.armholeY + 5.0) { seamAnchor = (int)i; seamY = a.pos.y; break; }
            }
            ofLogNotice("EditTest") << "side-seam point at y=" << seamY
                                     << "  F=" << sideSeamAt("F", seamY) << "  B=" << sideSeamAt("B", seamY);
            ofLogNotice("EditTest") << "  seam check before: " << seamCheck();

            // Pull it out 5 cm -- the front and back must stay together.
            paths_[code].moveAnchor(seamAnchor, Pt(5.f, 0.f));
            syncEditFromLive(code, seamAnchor);
            mirrorSeamEdit(code, seamAnchor);
            rebuild();
            ofLogNotice("EditTest") << "after pulling the seam out 5cm:"
                                     << "  F=" << sideSeamAt("F", seamY) << "  B=" << sideSeamAt("B", seamY);
            ofLogNotice("EditTest") << "  seam check after:  " << seamCheck();

            // Now move a slider. The edit must ride on top of the new shape.
            float len = paramState_.style.get("hemLength", 100.f);
            paramState_.style.set("hemLength", len + 30.f);
            rebuild();
            ofLogNotice("EditTest") << "after moving the length slider +30cm (edit must survive):"
                                     << "  F=" << sideSeamAt("F", seamY) << "  B=" << sideSeamAt("B", seamY);
            ofLogNotice("EditTest") << "  hem now at y=" << [&]{
                const pf::Piece* p = pieceByCode("F");
                Pt lo, hi; geo::bounds(p->sewing, lo, hi); return (double)hi.y; }();
            ofLogNotice("EditTest") << "  seam check after slider: " << seamCheck();
            ofLogNotice("EditTest") << "  edited pieces: " << edits_.size();

            bool pass = design_.checks.allPass();
            for (auto& c : design_.checks.results())
                if (!c.pass) ofLogNotice("EditTest") << "  FAILED: " << c.name << " -- " << c.detail;
            ofLogNotice("EditTest") << "checks: " << (pass ? "all pass" : "FAILURES");
            std::exit(pass ? 0 : 1);
        }
    }

    // Headless check that a facing follows the panel it is cut from:
    // drags the front neckline down and reports whether NF moved with it.
    if (const char* t = std::getenv("PATTERNFORGE_EDIT_TEST")) {
        if (std::string(t) == "facing") {
            auto pieceByCode = [&](const std::string& c) -> const pf::Piece* {
                for (auto& p : design_.pieces) if (p.code == c) return &p;
                return nullptr;
            };
            auto depths = [&](const char* c) {
                const pf::Piece* p = pieceByCode(c);
                if (!p) return std::string("(missing)");
                Pt lo, hi; geo::bounds(p->sewing, lo, hi);
                return "neck y=" + ofToString(geo::minYAtX(p->sewing, 0.0), 2) +
                       "  band bottom y=" + ofToString(hi.y, 2) +
                       "  area=" + ofToString(std::fabs(geo::area(p->sewing)), 1);
            };
            editOutlines_ = true;
            editView_.paths = &paths_;
            const std::string code = "F";
            ensureEditRecord(code);

            ofLogNotice("FacingTest") << "before: F  " << depths("F");
            ofLogNotice("FacingTest") << "before: NF " << depths("NF");

            // Grab the neckline point at the center-front fold and pull it
            // down 4 cm -- a deeper neckline.
            int neckAnchor = -1;
            double bestY = 1e9;
            for (size_t i = 0; i < paths_[code].anchors.size(); ++i) {
                const pf::Anchor& a = paths_[code].anchors[i];
                if (std::fabs(a.pos.x) < 0.5 && a.pos.y < bestY) { bestY = a.pos.y; neckAnchor = (int)i; }
            }
            ofLogNotice("FacingTest") << "dragging CF neck point (anchor " << neckAnchor
                                       << ", y=" << bestY << ") down 4cm";
            paths_[code].moveAnchor(neckAnchor, Pt(0.f, 4.f));
            syncEditFromLive(code, neckAnchor);
            rebuild();

            ofLogNotice("FacingTest") << "after:  F  " << depths("F");
            ofLogNotice("FacingTest") << "after:  NF " << depths("NF");

            const pf::Piece* F = pieceByCode("F");
            const pf::Piece* NF = pieceByCode("NF");
            bool followed = F && NF &&
                std::fabs(geo::minYAtX(F->sewing, 0.0) - geo::minYAtX(NF->sewing, 0.0)) < 0.05;
            ofLogNotice("FacingTest") << (followed
                ? "PASS: the facing's neckline is the panel's neckline"
                : "FAIL: the facing kept a different neckline from its panel");
            bool pass = followed && design_.checks.allPass();
            for (auto& c : design_.checks.results())
                if (!c.pass) ofLogNotice("FacingTest") << "  FAILED: " << c.name << " -- " << c.detail;
            std::exit(pass ? 0 : 1);
        }
    }

    // Subdividing must give MORE points and the SAME shape. A split that
    // moved the outline would quietly restyle the garment every time you
    // asked for somewhere to grab.
    if (const char* t = std::getenv("PATTERNFORGE_EDIT_TEST")) {
        if (std::string(t) == "subdivide") {
            const std::string code = "F";
            editOutlines_ = true;
            editView_.paths = &paths_;
            ensureEditRecord(code);

            Ring before = paths_[code].toRing();
            int nBefore = (int)paths_[code].anchors.size();
            double areaBefore = std::fabs(geo::area(before));

            // Twice, to see whether the residual compounds or settles.
            subdivideOutline(code);
            int nOnce = (int)paths_[code].anchors.size();
            double devOnce = 0.0;
            {
                Ring mid = paths_[code].toRing();
                for (auto& p : mid) {
                    double best = 1e18;
                    for (size_t i = 0; i < before.size(); ++i) {
                        glm::vec2 a(before[i].x, before[i].y);
                        glm::vec2 b(before[(i + 1) % before.size()].x, before[(i + 1) % before.size()].y);
                        glm::vec2 ab = b - a, ap = glm::vec2(p.x, p.y) - a;
                        double l2 = glm::dot(ab, ab);
                        double u = l2 < 1e-12 ? 0.0 : std::max(0.0, std::min(1.0, (double)glm::dot(ap, ab) / l2));
                        best = std::min(best, (double)glm::distance(a + ab * (float)u, glm::vec2(p.x, p.y)));
                    }
                    devOnce = std::max(devOnce, best);
                }
            }
            subdivideOutline(code);
            ofLogNotice("SubdivTest") << "after one split: " << nOnce << " anchors, worst "
                                       << ofToString(devOnce, 4) << " cm";

            Ring after = paths_[code].toRing();
            int nAfter = (int)paths_[code].anchors.size();
            double areaAfter = std::fabs(geo::area(after));

            // Compare the curves by sampling: every point of the new
            // outline should lie on the old one.
            double worst = 0.0;
            Pt worstAt(0.f, 0.f);
            for (auto& p : after) {
                double best = 1e18;
                for (size_t i = 0; i < before.size(); ++i) {
                    glm::vec2 a(before[i].x, before[i].y);
                    glm::vec2 b(before[(i + 1) % before.size()].x, before[(i + 1) % before.size()].y);
                    glm::vec2 ab = b - a, ap = glm::vec2(p.x, p.y) - a;
                    double l2 = glm::dot(ab, ab);
                    double u = l2 < 1e-12 ? 0.0 : std::max(0.0, std::min(1.0, (double)glm::dot(ap, ab) / l2));
                    best = std::min(best, (double)glm::distance(a + ab * (float)u, glm::vec2(p.x, p.y)));
                }
                if (best > worst) { worst = best; worstAt = p; }
            }
            ofLogNotice("SubdivTest") << "anchors " << nBefore << " -> " << nAfter;
            ofLogNotice("SubdivTest") << "worst deviation at (" << ofToString(worstAt.x, 2)
                                       << ", " << ofToString(worstAt.y, 2) << ")";
            ofLogNotice("SubdivTest") << "area " << ofToString(areaBefore, 2) << " -> "
                                       << ofToString(areaAfter, 2) << " cm2";
            ofLogNotice("SubdivTest") << "worst deviation from the original curve: "
                                       << ofToString(worst, 4) << " cm";
            // Splitting a curve is exact, so hold it to 0.2 mm -- well
            // inside the sampling noise, and tight enough to catch the
            // curve actually moving. It measures around 15 microns.
            bool ok = nAfter >= nBefore * 2 - 1 && worst < 0.02
                   && std::fabs(areaAfter - areaBefore) < 0.5;
            ofLogNotice("SubdivTest") << (ok ? "PASS: more points, same shape"
                                             : "FAIL: subdividing changed the outline");
            std::exit(ok ? 0 : 1);
        }
    }

    // Moves ONE point and measures how far every OTHER point moved.
    // Editing a point should be a local act: the rest of the outline is
    // supposed to stay exactly where it was.
    if (const char* t = std::getenv("PATTERNFORGE_EDIT_TEST")) {
        if (std::string(t) == "single") {
            const std::string code = "F";
            editOutlines_ = true;
            editView_.paths = &paths_;
            ensureEditRecord(code);

            // Where ARE the editable points? A landmark the drafter cares
            // about is no use if simplification dropped the anchor there.
            {
                const pf::Piece* p = nullptr;
                for (auto& q : design_.pieces) if (q.code == code) p = &q;
                ofLogNotice("SingleTest") << "anchors down the side seam (waist is y="
                                           << design_.size.get("backWaistLength", 40.0) << "):";
                for (size_t i = 0; i < paths_[code].anchors.size(); ++i) {
                    Pt a = paths_[code].anchors[i].pos;
                    double sx = p ? geo::maxXAtY(p->sewing, a.y) : 0.0;
                    bool onSeam = p && a.x > sx - 0.6 && a.y > design_.armholeY;
                    if (onSeam)
                        ofLogNotice("SingleTest") << "   anchor " << i << "  y=" << ofToString(a.y, 1)
                                                   << "  x=" << ofToString(a.x, 1);
                }
                if (p) {
                    ofLogNotice("SingleTest") << "sewing ring points: " << p->sewing.size()
                                               << "   cutting ring points: " << p->cutting.size();
                    ofLogNotice("SingleTest") << "cutting line self-intersects: "
                                               << (geo::selfIntersects(p->cutting) ? "YES" : "no");
                }
            }

            std::vector<Pt> before;
            for (auto& a : paths_[code].anchors) before.push_back(a.pos);
            int n = (int)before.size();
            int target = n / 3;
            ofLogNotice("SingleTest") << n << " anchors at tolerance "
                                       << ofToString(editDetailCm_, 2) << " cm";
            for (auto& piece : design_.pieces)
                ofLogNotice("SingleTest") << "   " << piece.code << ": "
                                           << paths_[piece.code].anchors.size() << " anchors from "
                                           << piece.sewing.size() << " ring points";
            ofLogNotice("SingleTest") << n << " anchors; moving anchor " << target << " by (4, 0)";

            paths_[code].moveAnchor(target, Pt(4.f, 0.f));
            syncEditFromLive(code, target);
            rebuild();

            auto& after = paths_[code].anchors;
            if ((int)after.size() != n) {
                ofLogNotice("SingleTest") << "  FAIL: anchor count changed " << n
                                           << " -> " << after.size();
                std::exit(1);
            }
            double moved = glm::distance(after[target].pos, before[target]);
            double worstOther = 0.0; int worstIdx = -1;
            for (int i = 0; i < n; ++i) {
                if (i == target) continue;
                double d = glm::distance(after[i].pos, before[i]);
                if (d > worstOther) { worstOther = d; worstIdx = i; }
            }
            ofLogNotice("SingleTest") << "  target moved " << ofToString(moved, 2) << " cm";
            ofLogNotice("SingleTest") << "  worst OTHER point moved "
                                       << ofToString(worstOther, 3) << " cm (anchor " << worstIdx << ")";
            int disturbed = 0;
            for (int i = 0; i < n; ++i)
                if (i != target && glm::distance(after[i].pos, before[i]) > 0.05) ++disturbed;
            ofLogNotice("SingleTest") << "  points disturbed by more than 0.5 mm: "
                                       << disturbed << " of " << (n - 1);
            // The anchors are only half the story: what is drawn is the
            // sampled curve between them, so measure the OUTLINE too.
            auto ringOf = [&]() { return paths_[code].toRing(); };
            bool ok = worstOther < 0.05;

            // And a real edit is a DRAG: many small moves in a row, each
            // one re-applied on top of a freshly generated outline.
            std::vector<Pt> dragBefore;
            for (auto& a : paths_[code].anchors) dragBefore.push_back(a.pos);
            double areaBefore = std::fabs(geo::area(ringOf()));
            for (int step = 0; step < 20; ++step) {
                paths_[code].moveAnchor(target, Pt(0.25f, 0.f));
                syncEditFromLive(code, target);
                rebuild();
            }
            double areaAfter = std::fabs(geo::area(ringOf()));
            auto& dragged = paths_[code].anchors;
            double dragWorstOther = 0.0; int dragWorstIdx = -1;
            if ((int)dragged.size() == n) {
                for (int i = 0; i < n; ++i) {
                    if (i == target) continue;
                    double d = glm::distance(dragged[i].pos, dragBefore[i]);
                    if (d > dragWorstOther) { dragWorstOther = d; dragWorstIdx = i; }
                }
            }
            double targetDrag = (int)dragged.size() == n
                ? glm::distance(dragged[target].pos, dragBefore[target]) : -1.0;
            ofLogNotice("SingleTest") << "after a 20-step drag (+5 cm):";
            ofLogNotice("SingleTest") << "  anchors now " << dragged.size() << " (was " << n << ")";
            ofLogNotice("SingleTest") << "  target moved " << ofToString(targetDrag, 2) << " cm";
            ofLogNotice("SingleTest") << "  worst OTHER point moved "
                                       << ofToString(dragWorstOther, 3) << " cm (anchor "
                                       << dragWorstIdx << ")";
            ofLogNotice("SingleTest") << "  outline area " << ofToString(areaBefore, 1)
                                       << " -> " << ofToString(areaAfter, 1) << " cm2";
            ok = ok && (int)dragged.size() == n && dragWorstOther < 0.05
                    && std::fabs(targetDrag - 5.0) < 0.2;
            // The reported case: pulling the WAIST in to shape the seam.
            // That makes the sewing line concave there, and the cutting
            // line is that curve offset outward by the seam allowance --
            // which is exactly where an offset misbehaves.
            {
                const pf::Piece* p = nullptr;
                for (auto& q : design_.pieces) if (q.code == code) p = &q;
                double waistY = design_.size.get("backWaistLength", 40.0);
                int waistAnchor = -1; double bestD = 1e9;
                for (size_t i = 0; i < paths_[code].anchors.size(); ++i) {
                    Pt a = paths_[code].anchors[i].pos;
                    double sx = p ? geo::maxXAtY(p->sewing, a.y) : 0.0;
                    if (!(a.x > sx - 0.6 && a.y > design_.armholeY)) continue;
                    double d = std::fabs(a.y - waistY);
                    if (d < bestD) { bestD = d; waistAnchor = (int)i; }
                }
                ofLogNotice("WaistTest") << "waist anchor " << waistAnchor
                                          << " at y=" << ofToString(paths_[code].anchors[waistAnchor].pos.y, 1);
                for (double pull : { -2.0, -4.0, -6.0 }) {
                    ensureEditRecord(code);
                    paths_[code].moveAnchor(waistAnchor, Pt((float)(pull / 2.0), 0.f));
                    paths_[code].moveAnchor(waistAnchor, Pt((float)(pull / 2.0), 0.f));
                    syncEditFromLive(code, waistAnchor);
                    rebuild();
                    const pf::Piece* q = nullptr;
                    for (auto& z : design_.pieces) if (z.code == code) q = &z;
                    ofLogNotice("WaistTest") << "  pulled waist in " << ofToString(-pull, 0) << " cm:"
                        << "  sewing crosses itself: " << (geo::selfIntersects(q->sewing) ? "YES" : "no")
                        << "   CUTTING crosses itself: " << (geo::selfIntersects(q->cutting) ? "YES" : "no")
                        << "   cutting pts " << q->cutting.size();
                }
            }


            ofLogNotice("SingleTest") << (ok ? "PASS: the edit stayed local"
                                             : "FAIL: moving one point disturbed the shape");
            std::exit(ok ? 0 : 1);
        }
    }

    // Headless check of multi-select: rubber-bands a run of points, drags
    // them, and confirms every one moved by the same amount.
    if (const char* t = std::getenv("PATTERNFORGE_EDIT_TEST")) {
        if (std::string(t) == "multi") {
            const std::string code = "F";
            editOutlines_ = true;
            editView_.paths = &paths_;
            editView_.active = true;
            ensureEditRecord(code);

            int pieceIdx = 0;
            for (size_t i = 0; i < design_.pieces.size(); ++i)
                if (design_.pieces[i].code == code) pieceIdx = (int)i;

            // Take a run of points down the side seam, below the armhole.
            std::vector<int> group;
            std::vector<Pt> before;
            for (size_t i = 0; i < paths_[code].anchors.size(); ++i) {
                const pf::Anchor& a = paths_[code].anchors[i];
                double sx = geo::maxXAtY(design_.pieces[pieceIdx].sewing, a.pos.y);
                if (a.pos.x > sx - 0.6 && a.pos.y > design_.armholeY + 3.0) {
                    group.push_back((int)i);
                    before.push_back(a.pos);
                }
            }
            editView_.selPiece = pieceIdx;
            editView_.selAnchors = group;
            editView_.selAnchor = group.empty() ? -1 : group.front();
            ofLogNotice("MultiTest") << "selected " << group.size() << " points on " << code;

            // Move the whole group, exactly as a drag does.
            const Pt delta(3.f, -2.f);
            for (int a : group) paths_[code].moveAnchor(a, delta);
            constrainEdit(code);
            for (int a : group) { syncEditFromLive(code, a); mirrorSeamEdit(code, a); }
            rebuild();

            bool allMoved = !group.empty();
            double worst = 0.0;
            for (size_t k = 0; k < group.size(); ++k) {
                Pt now = paths_[code].anchors[group[k]].pos;
                Pt want = before[k] + delta;
                worst = std::max(worst, (double)glm::distance(now, want));
            }
            // The fold constraint may legitimately hold a point on x = 0,
            // so report the worst rather than demanding perfection.
            ofLogNotice("MultiTest") << "  worst deviation from a uniform move: "
                                      << ofToString(worst, 3) << " cm";
            allMoved = allMoved && worst < 0.35;
            for (auto& c : design_.checks.results())
                if (!c.pass) ofLogNotice("MultiTest") << "  FAILED: " << c.name << " -- " << c.detail;
            bool pass = allMoved && design_.checks.allPass();
            ofLogNotice("MultiTest") << (pass ? "PASS: the group moved as one"
                                              : "FAIL: the group did not move together");
            std::exit(pass ? 0 : 1);
        }
    }

    // Loads a design-project.json and reports whether it actually drafts:
    // `PATTERNFORGE_LOAD=/path/to/design-project.json ./patternForge`.
    // Authoring a design file is not the same as it producing a garment --
    // an unknown style key is silently ignored, and a file can load
    // cleanly and still draft something unsewable -- so this runs the
    // checks and exits non-zero if any fail.
    if (const char* path = std::getenv("PATTERNFORGE_LOAD")) {
        loadDesignFile(path);
        bool loaded = lastExportMessage_.rfind("Loaded", 0) == 0;
        ofLogNotice("Import") << path;
        ofLogNotice("Import") << "  " << lastExportMessage_;
        if (loaded) {
            int failed = 0;
            for (auto& c : design_.checks.results()) if (!c.pass) ++failed;
            ofLogNotice("Import") << "  garment: " << design_.title
                                   << " / " << design_.pieces.size() << " pieces"
                                   << " / " << design_.checks.results().size() << " checks"
                                   << " / " << failed << " failing";
            for (auto& c : design_.checks.results())
                if (!c.pass) ofLogNotice("Import") << "    FAIL: " << c.name << " -- " << c.detail;
            std::exit(failed == 0 ? 0 : 2);
        }
        std::exit(1);
    }

    // Builds and runs the cloth simulation headlessly and reports whether
    // it actually held together: panels meshed, seams welded, and -- the
    // part that matters -- whether the solver stayed finite and the seams
    // stayed closed under gravity.
    if (const char* sim = std::getenv("PATTERNFORGE_SIM_TEST")) {
        if (const char* det = std::getenv("PATTERNFORGE_BODY_DETAIL")) {
            avatarDetailCm_ = (float)std::atof(det);
            avatarChoice_ = std::getenv("PATTERNFORGE_BODY_MALE") ? 2 : 1;
            uint64_t t0 = ofGetElapsedTimeMillis();
            loadAvatar(avatarChoice_);
            uint64_t t1 = ofGetElapsedTimeMillis();
            ofLogNotice("SimTest") << "  body: " << avatar_.name()
                                    << "  " << avatar_.originalTriangleCount() << " -> "
                                    << avatar_.triangleCount() << " tris at "
                                    << ofToString(avatarDetailCm_, 1) << " cm"
                                    << "  (loaded in " << (t1 - t0) << " ms)";
        }
        pf::sim::SimSettings ss;
        if (const char* r = std::getenv("PATTERNFORGE_SIM_RES")) ss.resolutionCm = std::atof(r);
        if (const char* f = std::getenv("PATTERNFORGE_INFLATE")) ss.startInflateCm = std::atof(f);
        if (const char* a = std::getenv("PATTERNFORGE_ASSEMBLY")) ss.assemblySteps = std::atoi(a);
        if (const char* m = std::getenv("PATTERNFORGE_FABRIC")) pf::sim::applyFabric(ss, std::atoi(m));
        pf::sim::SimGarment g = pf::sim::build(design_, ss,
                                               avatarChoice_ && avatar_.valid() ? &avatar_ : nullptr);
        ofLogNotice("SimTest") << design_.title;
        ofLogNotice("SimTest") << "  panels " << g.panelNames.size()
                                << "  verts " << g.pos.size()
                                << "  tris " << g.tris.size() / 3
                                << "  stretch " << g.stretch.size()
                                << "  bend " << g.bend.size();
        { std::string names; for (auto& n : g.panelNames) names += n + " ";
          ofLogNotice("SimTest") << "  panel list: " << names; }
        // Does each sleeve actually START AROUND THE ARM?
        //
        // Sitting near the arm is not the same thing, and the difference
        // does not show up in a stretch number: a sleeve laid alongside a
        // limb sews itself into a perfectly good tube beside it. The test
        // is whether the arm's centre line runs THROUGH the sleeve, so
        // stand on that line and ask whether there is cloth in every
        // direction round it.
        if (g.avatar) {
            glm::vec3 sh, dir;
            float armR = 6.f;
            if (g.avatar->armAxis(sh, dir, armR)) {
                glm::vec3 e0 = glm::normalize(glm::cross(dir, glm::vec3(0, 0, 1)));
                glm::vec3 e1 = glm::cross(dir, e0);
                for (size_t p = 0; p < g.panelNames.size(); ++p) {
                    if (g.panelNames[p].rfind("SL", 0) != 0) continue;
                    bool mirrored = g.panelNames[p].find("mirror") != std::string::npos;
                    const int kSectors = 24;
                    std::vector<bool> hit(kSectors, false);
                    double rsum = 0.0; int n = 0;
                    for (size_t i = 0; i < g.pos.size(); ++i) {
                        if (g.panelOf[i] != (int)p) continue;
                        glm::vec3 q = g.pos[i];
                        if (mirrored) q.x = -q.x;      // compare against the +x arm
                        glm::vec3 rel = q - sh;
                        rel -= dir * glm::dot(rel, dir);
                        float r = glm::length(rel);
                        rsum += r; ++n;
                        if (r < 0.5f) continue;
                        float ang = std::atan2(glm::dot(rel, e1), glm::dot(rel, e0));
                        int k = (int)((ang + 3.14159f) / (2.f * 3.14159f) * kSectors);
                        if (k >= 0 && k < kSectors) hit[k] = true;
                    }
                    int covered = 0;
                    for (int k = 0; k < kSectors; ++k) if (hit[k]) ++covered;
                    ofLogNotice("SimTest")
                        << "    " << g.panelNames[p] << " wraps the arm "
                        << (100 * covered / kSectors) << "% of the way round, averaging "
                        << ofToString(n ? rsum / n : 0.0, 1) << " cm off its centre line"
                        << " (arm radius " << ofToString(armR, 1) << " cm)"
                        // An open underarm seam is a gap on purpose: the
                        // sleeve is laid over the arm and has not been
                        // sewn shut yet. What would be wrong is a sleeve
                        // that only covers one side of the limb, or one
                        // floating well off it.
                        << ((covered * 100 / kSectors >= 65 && n
                             && rsum / n < armR + 9.0)
                                ? "" : "   <-- NOT around the arm");
                }
            }
        }
        ofLogNotice("SimTest") << "  seam sides started " << ofToString(g.weldGapAvg,2)
                                << " cm apart on average, worst " << ofToString(g.weldGapMax,2) << " cm";
        ofLogNotice("SimTest") << "  seams applied " << g.seamsApplied
                                << ", skipped " << g.seamsSkipped
                                << ", welded vertex pairs " << g.weldedPairs;
        for (auto& r : g.seamReport)
            ofLogNotice("SimTest") << "    seam \"" << r.name << "\": "
                                    << r.pairs << " pairs, avg " << ofToString(r.avgGap, 1)
                                    << " cm apart, worst " << ofToString(r.maxGap, 1) << " cm"
                                    << (r.reversed ? "  [matched reversed]" : "");
        if (g.empty()) { ofLogNotice("SimTest") << "FAIL: nothing to simulate"; std::exit(1); }

        auto worstStretch = [&](bool report = false) {
            double worst = 0.0; int wi = -1;
            float minRest = 1e9f; int tiny = 0;
            for (size_t i = 0; i < g.stretch.size(); ++i) {
                auto& c = g.stretch[i];
                minRest = std::min(minRest, c.rest);
                if (c.rest < 0.2f) ++tiny;
                if (c.rest < 1e-4f) continue;
                double r = glm::distance(g.pos[c.a], g.pos[c.b]) / c.rest;
                if (std::fabs(r - 1.0) > worst) { worst = std::fabs(r - 1.0); wi = (int)i; }
            }
            if (report && wi >= 0) {
                auto& c = g.stretch[wi];
                ofLogNotice("SimTest") << "    shortest rest edge " << ofToString(minRest, 3)
                                        << " cm; " << tiny << " edges under 2 mm";
                ofLogNotice("SimTest") << "    worst edge: rest " << ofToString(c.rest, 3)
                                        << " now " << ofToString(glm::distance(g.pos[c.a], g.pos[c.b]), 2)
                                        << " cm, panels " << g.panelOf[c.a] << "/" << g.panelOf[c.b];
                glm::vec3 p = g.pos[c.a];
                ofLogNotice("SimTest") << "    worst edge at (" << ofToString(p.x,1) << ", "
                                        << ofToString(p.y,1) << ", " << ofToString(p.z,1)
                                        << ")  -- y is cm below the nape";
                // How many badly-stretched edges there are, and where.
                int nearArm = 0, bad = 0;
                for (auto& e : g.stretch) {
                    if (e.rest < 1e-4f) continue;
                    double r = glm::distance(g.pos[e.a], g.pos[e.b]) / e.rest;
                    if (r < 1.5) continue;
                    ++bad;
                    if (g.pos[e.a].y > -30.f) ++nearArm;
                }
                {
                    // The worst single edge says little: what matters is
                    // how most of the cloth is behaving.
                    std::vector<double> rs;
                    for (auto& e : g.stretch) {
                        if (e.rest < 1e-4f) continue;
                        rs.push_back(std::fabs(glm::distance(g.pos[e.a], g.pos[e.b]) / e.rest - 1.0));
                    }
                    std::sort(rs.begin(), rs.end());
                    auto pct = [&](double f) { return rs.empty() ? 0.0 : rs[(size_t)(f * (rs.size() - 1))]; };
                    ofLogNotice("SimTest") << "    stretch spread: median "
                        << ofToString(pct(0.5) * 100, 1) << "%, 90th "
                        << ofToString(pct(0.9) * 100, 1) << "%, 99th "
                        << ofToString(pct(0.99) * 100, 1) << "%, max "
                        << ofToString(pct(1.0) * 100, 1) << "%";
                }
                ofLogNotice("SimTest") << "    " << bad << " edges over 50% stretched, "
                                        << nearArm << " of them above 30 cm below the nape "
                                        << "(shoulder/armhole region)";
            }
            return worst;
        };
        auto finite = [&]() {
            for (auto& p : g.pos)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
            return true;
        };

        int steps = std::atoi(std::getenv("PATTERNFORGE_SIM_STEPS") ? std::getenv("PATTERNFORGE_SIM_STEPS") : "120");
        uint64_t tSim0 = ofGetElapsedTimeMillis(); pf::sim::gProfile.reset();
        float y0 = 0.f; for (auto& p : g.pos) y0 = std::min(y0, p.y);
        {
            // Where the PLACEMENT itself is distorted, before a single
            // step: anything bad here the solver has to undo rather than
            // simply hold, and it is the placement's fault, not physics'.
            std::vector<std::pair<double,int>> rs;
            for (size_t i = 0; i < g.stretch.size(); ++i) {
                auto& c = g.stretch[i];
                if (c.rest < 1e-4f) continue;
                rs.push_back({ glm::distance(g.pos[c.a], g.pos[c.b]) / c.rest - 1.0, (int)i });
            }
            std::sort(rs.begin(), rs.end());
            std::map<std::string,int> byPanel;
            for (size_t k = rs.size() > 40 ? rs.size() - 40 : 0; k < rs.size(); ++k)
                ++byPanel[g.panelNames[g.panelOf[g.stretch[rs[k].second].a]]];
            std::string where;
            for (auto& kv : byPanel) where += kv.first + " x" + ofToString(kv.second) + "  ";
            if (!rs.empty()) {
                ofLogNotice("SimTest") << "  placement: worst 40 stretched edges are on " << where;
                for (size_t k = rs.size() - std::min<size_t>(5, rs.size()); k < rs.size(); ++k) {
                    auto& c = g.stretch[rs[k].second];
                    glm::vec3 p1 = g.pos[c.a], p2 = g.pos[c.b];
                    ofLogNotice("SimTest") << "      " << ofToString(rs[k].first * 100, 0)
                        << "%: rest " << ofToString(c.rest, 2) << " cm, "
                        << g.panelNames[g.panelOf[c.a]]
                        << " (" << ofToString(p1.x,1) << "," << ofToString(p1.y,1) << "," << ofToString(p1.z,1)
                        << ") -> (" << ofToString(p2.x,1) << "," << ofToString(p2.y,1) << "," << ofToString(p2.z,1) << ")";
                }
            }
        }
        ofLogNotice("SimTest") << "  stretch at step 0: "
                                << ofToString(worstStretch() * 100.0, 1) << "%";
        {
            // One step, then look hard at what moved. A spike this fast is
            // constraints fighting each other, not gravity.
            std::set<int> onSeam;
            for (auto& sp : g.seamPairs) { onSeam.insert(sp.first); onSeam.insert(sp.second); }
            std::vector<glm::vec3> before = g.pos;
            pf::sim::step(g, ss, 1.0 / 60.0);
            int wi = -1; double worst = 0;
            for (size_t i = 0; i < g.stretch.size(); ++i) {
                auto& c = g.stretch[i];
                if (c.rest < 1e-4f) continue;
                double r = std::fabs(glm::distance(g.pos[c.a], g.pos[c.b]) / c.rest - 1.0);
                if (r > worst) { worst = r; wi = (int)i; }
            }
            if (wi >= 0) {
                auto& c = g.stretch[wi];
                ofLogNotice("SimTest") << "  after ONE step, worst edge "
                    << ofToString(worst * 100, 0) << "%: rest " << ofToString(c.rest, 2)
                    << " now " << ofToString(glm::distance(g.pos[c.a], g.pos[c.b]), 2) << " cm";
                ofLogNotice("SimTest") << "    endpoints on a seam? a="
                    << (onSeam.count(c.a) ? "yes" : "no") << " b=" << (onSeam.count(c.b) ? "yes" : "no");
                ofLogNotice("SimTest") << "    moved this step: a="
                    << ofToString(glm::distance(before[c.a], g.pos[c.a]), 2) << " cm, b="
                    << ofToString(glm::distance(before[c.b], g.pos[c.b]), 2) << " cm";
                ofLogNotice("SimTest") << "    at y=" << ofToString(g.pos[c.a].y, 1)
                                        << " (cm below nape)";
            }
        }
        // Sew first, then drape -- and report them separately, because
        // they fail for different reasons. Stretch left after sewing is a
        // placement or a seam-matching fault; stretch that appears only
        // once gravity is on is the drape's.
        while (g.assembling()) {
            pf::sim::step(g, ss, 1.0 / 60.0);
            if (!finite()) { ofLogNotice("SimTest") << "FAIL: blew up while sewing"; std::exit(1); }
        }
        if (ss.assemblySteps > 0)
            ofLogNotice("SimTest") << "  sewn in " << ss.assemblySteps << " frames: seam gap "
                                    << ofToString(g.seamGapAfterAssembly, 2) << " cm, stretch "
                                    << ofToString(worstStretch() * 100.0, 1) << "%";
        for (int i = 0; i < steps; ++i) {
            pf::sim::step(g, ss, 1.0 / 60.0);
            if (!finite()) { ofLogNotice("SimTest") << "FAIL: blew up at step " << i; std::exit(1); }
            if (i == 0 || i == 2 || i == 9 || i == 29 || i == 59)
                ofLogNotice("SimTest") << "  stretch after step " << (i + 1) << ": "
                                        << ofToString(worstStretch() * 100.0, 1) << "%";
        }
        float y1 = 0.f; for (auto& p : g.pos) y1 = std::min(y1, p.y);
        // Cloth inside the body would mean the collider did nothing -- so
        // measure against the body the collider ACTUALLY used. Counting
        // capsules while the drape ran on an avatar failed a third of the
        // garment for being inside a torso that was never there.
        int inside = 0;
        for (auto& p : g.pos) {
            if (g.avatar) {
                glm::vec3 q = p;
                if (g.avatar->pushOut(q, 0.f) && glm::distance(q, p) > 0.5f) ++inside;
                continue;
            }
            for (auto& cap : g.body) {
                glm::vec3 ab = cap.b - cap.a;
                float l2 = glm::dot(ab, ab);
                float t = l2 < 1e-6f ? 0.f : glm::clamp(glm::dot(p - cap.a, ab) / l2, 0.f, 1.f);
                if (glm::distance(p, cap.a + ab * t) < cap.r - 0.5f) { ++inside; break; }
            }
        }
        uint64_t tSim1 = ofGetElapsedTimeMillis();
        ofLogNotice("SimTest") << "  after " << steps << " steps ("
                                << ofToString((tSim1 - tSim0) / (double)steps, 1) << " ms/step):";
        ofLogNotice("SimTest") << "    constraints " << ofToString(pf::sim::gProfile.constraintsMs/steps,1)
                                << " ms/step, collision " << ofToString(pf::sim::gProfile.collisionMs/steps,1) << " ms/step";
        if (g.avatar) {
            int through = 0;
            for (auto& p : g.pos) {
                glm::vec3 q = p;
                if (g.avatar->pushOut(q, 0.f) && glm::distance(q, p) > 0.6f) ++through;
            }
            ofLogNotice("SimTest") << "    cloth inside the body: " << through
                                    << " of " << g.pos.size();
        }
        ofLogNotice("SimTest") << "    worst edge stretch " << ofToString(worstStretch(true) * 100.0, 1) << "%";
        ofLogNotice("SimTest") << "    lowest point " << ofToString(y0, 1) << " -> " << ofToString(y1, 1) << " cm";
        ofLogNotice("SimTest") << "    verts inside the body: " << inside << " of " << g.pos.size();
        bool ok = finite() && worstStretch() < 0.35 && g.seamsApplied > 0
               && inside < (int)g.pos.size() / 20;
        ofLogNotice("SimTest") << (ok ? "PASS: it hangs together" : "FAIL: see above");
        std::exit(ok ? 0 : 1);
    }

    // Machine-readable dump of every garment's inputs:    // Machine-readable dump of every garment's inputs:
    // `PATTERNFORGE_DUMP_SPECS=1 ./patternForge`. Anything authoring a
    // design-project.json needs exactly this -- the garment keys, the size
    // presets and every style parameter with its range -- and reading it
    // out of the app means it cannot drift from what the app accepts.
    if (const char* d = std::getenv("PATTERNFORGE_DUMP_SPECS")) {
        if (std::string(d) == "1") {
            ofJson all = ofJson::array();
            for (auto& mod : modules_) {
                ofJson m;
                m["garmentKey"] = mod->key();
                m["displayName"] = mod->displayName();
                ofJson presets = ofJson::array();
                for (auto& p : mod->sizePresets()) {
                    ofJson pj;
                    pj["name"] = p.name;
                    ofJson cm;
                    for (auto& kv : p.cm) cm[kv.first] = kv.second;
                    pj["measurementsCm"] = cm;
                    presets.push_back(pj);
                }
                m["sizePresets"] = presets;
                ofJson specs = ofJson::array();
                for (auto& s : mod->styleParamSpecs()) {
                    ofJson sj;
                    sj["key"] = s.key;
                    sj["label"] = s.label;
                    sj["min"] = s.minV;
                    sj["max"] = s.maxV;
                    sj["default"] = s.defaultV;
                    if (!s.choices.empty()) sj["choices"] = s.choices;
                    specs.push_back(sj);
                }
                m["styleParams"] = specs;
                all.push_back(m);
            }
            std::cout << all.dump(2) << std::endl;
            std::exit(0);
        }
    }

    // Headless smoke-test hook: `PATTERNFORGE_AUTOEXPORT=1 ./patternForge`
    // exports the default design for every module, then re-builds (no
    // full export -- just geometry + checks) once per choice of every
    // discrete style param (e.g. Dress's Silhouette), so a per-option
    // regression -- one silhouette producing a degenerate piece -- fails
    // the smoke test instead of hiding behind the one combination that
    // happened to be the default. Quits when done.
    if (const char* auto_ = std::getenv("PATTERNFORGE_AUTOEXPORT")) {
        if (std::string(auto_) == "1") {
            bool allOk = true;
            // When a garment (and optional style values) was named, export
            // exactly that design instead of sweeping every module's
            // defaults -- otherwise the sweep would overwrite it.
            if (std::getenv("PATTERNFORGE_3D_GARMENT")) {
                doExport();
                ofLogNotice("AutoExport") << design_.title << ": " << lastExportMessage_;
                ofLogNotice("AutoExport") << "  checks: " << (design_.checks.allPass() ? "OK" : "FAILED");
                std::exit(design_.checks.allPass() && lastExportMessage_.rfind("Exported", 0) == 0 ? 0 : 1);
            }
            for (int i = 0; i < (int)modules_.size(); ++i) {
                paramState_.moduleIndex = i;
                pf::ui::setDefaultsForModule(modules_[i], paramState_);
                rebuild();
                doExport();
                ofLogNotice("AutoExport") << modules_[i]->displayName() << ": " << lastExportMessage_;
                if (lastExportMessage_.rfind("Exported", 0) != 0) allOk = false;

                for (auto& spec : modules_[i]->styleParamSpecs()) {
                    if (spec.choices.empty()) continue;
                    for (int c = 0; c < (int)spec.choices.size(); ++c) {
                        paramState_.style.set(spec.key, (float)c);
                        rebuild();
                        bool pass = design_.checks.allPass();
                        ofLogNotice("AutoExport") << "  " << modules_[i]->displayName() << " / " << spec.label
                                                   << " = " << spec.choices[c] << ": "
                                                   << (pass ? "checks OK" : "CHECKS FAILED");
                        if (!pass) allOk = false;
                    }
                    paramState_.style.set(spec.key, spec.defaultV);
                }
            }
            std::exit(allOk ? 0 : 1);
        }
    }
}

void ofApp::rebuild() {
    if (modules_.empty()) return;
    auto& module = modules_[paramState_.moduleIndex];
    auto presets = module->sizePresets();
    SizePreset size = presets.empty() ? SizePreset{} : presets[paramState_.sizePresetIndex];
    design_ = module->build(size, paramState_.style);
    applyEdits();          // point edits re-applied on top of the generated shape
    garment3D_ = render3d::buildGarment3D(design_);
    preview3D_.frame(garment3D_);
}

void ofApp::doLoadDesign() {
    auto result = ofSystemLoadDialog("Load design-project.json");
    if (!result.bSuccess) return;
    loadDesignFile(result.filePath);
}

// The whole of loading, with the file dialog taken off the front, so a
// headless check exercises exactly what the GUI does rather than a second
// implementation that could agree with the file and not with the app.
void ofApp::loadDesignFile(const std::string& path) {
    auto result = ofFile(path);
    ofJson j = ofLoadJson(path);
    if (j.empty()) { lastExportMessage_ = "Load failed: not valid JSON."; return; }

    std::string garmentKey = j.value("garmentKey", "");
    int moduleIdx = -1;
    for (int i = 0; i < (int)modules_.size(); ++i) if (modules_[i]->key() == garmentKey) { moduleIdx = i; break; }
    if (moduleIdx < 0) { lastExportMessage_ = "Load failed: unknown garment \"" + garmentKey + "\"."; return; }

    paramState_.moduleIndex = moduleIdx;
    pf::ui::setDefaultsForModule(modules_[moduleIdx], paramState_);

    std::string sizeName = j.value("sizePreset", "");
    auto presets = modules_[moduleIdx]->sizePresets();
    for (int i = 0; i < (int)presets.size(); ++i) if (presets[i].name == sizeName) { paramState_.sizePresetIndex = i; break; }

    if (j.contains("style")) {
        for (auto it = j["style"].begin(); it != j["style"].end(); ++it) {
            if (it.value().is_number()) paramState_.style.set(it.key(), it.value().get<float>());
        }
    }
    if (j.contains("outlineEdits")) editsFromJson(j["outlineEdits"]);
    else { edits_.clear(); paths_.clear(); }
    rebuild();
    lastExportMessage_ = "Loaded " + garmentKey + " design from " + path +
                         (edits_.empty() ? "" : " (with " + ofToString((int)edits_.size()) + " edited outline(s))");
}

void ofApp::doExport() {
    ofDirectory(exportRootDir_).create(true);
    // Stamped so an export never lands on top of an earlier one -- the
    // previous package is how you compare a change against what you had.
    std::string stamp = ofGetTimestampString("%Y%m%d-%H%M%S");
    auto result = exportx::writePackage(design_, exportRootDir_, editsToJson(), stamp);
    if (result.ok) {
        lastExportMessage_ = "Exported to " + result.folderPath + " (" +
                              ofToString(result.fullSizeBoards) + " boards, " +
                              ofToString(result.a4Pages) + " A4 pages, " +
                              ofToString(result.letterPages) + " Letter pages, " +
                              ofToString(result.instructionPages) + "-page manual, " +
                              ofToString(result.dxfEntities) + " DXF entities, " +
                              ofToString(result.dxfPanels) + " physical panels; muslin: " +
                              ofToString(result.muslinPieces) + " pieces, " +
                              ofToString(result.muslinBoards) + " boards, " +
                              ofToString(result.muslinA4Pages) + " A4 pages). Zip: " + result.zipPath;
    } else {
        lastExportMessage_ = "Export failed: " + result.error;
    }
}



// A piece cut on the fold only has half its outline stored, so an edit
// must leave that fold edge intact for the mirrored half to meet it.
void ofApp::constrainEdit(const std::string& code) {
    for (auto& piece : design_.pieces) {
        if (piece.code == code && piece.foldAtCF) {
            auto it = paths_.find(code);
            if (it != paths_.end()) it->second.constrainToFold();
            return;
        }
    }
}

void ofApp::applyEdits() {
    int edited = 0;
    for (auto& piece : design_.pieces) {
        // The generated outline is always the base an edit is applied to,
        // so moving a slider still reshapes the piece.
        pf::EditablePath base = pf::EditablePath::fromRing(piece.sewing, editDetailCm_);
        basePaths_[piece.code] = base;

        auto it = edits_.find(piece.code);
        if (it == edits_.end() || !it->second.anyEdit()) {
            paths_[piece.code] = base;
            continue;
        }
        pf::EditablePath live = it->second.applyTo(base);
        if (piece.foldAtCF) live.constrainToFold();
        paths_[piece.code] = live;
        piece.sewing = live.toRing();
        piece.resolveCutting();
        piece.handEdited = true;
        ++edited;

        // A hand-drawn outline can be made impossible in ways a generated
        // one cannot, so it gets checked on its own terms.
        double area = std::fabs(geo::area(piece.sewing));
        design_.checks.add(piece.code + ": edited outline encloses an area",
                           ofToString(area, 1) + " cm2", area > 1.0);
        design_.checks.add(piece.code + ": edited outline does not cross itself",
                           geo::selfIntersects(piece.sewing) ? "crosses itself" : "clean",
                           !geo::selfIntersects(piece.sewing));
        if (piece.foldAtCF) {
            double worst = 0.0;
            int onFold = 0;
            for (auto& p : piece.sewing) {
                worst = std::min(worst, (double)p.x);
                if (std::fabs(p.x) < 0.05) ++onFold;
            }
            design_.checks.add(piece.code + ": fold edge still on the fold",
                               ofToString(onFold) + " points on the fold, none past it by more than " +
                               ofToString(-worst, 2) + " cm",
                               worst > -0.05 && onFold >= 2);
        }
    }
    // A facing is cut from its panel, so it has to be re-cut from whatever
    // that panel has become. Doing this AFTER the edit pass above is the
    // whole point: the module built each facing from the generated panel,
    // but the panel the facing is sewn to is the edited one.
    for (auto& piece : design_.pieces) {
        if (!piece.isDerived()) continue;
        const pf::Piece* src = nullptr;
        for (auto& s : design_.pieces) if (s.code == piece.derivedFromCode) src = &s;
        if (!src) continue;

        piece.sewing = pf::common::facingBand(src->sewing, piece.facingMarginCm);
        piece.resolveCutting();
        // The facing inherits its panel's provenance: cut from a hand-edited
        // panel, it is no longer what the sliders alone would have drawn.
        piece.handEdited = src->handEdited;
        paths_[piece.code] = pf::EditablePath::fromRing(piece.sewing, editDetailCm_);
        basePaths_[piece.code] = paths_[piece.code];

        pf::common::expectFacingCoversNeckline(design_.checks,
            piece.code + " still covers the " + src->code + " neckline it finishes",
            src->sewing, piece.sewing, 1.0);
    }

    if (edited > 0) {
        design_.checks.add("Hand-edited pieces",
                           ofToString(edited) + " piece(s) carry point edits, applied on top of the "
                           "generated shape -- the sliders still work", true);
    }
    editView_.paths = &paths_;
}

// Records a point just inserted into `code`'s live path at segment `seg`,
// remembering WHERE ALONG the generated outline it sits so that moving a
// slider keeps it.
void ofApp::recordInsertedPoint(const std::string& code, int seg) {
    auto& live = paths_[code];
    int at = seg + 1;
    if (at < 0 || at >= (int)live.anchors.size()) return;
    pf::PointEdit added;
    added.added = true;
    int bseg; float bt, bdist;
    basePaths_[code].closestSegment(live.anchors[at].pos, bseg, bt, bdist);
    auto bparams = basePaths_[code].anchorParameters();
    float t0 = bseg >= 0 && bseg < (int)bparams.size() ? bparams[bseg] : 0.f;
    float t1 = bseg + 1 < (int)bparams.size() ? bparams[bseg + 1] : 1.f;
    if (t1 < t0) t1 += 1.f;
    added.t = std::fmod(t0 + (t1 - t0) * bt, 1.f);
    auto& pts = edits_[code].points;
    pts.insert(pts.begin() + std::min((size_t)at, pts.size()), added);
}

// Splits every segment of a piece at its midpoint, doubling the number of
// control points WITHOUT changing the shape: a De Casteljau split at
// t = 0.5 reproduces the same curve as two curves. So this is pure "give
// me more to grab", with nothing to undo afterwards.
void ofApp::subdivideOutline(const std::string& code) {
    ensureEditRecord(code);
    int before = (int)paths_[code].anchors.size();
    if (before < 2 || before > 200) return;   // a sane ceiling
    // Back to front: inserting shifts the segments after it.
    for (int seg = before - 1; seg >= 0; --seg) {
        paths_[code].insertPoint(seg, 0.5f);
        recordInsertedPoint(code, seg);

    }

    // Now pin EVERY point: its exact position and its exact handles.
    //
    // Splitting a curve does not only add a point, it also shortens the
    // handles of the anchors either side. And an added point is otherwise
    // re-derived on rebuild from its position ALONG the outline, which is
    // measured by arc length while the split was by curve parameter -- a
    // mismatch that grows with segment length, and the center-front fold
    // edge of a dress is one straight segment most of a metre long. On
    // that, re-deriving landed the point almost half a centimetre out.
    //
    // Recording each point's own offset and handles removes the guesswork:
    // the rebuild reproduces exactly what the split produced, whatever the
    // parameterisation does. Which is honest anyway -- once you have asked
    // for these points, they are hand-placed, not generated.
    {
        int n = (int)paths_[code].anchors.size();
        auto& pts = edits_[code].points;
        for (int idx = 0; idx < n && idx < (int)pts.size(); ++idx) {
            pts[idx].handlesSet = true;
            syncEditFromLive(code, idx);
        }
    }
    constrainEdit(code);
    rebuild();
}

// Gives a piece an edit record lined up with its live path, so an edit can
// be recorded against the point the designer actually grabbed.
void ofApp::ensureEditRecord(const std::string& code) {
    auto& rec = edits_[code];
    auto& live = paths_[code];
    if (rec.points.size() == live.anchors.size()) return;
    rec.points.clear();
    auto params = live.anchorParameters();
    for (size_t i = 0; i < live.anchors.size(); ++i) {
        pf::PointEdit pe;
        pe.t = i < params.size() ? params[i] : 0.f;
        rec.points.push_back(pe);
    }
}

// Records where a point ended up, measured against where the generated
// outline puts it -- that difference is the edit.
void ofApp::syncEditFromLive(const std::string& code, int anchor) {
    auto& rec = edits_[code];
    auto& live = paths_[code];
    if (anchor < 0 || anchor >= (int)live.anchors.size() || anchor >= (int)rec.points.size()) return;
    const pf::Anchor& a = live.anchors[anchor];
    pf::PointEdit& pe = rec.points[anchor];
    pf::Anchor basePoint = basePaths_[code].anchorAtParameter(pe.t);
    pe.posOffset = a.pos - basePoint.pos;
    pe.inRel = a.inH - a.pos;
    pe.outRel = a.outH - a.pos;
}

ofRectangle ofApp::canvasViewport() const {
    return ofRectangle(300, 0, ofGetWidth() - 600.f, (float)ofGetHeight());
}

void ofApp::resetEdit(const std::string& code) {
    edits_.erase(code);
    paths_.erase(code);
    editView_.selPiece = editView_.selAnchor = -1;
    rebuild();
}

ofJson ofApp::editsToJson() const {
    ofJson j = ofJson::object();
    for (auto& kv : edits_) if (kv.second.anyEdit()) j[kv.first] = kv.second.toJson();
    return j;
}

void ofApp::editsFromJson(const ofJson& j) {
    edits_.clear();
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it)
        edits_[it.key()] = pf::OutlineEdits::fromJson(it.value());
}


std::string ofApp::seamPartner(const std::string& code) const {
    // The panels that are sewn to each other down the side seam.
    if (code == "F") return "B";
    if (code == "B") return "F";
    if (code == "SK") return "SKB";
    if (code == "SKB") return "SK";
    return "";
}

void ofApp::mirrorSeamEdit(const std::string& code, int anchor) {
    std::string partner = seamPartner(code);
    if (partner.empty() || !paths_.count(partner) || !basePaths_.count(code)) return;
    auto& live = paths_[code];
    if (anchor < 0 || anchor >= (int)live.anchors.size()) return;

    const pf::Anchor& moved = live.anchors[anchor];
    const pf::Piece* self = nullptr;
    const pf::Piece* other = nullptr;
    for (auto& p : design_.pieces) {
        if (p.code == code) self = &p;
        if (p.code == partner) other = &p;
    }
    if (!self || !other) return;

    // Only side-seam points travel: that is the outer edge, below the
    // armhole. The neckline, the shoulder and the fold are each panel's
    // own business.
    const pf::EditablePath& base = basePaths_[code];
    pf::Anchor basePoint = base.anchorAtParameter(edits_[code].points[anchor].t);
    double seamX = geo::maxXAtY(basePaths_[code].toRing(), basePoint.pos.y);
    bool onSideSeam = basePoint.pos.x > seamX - 0.6 &&
                      basePoint.pos.y > design_.armholeY + 1.0;
    if (!onSideSeam) return;

    // Find the partner's point at the same height on its side seam, or add
    // one there, then give it the same offset.
    auto& partnerLive = paths_[partner];
    ensureEditRecord(partner);
    float y = basePoint.pos.y;
    int bestIdx = -1;
    float bestDy = 2.0f;
    for (size_t i = 0; i < partnerLive.anchors.size(); ++i) {
        const pf::Anchor& a = partnerLive.anchors[i];
        double sx = geo::maxXAtY(basePaths_[partner].toRing(), a.pos.y);
        if (a.pos.x < sx - 0.6) continue;             // not on the side seam
        float dy = std::fabs(a.pos.y - y);
        if (dy < bestDy) { bestDy = dy; bestIdx = (int)i; }
    }
    if (bestIdx < 0) {
        // No point there yet: split the partner's side seam at that height.
        Pt target((float)geo::maxXAtY(basePaths_[partner].toRing(), y), y);
        int seg; float t, dist;
        partnerLive.closestSegment(target, seg, t, dist);
        if (seg < 0) return;
        partnerLive.insertPoint(seg, t);
        pf::PointEdit added;
        added.added = true;
        int bseg; float bt, bdist;
        basePaths_[partner].closestSegment(target, bseg, bt, bdist);
        auto bparams = basePaths_[partner].anchorParameters();
        float t0 = bseg >= 0 && bseg < (int)bparams.size() ? bparams[bseg] : 0.f;
        float t1 = bseg + 1 < (int)bparams.size() ? bparams[bseg + 1] : 1.f;
        if (t1 < t0) t1 += 1.f;
        added.t = std::fmod(t0 + (t1 - t0) * bt, 1.f);
        auto& pts = edits_[partner].points;
        pts.insert(pts.begin() + std::min((size_t)(seg + 1), pts.size()), added);
        bestIdx = seg + 1;
    }
    if (bestIdx >= (int)edits_[partner].points.size()) return;
    // Same offset, so both seams stay the same shape and length.
    edits_[partner].points[bestIdx].posOffset = edits_[code].points[anchor].posOffset;
    pf::Anchor partnerBase = basePaths_[partner].anchorAtParameter(edits_[partner].points[bestIdx].t);
    pf::Anchor& pa = partnerLive.anchors[bestIdx];
    Pt delta = (partnerBase.pos + edits_[partner].points[bestIdx].posOffset) - pa.pos;
    partnerLive.moveAnchor(bestIdx, delta);
}

void ofApp::loadAvatar(int which) {
    avatarChoice_ = which;
    if (which == 0) { avatar_ = pf::sim::Avatar(); return; }
    const char* file = (which == 2) ? "avatar/male_m.obj" : "avatar/female_m.obj";
    std::string path = ofToDataPath(file, true);
    if (!avatar_.load(path, 23.0, avatarDetailCm_)) {
        ofLogWarning("Avatar") << "could not load " << path << " -- falling back to capsules";
        avatarChoice_ = 0;
        return;
    }
    // Fit the body to the size being drafted, or the garment is asked to
    // drape over someone it was never cut for.
    avatar_.fitGirthTo(design_.size.get("hip", 94.0), design_.size.get("bust", 86.4));
}

void ofApp::startSim() {
    if (avatarChoice_ != 0 && !avatar_.valid()) loadAvatar(avatarChoice_);
    sim_ = pf::sim::build(design_, simSettings_, avatarChoice_ ? &avatar_ : nullptr);
    simSteps_ = 0;
}

void ofApp::stepSim() {
    if (sim_.empty()) return;
    pf::sim::step(sim_, simSettings_, 1.0 / 60.0);
    ++simSteps_;

    simMesh_.clear();
    simMesh_.setMode(OF_PRIMITIVE_TRIANGLES);
    for (auto& p : sim_.pos) {
        simMesh_.addVertex(p);
        simMesh_.addColor(ofFloatColor(0.86f, 0.84f, 0.80f));
    }
    for (size_t t = 0; t + 2 < sim_.tris.size(); t += 3)
        simMesh_.addTriangle(sim_.tris[t], sim_.tris[t + 1], sim_.tris[t + 2]);
    // The cloth changes shape every frame, so its normals do too.
    pf::ui::Preview3D::computeNormals(simMesh_);
}

void ofApp::update() {
    if (simulate_) stepSim();
    // The file dialog runs a modal event loop of its own, so it can only
    // be opened between frames -- never inside the ImGui frame that the
    // button was drawn in.
    if (wantLoadDialog_) {
        wantLoadDialog_ = false;
        doLoadDesign();
    }
}

void ofApp::dragEvent(ofDragInfo info) {
    for (auto& f : info.files) {
        if (ofFilePath::getFileExt(f) != "json") continue;
        loadDesignFile(f);
        return;
    }
}

void ofApp::draw() {
    ofRectangle viewport(300, 0, ofGetWidth() - 600, ofGetHeight());
    editView_.active = editOutlines_ && !show3D_;
    editView_.paths = &paths_;
    if (show3D_) {
        if (simulate_ && !sim_.empty())
            preview3D_.setFabric(fabric_),
            preview3D_.drawDrape(simMesh_, sim_.body, viewport,
                                 avatarChoice_ && avatar_.valid() ? &avatar_.mesh() : nullptr);
        else preview3D_.draw(garment3D_, viewport);
    }
    else canvas_.draw(design_, viewport, editView_);

    gui_.begin();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300, (float)ofGetHeight()));
    ImGui::Begin("Design", nullptr, flags);
    bool changed = pf::ui::drawParamPanel(modules_, paramState_);
    if (changed) rebuild();
    ImGui::Separator();
    if (ImGui::Checkbox("3D preview (rough silhouette, no drape)", &show3D_)) preview3D_.setActive(show3D_);
    if (show3D_) {
        ImGui::TextDisabled("Drag to orbit, scroll to zoom.");
        if (ImGui::Button("Reset view", ImVec2(-1, 0))) preview3D_.frame(garment3D_);
        ImGui::Separator();
        if (ImGui::Checkbox("Drape simulation (EXPERIMENTAL)", &simulate_)) {
            if (simulate_) startSim();
        }
        if (simulate_) {
            ImGui::TextWrapped("The real panels, meshed and sewn along the seam list, falling "
                               "under gravity against a body built from the size preset.");
            const char* bodies[] = { "Capsules (rough)", "Female avatar", "Male avatar" };
            if (ImGui::Combo("Body", &avatarChoice_, bodies, 3)) { loadAvatar(avatarChoice_); startSim(); }
            if (avatar_.valid()) {
                ImGui::Text("%s: %.0f cm", avatar_.name().c_str(), avatar_.heightCm());
                ImGui::Text("%d tris (from %d)", (int)avatar_.triangleCount(),
                            (int)avatar_.originalTriangleCount());
                if (ImGui::SliderFloat("Body detail (cm)", &avatarDetailCm_, 0.f, 6.f, "%.1f")) {
                    loadAvatar(avatarChoice_);
                    startSim();
                }
                ImGui::TextDisabled("Larger merges more of the body mesh.");
            }
            // Solver quality. Raising these is the fix when cloth pulls
            // apart -- it means the constraints are not converging.
            const char* fabrics[] = { "Cotton poplin", "Wool suiting", "Silk crepe",
                                      "Denim", "Cotton jersey", "Leather" };
            if (ImGui::Combo("Fabric", &fabric_, fabrics, 6)) {
                pf::sim::applyFabric(simSettings_, fabric_);
                startSim();
            }
            if (ImGui::SliderFloat("Start standoff (cm)", &startInflate_, 0.f, 20.f, "%.1f")) {
                simSettings_.startInflateCm = startInflate_;
                startSim();
            }
            ImGui::TextDisabled("How far off the body the panels start.");
            ImGui::SliderInt("Passes/substep", &simSettings_.iterations, 4, 80);
            ImGui::SliderInt("Substeps/frame", &simSettings_.substeps, 1, 16);
            ImGui::TextDisabled("Higher = stiffer seams, steadier cloth, slower.");
            // Four sliders sitting together are easy to knock, and a drape
            // run with the wrong ones looks like a broken simulation rather
            // than a mis-set one. One click puts them all back.
            if (ImGui::Button("Reset drape settings")) {
                pf::sim::SimSettings fresh;
                fresh.resolutionCm = simSettings_.resolutionCm;
                simSettings_ = fresh;
                fabric_ = 0;
                startInflate_ = (float)fresh.startInflateCm;
                startSim();
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart drape", ImVec2(-1, 0))) startSim();
            ImGui::Text("%d panels, %d verts, %d tris",
                        (int)sim_.panelNames.size(), (int)sim_.pos.size(),
                        (int)sim_.tris.size() / 3);
            ImGui::Text("%d seams sewn, %d skipped", sim_.seamsApplied, sim_.seamsSkipped);
            ImGui::Text("step %d", simSteps_);
            // Which of the two phases is running, and how far through.
            // Cloth that is still being sewn looks wrong on purpose, and
            // without this the sewing reads as a broken drape.
            if (sim_.assembling()) {
                ImGui::Text("SEWING %d%% -- no gravity yet",
                            (int)(100.0 * sim_.assemblyStep / std::max(1, sim_.assemblyTotal)));
                ImGui::ProgressBar((float)sim_.assemblyStep / std::max(1, sim_.assemblyTotal),
                                   ImVec2(-1, 0));
            } else {
                ImGui::Text("DRAPING -- seam gap after sewing: %.2f cm",
                            sim_.seamGapAfterAssembly);
            }
            ImGui::TextWrapped("The panels start %.1f cm apart at the seams (worst %.1f), held "
                               "off the body, and are drawn together and sewn before gravity "
                               "is applied.", sim_.weldGapAvg, sim_.weldGapMax);
            if (ImGui::SliderInt("Sewing frames", &simSettings_.assemblySteps, 0, 300)) startSim();
            // The spread, not just the worst: one pinched edge says
            // nothing about how the garment is hanging.
            std::vector<double> rs;
            for (auto& c : sim_.stretch) {
                if (c.rest < 1e-4f) continue;
                rs.push_back(std::fabs(glm::distance(sim_.pos[c.a], sim_.pos[c.b]) / c.rest - 1.0));
            }
            if (!rs.empty()) {
                std::sort(rs.begin(), rs.end());
                ImGui::Text("stretch: median %.1f%%, 90th %.1f%%, worst %.0f%%",
                            rs[rs.size() / 2] * 100.0,
                            rs[(size_t)(rs.size() * 0.9)] * 100.0,
                            rs.back() * 100.0);
            }
        }
    } else {
        ImGui::Checkbox("Show cutting line", &canvas_.showCutting);
        ImGui::Checkbox("Show sewing line", &canvas_.showSewing);
        ImGui::Checkbox("Show grainlines", &canvas_.showGrain);
        ImGui::Checkbox("Show marks", &canvas_.showMarks);
        if (ImGui::Button("Reset view", ImVec2(-1, 0))) canvas_.resetView();
        ImGui::Separator();
        if (ImGui::Checkbox("Edit base outlines", &editOutlines_)) {
            editView_.selPiece = editView_.selAnchor = -1;
            editView_.selAnchors.clear();
        }
        if (editOutlines_) {
            ImGui::TextWrapped("Drag a point to move it. Click a point to show its curve handles, "
                               "then drag those to bend the line. Double-click a line to add a point. "
                               "To remove a point, select it and press Delete.");
            // How many points there are to grab, and a way to make more.
            if (ImGui::SliderFloat("Point spacing (cm)", &editDetailCm_, 0.02f, 0.6f, "%.2f")) {
                // Edits are keyed by position ALONG the outline, not by
                // index, so they survive the path being rebuilt denser.
                paths_.clear();
                rebuild();
            }
            ImGui::TextDisabled("Smaller keeps more of the drafted points.");
            if (editView_.selPiece >= 0 && editView_.selPiece < (int)design_.pieces.size()) {
                const std::string& code = design_.pieces[editView_.selPiece].code;
                std::string label = "Add points to " + code + " (split every segment)";
                if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) subdivideOutline(code);
                ImGui::TextDisabled("Doubles the points without changing the shape.");
            } else {
                ImGui::TextDisabled("Select a piece to add points to it.");
            }
            ImGui::Separator();
            ImGui::TextWrapped("Several at once: shift-drag on empty space to rubber-band a group, "
                               "or shift-click points to add and remove them. Dragging any point of "
                               "a group moves the whole group together, and Delete removes all of "
                               "them. A group stays within one piece.");
            std::vector<std::string> codes;
            for (auto& kv : edits_) if (kv.second.anyEdit()) codes.push_back(kv.first);
            if (!codes.empty()) {
                ImGui::TextDisabled("Edited pieces (these still follow the sliders):");
                for (auto& code : codes) {
                    ImGui::PushID(code.c_str());
                    ImGui::Text("%s", code.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset")) resetEdit(code);
                    ImGui::PopID();
                }
                if (ImGui::Button("Reset all outline edits", ImVec2(-1, 0))) {
                    edits_.clear();
                    paths_.clear();
                    editView_.selPiece = editView_.selAnchor = -1;
                    rebuild();
                }
            }
        }
    }
    ImGui::Separator();
    // Three ways in, because the native dialog is the fragile one: drop a
    // file on the window, paste a path, or use the dialog.
    ImGui::TextDisabled("Load a design: drop a .json on this window,");
    ImGui::TextDisabled("or paste its path here:");
    ImGui::PushItemWidth(-60);
    bool entered = ImGui::InputText("##loadpath", loadPathBuf_, sizeof(loadPathBuf_),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if ((ImGui::Button("Load") || entered) && loadPathBuf_[0]) {
        std::string p(loadPathBuf_);
        // Paths dragged into a terminal arrive quoted or escaped.
        if (p.size() > 1 && (p.front() == '"' || p.front() == '\'')) p = p.substr(1, p.size() - 2);
        loadDesignFile(p);
    }
    // Deferred, never called here: opening the file dialog between
    // gui_.begin() and gui_.end() locks the app up.
    if (ImGui::Button("Load design-project.json...", ImVec2(-1, 0))) wantLoadDialog_ = true;
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2((float)(ofGetWidth() - 300), 0));
    ImGui::SetNextWindowSize(ImVec2(300, (float)ofGetHeight()));
    ImGui::Begin("Pieces & checks", nullptr, flags);
    ImGui::TextWrapped("%s", design_.title.c_str());
    ImGui::TextDisabled("%d pieces", (int)design_.pieces.size());
    ImGui::Separator();
    for (auto& piece : design_.pieces) {
        ImGui::BulletText("%s: %s (%s)", piece.code.c_str(), piece.name.c_str(), piece.cutQty.c_str());
    }
    ImGui::Separator();
    pf::ui::drawChecksPanel(design_.checks);
    ImGui::Separator();
    if (ImGui::Button("Export package", ImVec2(-1, 0))) doExport();
    ImGui::TextWrapped("Export folder: %s", exportRootDir_.c_str());
    if (!lastExportMessage_.empty()) ImGui::TextWrapped("%s", lastExportMessage_.c_str());
    ImGui::End();

    gui_.end();

    // Headless visual-check hook: `PATTERNFORGE_3D_SNAPSHOT=/path.png ./patternForge`
    // forces the 3D view on, lets a few frames render (camera needs a
    // frame to settle after frame()), saves a screenshot, and quits --
    // so the 3D preview can be checked without driving the GUI by hand.
    // Screenshot of the outline editor, for checking the points render.
    if (const char* path = std::getenv("PATTERNFORGE_EDIT_SNAPSHOT")) {
        static int eframes = 0;
        if (eframes == 0) {
            // Optionally pull the waist in first, so the reported case can
            // be looked at rather than only measured.
            if (const char* pull = std::getenv("PATTERNFORGE_WAIST_PULL")) {
                const std::string code = "F";
                editOutlines_ = true;
                editView_.paths = &paths_;
                ensureEditRecord(code);
                const pf::Piece* p = nullptr;
                for (auto& q : design_.pieces) if (q.code == code) p = &q;
                double waistY = design_.size.get("backWaistLength", 40.0);
                int wa = -1; double bestD = 1e9;
                for (size_t i = 0; i < paths_[code].anchors.size(); ++i) {
                    Pt a = paths_[code].anchors[i].pos;
                    double sx = p ? geo::maxXAtY(p->sewing, a.y) : 0.0;
                    if (!(a.x > sx - 0.6 && a.y > design_.armholeY)) continue;
                    double d = std::fabs(a.y - waistY);
                    if (d < bestD) { bestD = d; wa = (int)i; }
                }
                if (wa >= 0) {
                    paths_[code].moveAnchor(wa, Pt((float)std::atof(pull), 0.f));
                    syncEditFromLive(code, wa);
                    rebuild();
                    editView_.selPiece = 0;
                    editView_.selAnchor = wa;
                    editView_.selAnchors = { wa };
                }
            }
            editOutlines_ = true;
            show3D_ = false;
            editView_.selPiece = 0;
            editView_.selAnchor = 3;
            // A small run, so the shot shows the group highlight and the
            // primary point's handles side by side.
            editView_.selAnchors = { 3, 4, 5, 6 };
            editView_.marqueeActive = true;
            editView_.marqueeFrom = glm::vec2(340.f, 90.f);
            editView_.marqueeTo = glm::vec2(430.f, 240.f);
        }
        ++eframes;
        if (eframes == 6) { ofSaveScreen(path); std::exit(0); }
    }
    if (const char* path = std::getenv("PATTERNFORGE_3D_SNAPSHOT")) {
        static int frames = 0;
        if (frames == 0) {
            if (std::getenv("PATTERNFORGE_DRAPE")) { simulate_ = true; startSim(); }
            show3D_ = true;
            preview3D_.setActive(true);
            // PATTERNFORGE_3D_AZIMUTH turns the camera: 180 for a back view.
            if (const char* az = std::getenv("PATTERNFORGE_3D_AZIMUTH"))
                preview3D_.frame(garment3D_, std::stof(az));
        }
        ++frames;
        // Six frames lets the camera settle but is nowhere near enough for
        // cloth to fall, so a drape snapshot taken then shows the assembly
        // rather than the drape. PATTERNFORGE_SNAPSHOT_FRAMES waits longer.
        int want = 6;
        if (const char* f = std::getenv("PATTERNFORGE_SNAPSHOT_FRAMES"))
            want = std::max(6, std::atoi(f));
        if (frames == want) { ofSaveScreen(path); std::exit(0); }
    }
}

void ofApp::mousePressed(int x, int y, int button) {
    lastMouseX_ = x; lastMouseY_ = y;
    if (!editOutlines_ || show3D_ || ImGui::GetIO().WantCaptureMouse) return;

    ofRectangle vp = canvasViewport();
    glm::vec2 screen((float)x, (float)y);

    // A second click in the same spot, soon after the first, is a
    // double-click: the easiest way to add a point on a line.
    float now = ofGetElapsedTimef();
    bool doubleClick = (now - lastClickTime_ < 0.35f) && glm::distance(screen, lastClickPos_) < 8.f;
    lastClickTime_ = now;
    lastClickPos_ = screen;

    // A double-click means "add a point here", so it takes precedence over
    // selecting a nearby one -- at normal zoom the anchors are close enough
    // together that almost any click on the line is also near a point.
    // Only a double-click landing right on top of a point is left alone.
    if (doubleClick) {
        pf::ui::EditHit onPoint = canvas_.hitTest(design_, vp, editView_, screen, 6.f);
        if (onPoint.piece < 0 || onPoint.handle != 0) {
            int seg; float t;
            pf::ui::EditHit onPath = canvas_.hitPath(design_, vp, editView_, screen, 14.f, seg, t);
            if (onPath.piece >= 0) {
                auto& code = design_.pieces[onPath.piece].code;
                ensureEditRecord(code);
                paths_[code].insertPoint(seg, t);
                recordInsertedPoint(code, seg);
                constrainEdit(code);
                syncEditFromLive(code, seg + 1);
                editView_.selPiece = onPath.piece;
                editView_.selAnchor = seg + 1;
                editView_.selHandle = 0;
                draggingPoint_ = true; // so it can be dragged straight away
                rebuild();
                return;
            }
        }
    }

    bool shift = ofGetKeyPressed(OF_KEY_SHIFT);
    pf::ui::EditHit hit = canvas_.hitTest(design_, vp, editView_, screen);
    if (hit.piece >= 0) {
        // A click only ever selects and drags. Removing a point is a
        // deliberate act: select it, then press Delete.
        if (shift && hit.piece == editView_.selPiece && hit.handle == 0) {
            // Shift adds to (or removes from) the selection.
            toggleSelected(hit.anchor);
            editView_.selAnchor = hit.anchor;
            editView_.selHandle = 0;
            draggingPoint_ = !editView_.selAnchors.empty();
            return;
        }
        bool keepGroup = editView_.isSelected(hit.piece, hit.anchor) &&
                         editView_.selAnchors.size() > 1 && hit.handle == 0;
        editView_.selPiece = hit.piece;
        editView_.selAnchor = hit.anchor;
        editView_.selHandle = hit.handle;
        // Clicking a point that is already part of a group keeps the group,
        // so the whole set can be dragged; clicking anything else starts a
        // fresh selection of one.
        if (!keepGroup) editView_.selAnchors = { hit.anchor };
        draggingPoint_ = true;
        return;
    }

    // Shift-drag on empty canvas rubber-bands a selection. Plain drag still
    // pans the view, which is worth more than a second way to select.
    if (shift) {
        editView_.marqueeActive = true;
        editView_.marqueeFrom = editView_.marqueeTo = screen;
        return;
    }
    editView_.selPiece = editView_.selAnchor = -1;
    editView_.selAnchors.clear();
}

// Adds the anchor to the selection, or takes it out if already there.
void ofApp::toggleSelected(int anchor) {
    auto& sel = editView_.selAnchors;
    for (size_t i = 0; i < sel.size(); ++i) {
        if (sel[i] == anchor) { sel.erase(sel.begin() + i); return; }
    }
    sel.push_back(anchor);
}

void ofApp::mouseReleased(int x, int y, int button) {
    if (editView_.marqueeActive) {
        editView_.marqueeActive = false;
        int piece = -1;
        auto picked = canvas_.anchorsInRect(design_, canvasViewport(), editView_,
                                            editView_.marqueeFrom,
                                            glm::vec2((float)x, (float)y), piece);
        if (piece >= 0 && !picked.empty()) {
            editView_.selPiece = piece;
            editView_.selAnchors = picked;
            editView_.selAnchor = picked.front();
            editView_.selHandle = 0;
        }
    }
    draggingPoint_ = false;
}

void ofApp::keyPressed(int key) {
    if ((key == OF_KEY_DEL || key == OF_KEY_BACKSPACE) && editOutlines_ &&
        editView_.selPiece >= 0 && editView_.selAnchor >= 0) {
        auto& code = design_.pieces[editView_.selPiece].code;
        ensureEditRecord(code);
        auto& rec = edits_[code];
        std::vector<int> going = editView_.selAnchors;
        if (going.empty()) going.push_back(editView_.selAnchor);
        // Highest index first: erasing shifts everything after it down, so
        // ascending order would delete the wrong points.
        std::sort(going.begin(), going.end(), std::greater<int>());
        for (int idx : going) {
            if (idx >= 0 && idx < (int)rec.points.size()) {
                // Deleting a generated point is itself an edit to remember;
                // a point the designer added just goes away.
                if (!rec.points[idx].added) rec.removedT.push_back(rec.points[idx].t);
                rec.points.erase(rec.points.begin() + idx);
            }
            paths_[code].removePoint(idx);
        }
        constrainEdit(code);
        editView_.selAnchor = -1;
        editView_.selAnchors.clear();
        rebuild();
    }
}

void ofApp::mouseDragged(int x, int y, int button) {
    if (ImGui::GetIO().WantCaptureMouse) { lastMouseX_ = x; lastMouseY_ = y; return; }
    if (draggingPoint_ && editView_.selPiece >= 0 && editView_.selAnchor >= 0) {
        ofRectangle vp = canvasViewport();
        auto offsets = canvas_.layoutOffsets(design_);
        glm::vec2 off = offsets[editView_.selPiece];
        Pt now = canvas_.screenToPiece(glm::vec2((float)x, (float)y), vp, off);
        Pt before = canvas_.screenToPiece(glm::vec2((float)lastMouseX_, (float)lastMouseY_), vp, off);
        auto& code = design_.pieces[editView_.selPiece].code;
        ensureEditRecord(code);
        auto& path = paths_[code];
        if (editView_.selHandle == 0) {
            // Every selected point moves by the SAME delta, so the shape
            // between them is carried along rather than redrawn -- that is
            // the whole point of selecting a run of points at once.
            Pt delta = now - before;
            std::vector<int> moving = editView_.selAnchors;
            if (moving.empty()) moving.push_back(editView_.selAnchor);
            for (int a : moving)
                if (a >= 0 && a < (int)path.anchors.size()) path.moveAnchor(a, delta);
            constrainEdit(code);
            for (int a : moving) {
                syncEditFromLive(code, a);
                mirrorSeamEdit(code, a);
            }
        } else {
            path.moveHandle(editView_.selAnchor, editView_.selHandle == 1, now);
            constrainEdit(code);
            if (editView_.selAnchor < (int)edits_[code].points.size())
                edits_[code].points[editView_.selAnchor].handlesSet = true;
            syncEditFromLive(code, editView_.selAnchor);
            mirrorSeamEdit(code, editView_.selAnchor);
        }
        lastMouseX_ = x; lastMouseY_ = y;
        rebuild();
        return;
    }
    if (editView_.marqueeActive) {
        editView_.marqueeTo = glm::vec2((float)x, (float)y);
        lastMouseX_ = x; lastMouseY_ = y;
        return;
    }
    if (show3D_) return; // ofEasyCam handles its own drag-to-orbit while active
    canvas_.drag(glm::vec2((float)(x - lastMouseX_), (float)(y - lastMouseY_)));
    lastMouseX_ = x; lastMouseY_ = y;
}

void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (show3D_) return; // ofEasyCam handles its own scroll-to-zoom while active
    canvas_.scroll(scrollY, glm::vec2((float)x, (float)y), ofRectangle(300, 0, ofGetWidth() - 600.f, (float)ofGetHeight()));
}
