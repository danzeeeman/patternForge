#include "PatternCanvas.h"

namespace pf { namespace ui {

void PatternCanvas::scroll(float wheelDy, glm::vec2 mouseScreenPos, const ofRectangle& viewport) {
    float oldZoom = pxPerCm_;
    pxPerCm_ = ofClamp(pxPerCm_ * (1.f + wheelDy * 0.1f), 0.5f, 30.f);
    // Zoom around the mouse position so the point under the cursor stays put.
    glm::vec2 anchor = mouseScreenPos - glm::vec2(viewport.x, viewport.y) - panPx_;
    panPx_ -= anchor * (pxPerCm_ / oldZoom - 1.f);
}

void PatternCanvas::drag(glm::vec2 deltaScreenPx) { panPx_ += deltaScreenPx; }

void PatternCanvas::resetView() { panPx_ = { 40.f, 40.f }; pxPerCm_ = 3.5f; }

static void drawRing(const Ring& r, bool dashed) {
    if (r.size() < 2) return;
    if (!dashed) {
        ofPolyline line;
        for (auto& p : r) line.addVertex(p.x, p.y);
        line.close();
        line.draw();
        return;
    }
    // Simple screen-space dashing: walk the closed perimeter, alternating
    // visible/gap segments of fixed cm length.
    const float dashLen = 0.4f, gapLen = 0.3f;
    bool on = true;
    float remaining = dashLen;
    for (size_t i = 0; i <= r.size(); ++i) {
        Pt a = r[i % r.size()];
        Pt b = r[(i + 1) % r.size()];
        float segLen = glm::distance(a, b);
        float t = 0.f;
        while (t < segLen) {
            float step = std::min(remaining, segLen - t);
            Pt p0 = a + (b - a) * (t / segLen);
            Pt p1 = a + (b - a) * ((t + step) / segLen);
            if (on) ofDrawLine(p0.x, p0.y, p1.x, p1.y);
            t += step;
            remaining -= step;
            if (remaining <= 0.0001f) { on = !on; remaining = on ? dashLen : gapLen; }
        }
        if (i == r.size()) break;
    }
}

std::vector<glm::vec2> PatternCanvas::layoutOffsets(const DesignResult& design) const {
    std::vector<glm::vec2> offsets;
    offsets.reserve(design.pieces.size());
    double trackX = 0.0;
    const double gap = 3.0;
    for (auto& piece : design.pieces) {
        Pt lo, hi;
        geo::bounds(piece.cutting, lo, hi);
        offsets.push_back(glm::vec2((float)(trackX - lo.x), (float)-lo.y));
        trackX += (hi.x - lo.x) + gap;
    }
    return offsets;
}

Pt PatternCanvas::screenToPiece(glm::vec2 screen, const ofRectangle& viewport, glm::vec2 pieceOffset) const {
    glm::vec2 world = (screen - glm::vec2(viewport.x, viewport.y) - panPx_) / pxPerCm_;
    return Pt(world.x - pieceOffset.x, world.y - pieceOffset.y);
}

glm::vec2 PatternCanvas::pieceToScreen(Pt p, const ofRectangle& viewport, glm::vec2 pieceOffset) const {
    return glm::vec2(viewport.x, viewport.y) + panPx_ + (glm::vec2(p.x, p.y) + pieceOffset) * pxPerCm_;
}

EditHit PatternCanvas::hitTest(const DesignResult& design, const ofRectangle& viewport,
                               const EditView& edit, glm::vec2 screen, float grabPx) const {
    EditHit best;
    if (!edit.paths) return best;
    float bestDist = grabPx;
    auto offsets = layoutOffsets(design);
    for (size_t i = 0; i < design.pieces.size(); ++i) {
        // A derived piece (a facing) follows the panel it is cut from, so
        // it is not grabbable: any edit here would be re-cut away on the
        // next rebuild. Edit the panel and the facing follows.
        if (design.pieces[i].isDerived()) continue;
        auto it = edit.paths->find(design.pieces[i].code);
        if (it == edit.paths->end()) continue;
        const auto& anchors = it->second.anchors;
        for (size_t a = 0; a < anchors.size(); ++a) {
            // Handles are only grabbable on the selected anchor, so they
            // never steal a click meant for a neighbouring point.
            bool selected = ((int)i == edit.selPiece && (int)a == edit.selAnchor);
            std::vector<std::pair<Pt,int>> targets = { { anchors[a].pos, 0 } };
            if (selected) {
                targets.push_back({ anchors[a].inH, 1 });
                targets.push_back({ anchors[a].outH, 2 });
            }
            for (auto& t : targets) {
                float d = glm::distance(pieceToScreen(t.first, viewport, offsets[i]), screen);
                if (d < bestDist) { bestDist = d; best.piece = (int)i; best.anchor = (int)a; best.handle = t.second; }
            }
        }
    }
    return best;
}

EditHit PatternCanvas::hitPath(const DesignResult& design, const ofRectangle& viewport,
                               const EditView& edit, glm::vec2 screen, float grabPx,
                               int& segOut, float& tOut) const {
    EditHit best; segOut = -1; tOut = 0.f;
    if (!edit.paths) return best;
    float bestDist = grabPx;
    auto offsets = layoutOffsets(design);
    for (size_t i = 0; i < design.pieces.size(); ++i) {
        // A derived piece (a facing) follows the panel it is cut from, so
        // it is not grabbable: any edit here would be re-cut away on the
        // next rebuild. Edit the panel and the facing follows.
        if (design.pieces[i].isDerived()) continue;
        auto it = edit.paths->find(design.pieces[i].code);
        if (it == edit.paths->end()) continue;
        Pt local = screenToPiece(screen, viewport, offsets[i]);
        int seg; float t, dist;
        it->second.closestSegment(local, seg, t, dist);
        float distPx = dist * pxPerCm_;
        if (seg >= 0 && distPx < bestDist) {
            bestDist = distPx; best.piece = (int)i; best.anchor = -1; segOut = seg; tOut = t;
        }
    }
    return best;
}

void PatternCanvas::draw(const DesignResult& design, const ofRectangle& viewport, const EditView& edit) {
    auto offsets = layoutOffsets(design);

    for (size_t i = 0; i < design.pieces.size(); ++i) {
        const Piece& piece = design.pieces[i];
        glm::vec2 off = offsets[i];
        Pt lo, hi;
        geo::bounds(piece.cutting, lo, hi);

        ofPushMatrix();
        ofTranslate(viewport.x + panPx_.x, viewport.y + panPx_.y);
        ofScale(pxPerCm_, pxPerCm_);
        ofTranslate(off.x, off.y);
        ofSetLineWidth(1.5f / pxPerCm_ * 3.5f);

        if (showCutting) { ofSetColor(30, 30, 30); drawRing(piece.cutting, false); }
        if (showSewing && piece.hasSeamAllowance()) { ofSetColor(120, 120, 220); drawRing(piece.sewing, true); }

        // A piece cut on the fold is half a panel. Show the other half --
        // its mirror across the fold -- so the whole panel is visible while
        // only half of it is being edited.
        if (showMirroredHalf && piece.foldAtCF) {
            Ring mirror = piece.cutting;
            for (auto& p : mirror) p.x = -p.x;
            ofSetColor(30, 30, 30, 70);
            drawRing(mirror, false);
        }

        if (showGrain && !edit.active) {
            Pt c = geo::centroid(piece.sewing);
            double span = std::min(5.0, std::max(1.0, (double)(hi.y - lo.y) / 4.0));
            ofSetColor(200, 60, 60);
            ofDrawLine(c.x, c.y - span, c.x, c.y + span);
            ofDrawLine(c.x - 0.15, c.y - span + 0.4, c.x, c.y - span);
            ofDrawLine(c.x + 0.15, c.y - span + 0.4, c.x, c.y - span);
        }
        // Fold lines: dashed, so they never read as an edge to cut.
        if (showMarks) {
            for (auto& f : piece.lines) {
                switch (f.kind) {
                    case MarkedLine::CutOnFold: ofSetColor(200, 60, 60); break;
                    case MarkedLine::Cut:       ofSetColor(30, 30, 30); break;
                    case MarkedLine::SeamOpen:  ofSetColor(30, 140, 180); break;
                    default:                    ofSetColor(120, 90, 180); break;
                }
                // A cut line is solid: dashing it would read as a fold.
                if (f.kind == MarkedLine::Cut) { ofDrawLine(f.a.x, f.a.y, f.b.x, f.b.y); continue; }
                glm::vec2 a(f.a.x, f.a.y), b(f.b.x, f.b.y);
                float len = glm::distance(a, b);
                int dashes = std::max(2, (int)(len / 1.2f));
                for (int d = 0; d < dashes; ++d) {
                    float t0 = (float)d / dashes, t1 = t0 + 0.6f / dashes;
                    glm::vec2 p0 = glm::mix(a, b, t0), p1 = glm::mix(a, b, std::min(t1, 1.f));
                    ofDrawLine(p0.x, p0.y, p1.x, p1.y);
                }
            }
        }
        if (showMarks) {
            for (auto& c : piece.closures) {
                ofSetColor(200, 120, 20);
                ofNoFill();
                if (c.kind == Closure::Zipper) {
                    ofDrawLine(c.a.x, c.a.y, c.b.x, c.b.y);
                    ofDrawLine(c.a.x - 0.4f, c.a.y, c.a.x + 0.4f, c.a.y);
                    ofDrawLine(c.b.x - 0.4f, c.b.y, c.b.x + 0.4f, c.b.y);
                } else {
                    float r = (float)std::max(0.25, c.sizeMm / 20.0);
                    ofDrawCircle(c.a.x, c.a.y, r);
                    if (c.kind == Closure::Buttonhole)
                        ofDrawLine(c.a.x - r, c.a.y, c.a.x + r, c.a.y);
                }
                ofFill();
            }
            ofSetColor(20, 140, 90);
            ofNoFill();
            for (auto& m : piece.marks) {
                if (m.kind == Mark::Landmark) continue; // structural, not a marking
                ofDrawCircle(m.pos.x, m.pos.y, 0.11f);
            }
            ofFill();
        }
        ofPopMatrix();

        // Labels and edit points are drawn in screen space so they keep a
        // usable size at any zoom.
        glm::vec2 label = pieceToScreen(Pt(lo.x, lo.y - 0.9f), viewport, off);
        ofSetColor(piece.handEdited ? ofColor(150, 90, 30) : ofColor(60, 60, 60));
        ofDrawBitmapString(piece.handEdited ? piece.code + " *" : piece.code, label.x, label.y);

        if (!edit.active || !edit.paths) continue;
        auto it = edit.paths->find(piece.code);
        if (it == edit.paths->end()) continue;
        const auto& anchors = it->second.anchors;
        for (size_t a = 0; a < anchors.size(); ++a) {
            bool selected = edit.isSelected((int)i, (int)a);
            // Handles belong to the primary point only: showing them for
            // every point of a big selection would bury the outline.
            bool primary = ((int)i == edit.selPiece && (int)a == edit.selAnchor);
            glm::vec2 p = pieceToScreen(anchors[a].pos, viewport, off);
            if (primary) {
                glm::vec2 hIn = pieceToScreen(anchors[a].inH, viewport, off);
                glm::vec2 hOut = pieceToScreen(anchors[a].outH, viewport, off);
                ofSetColor(150, 150, 170);
                ofSetLineWidth(1.f);
                ofDrawLine(p, hIn);
                ofDrawLine(p, hOut);
                ofSetColor(90, 120, 200);
                ofDrawCircle(hIn, 3.5f);
                ofDrawCircle(hOut, 3.5f);
            }
            ofSetColor(selected ? ofColor(220, 90, 40) : ofColor(40, 90, 160));
            ofDrawCircle(p, selected ? 5.f : 3.5f);
            if (showMirroredHalf && piece.foldAtCF) {
                glm::vec2 m = pieceToScreen(Pt(-anchors[a].pos.x, anchors[a].pos.y), viewport, off);
                ofSetColor(40, 90, 160, 70);
                ofDrawCircle(m, 2.5f);
            }
        }
    }

    if (edit.active && edit.marqueeActive) {
        ofPushStyle();
        ofSetColor(220, 90, 40, 40);
        ofFill();
        ofDrawRectangle(ofRectangle(edit.marqueeFrom, edit.marqueeTo));
        ofSetColor(220, 90, 40, 180);
        ofNoFill();
        ofSetLineWidth(1.f);
        ofDrawRectangle(ofRectangle(edit.marqueeFrom, edit.marqueeTo));
        ofPopStyle();
    }
}

std::vector<int> PatternCanvas::anchorsInRect(const DesignResult& design, const ofRectangle& viewport,
                                              const EditView& edit, glm::vec2 cornerA, glm::vec2 cornerB,
                                              int& pieceOut) const {
    pieceOut = -1;
    std::vector<int> best;
    if (!edit.paths) return best;
    ofRectangle box(cornerA, cornerB);
    auto offsets = layoutOffsets(design);
    for (size_t i = 0; i < design.pieces.size(); ++i) {
        // A derived piece follows its panel, so it is not editable and not
        // selectable either.
        if (design.pieces[i].isDerived()) continue;
        auto it = edit.paths->find(design.pieces[i].code);
        if (it == edit.paths->end()) continue;
        std::vector<int> here;
        for (size_t a = 0; a < it->second.anchors.size(); ++a)
            if (box.inside(pieceToScreen(it->second.anchors[a].pos, viewport, offsets[i])))
                here.push_back((int)a);
        if (here.size() > best.size()) { best = here; pieceOut = (int)i; }
    }
    return best;
}

} } // namespace pf::ui
