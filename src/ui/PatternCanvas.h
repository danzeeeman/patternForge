#pragma once
#include "ofMain.h"
#include "../garments/GarmentModule.h"
#include "../geometry/EditablePath.h"
#include <map>
#include <string>

// Live 2D preview of every piece in a DesignResult: laid out left to
// right in a simple cutting-table flow, cutting line solid, sewing line
// (if any) lightly dashed, grain arrow and registration marks drawn per
// piece -- the same information FullSizeExport puts on paper, just
// interactive (scroll to zoom, drag to pan) instead of printed.
//
// It also hosts outline editing: with an edit view supplied it draws each
// piece's Bezier anchors and handles and can hit-test them, so the base
// outlines can be reshaped by hand.

namespace pf { namespace ui {

// What the canvas needs to draw and hit-test the editable outlines.
struct EditView {
    bool active = false;
    const std::map<std::string, EditablePath>* paths = nullptr;
    int selPiece = -1;   // index into design.pieces
    int selAnchor = -1;  // the primary point: the one whose handles show
    int selHandle = 0;   // 0 = the anchor itself, 1 = incoming, 2 = outgoing

    // Every selected point on selPiece, primary included. Dragging any one
    // of them moves the whole set together, which is how you move a run of
    // a curve without flattening the shape you already made.
    //
    // A selection stays within ONE piece: points on different pieces have
    // different local coordinates, so "move them all by the same amount"
    // would not mean the same thing for each.
    std::vector<int> selAnchors;

    bool isSelected(int piece, int anchor) const {
        if (piece != selPiece) return false;
        for (int a : selAnchors) if (a == anchor) return true;
        return anchor == selAnchor;
    }

    // Rubber-band box being dragged out, in screen pixels.
    bool marqueeActive = false;
    glm::vec2 marqueeFrom{ 0.f, 0.f }, marqueeTo{ 0.f, 0.f };
};

// What was under the cursor. piece < 0 means nothing.
struct EditHit {
    int piece = -1;
    int anchor = -1;
    int handle = 0;      // 0 anchor, 1 incoming handle, 2 outgoing handle
};

class PatternCanvas {
public:
    void scroll(float wheelDy, glm::vec2 mouseScreenPos, const ofRectangle& viewport);
    void drag(glm::vec2 deltaScreenPx);
    void resetView();
    void draw(const DesignResult& design, const ofRectangle& viewport, const EditView& edit = {});

    // Where each piece sits on the canvas, in cm, in design.pieces order.
    std::vector<glm::vec2> layoutOffsets(const DesignResult& design) const;

    // Screen <-> piece-local cm, for editing.
    Pt screenToPiece(glm::vec2 screen, const ofRectangle& viewport, glm::vec2 pieceOffset) const;
    glm::vec2 pieceToScreen(Pt p, const ofRectangle& viewport, glm::vec2 pieceOffset) const;
    float pixelsPerCm() const { return pxPerCm_; }

    // Nearest anchor or handle within `grabPx`, else piece = -1.
    EditHit hitTest(const DesignResult& design, const ofRectangle& viewport,
                    const EditView& edit, glm::vec2 screen, float grabPx = 9.f) const;
    // Every anchor inside the screen-space rectangle. A rubber band can
    // only ever select within one piece (see EditView::selAnchors), so this
    // returns the anchors of whichever piece has the most inside the box.
    std::vector<int> anchorsInRect(const DesignResult& design, const ofRectangle& viewport,
                                   const EditView& edit, glm::vec2 cornerA, glm::vec2 cornerB,
                                   int& pieceOut) const;

    // Nearest point on any piece's path, for inserting an anchor.
    EditHit hitPath(const DesignResult& design, const ofRectangle& viewport,
                    const EditView& edit, glm::vec2 screen, float grabPx,
                    int& segOut, float& tOut) const;

    bool showCutting = true;
    bool showSewing = true;
    bool showGrain = true;
    bool showMarks = true;
    bool showMirroredHalf = true; // draw the other half of a cut-on-fold piece

private:
    glm::vec2 panPx_{ 40.f, 40.f };
    float pxPerCm_ = 3.5f;
};

} } // namespace pf::ui
