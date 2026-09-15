#include "ClothSim.h"
#include "../garments/GarmentCommon.h"
#include "../garments/BlockMath.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <functional>
#include <chrono>
#include <thread>
#include <cstdlib>

namespace pf { namespace sim {

namespace {

// Placement diagnostics. Useful when a garment starts torn and useless
// otherwise, so they only speak up under the headless sim test.
bool tracing() {
    static bool on = std::getenv("PATTERNFORGE_SIM_TEST") != nullptr;
    return on;
}

// Where a flat point of a panel starts in 3D.
//
// NOT a rigid transform of the flat panel. A panel stood flat in front of
// the body has to travel a long way to become a garment, and its seam
// vertices get dragged the whole width of the body when they are welded --
// which leaves the mesh wildly distorted before the solver has run a step,
// and XPBD cannot recover from that.
//
// Instead each panel is WRAPPED around the body on a cylinder: the flat x
// becomes an arc at the body's radius, so distances along the panel are
// nearly preserved and it starts close to the shape it will settle into.
// The solver then only has to do the last bit of the work, which is what
// it is good at.
// The tube a group of panels wraps is not a cylinder -- it is as wide at
// each level as the panels are.
//
// A constant radius is the one assumption in this whole file that a
// garment flatly contradicts. A torso tube sized to the bust is 42 cm
// deep; a shoulder is about 10. Wrapping the front and back panels on
// that cylinder put their shoulder tips 40 cm apart, one on each side of
// a barrel, and the shoulder seam then had to drag the top of the garment
// across the body before anything could drape -- which is exactly the
// mess that showed up around the armholes.
//
// Taking the radius from the panels' own width at each height instead
// makes every level close on itself: front and back meet at the shoulder
// because at the shoulder they are only a shoulder's width apart, and a
// flared skirt starts as the cone it actually is.
// And it is not round either. Across the shoulders a torso is nearly
// three times as wide as it is deep, so a circle of the correct
// circumference still lands the shoulder tip at x = 9 when the body's --
// and the sleeve's -- is at x = 18. `half` is the half-width across the
// body, `depth` the half-depth front to back; a limb has them equal.
struct Profile {
    float step = 1.f;                 // cm between samples, from y = 0
    std::vector<float> half, depth;
    static float lerp(const std::vector<float>& v, double y, float dflt) {
        if (v.empty()) return dflt;
        if (y <= 0.0) return v.front();
        size_t i = (size_t)y;
        if (i + 1 >= v.size()) return v.back();
        float t = (float)(y - (double)i);
        return v[i] * (1.f - t) + v[i + 1] * t;
    }
    void at(double y, float& a, float& b) const {
        a = lerp(half, y / step, 12.f);
        b = lerp(depth, y / step, a);
    }
};

// Arc length along an ellipse, and its inverse.
//
// On a circle the panel's x is just an angle, x / r. On an ellipse the
// same arc covers different angles depending on where you are -- more
// near the flat sides, less at the ends -- and that difference is the
// whole point: it is what puts a shoulder seam over a shoulder instead of
// a third of the way down the chest. `a` is the semi-axis along sin,
// `b` along cos, matching how place() lays out its basis.
double angleAfterArc(double a, double b, double phi0, double s) {
    if (a < 1e-3 && b < 1e-3) return phi0;
    double dir = s < 0 ? -1.0 : 1.0;
    s = std::fabs(s);
    const int kSteps = 512;
    const double h = 2.0 * 3.14159265358979 / kSteps;
    double acc = 0.0, phi = phi0;
    // |d/dt (a sin t, b cos t)| = |(a cos t, -b sin t)|. Writing it the
    // other way round -- a with sin, b with cos -- reads plausibly and is
    // wrong everywhere the two axes differ: it multiplied every edge across
    // the centre front by the torso's width-to-depth ratio.
    auto speed = [&](double t) {
        double st = std::sin(t), ct = std::cos(t);
        return std::sqrt(a * a * ct * ct + b * b * st * st);
    };
    for (int i = 0; i < kSteps; ++i) {
        double t0 = phi0 + dir * i * h, t1 = t0 + dir * h;
        double seg = (speed(t0) + speed(t1)) * 0.5 * h;
        if (seg < 1e-9) continue;
        if (acc + seg >= s) return t0 + dir * h * (s - acc) / seg;
        acc += seg;
        phi = t1;
    }
    return phi;
}

struct Wrap {
    float radius = 12.f;     // fallback when there is no profile
    const Profile* prof = nullptr;
    // Smallest the tube may be, whatever the panels say. A sleeve drafted
    // close to the arm can profile out narrower than the arm itself, and
    // cloth that starts inside a limb is ejected 15 cm on the first step.
    float armFloor = 0.f;
    // Where the panel's own x = 0 sits along its arc, per level.
    //
    // A torso or sleeve panel is centred on the line its tube is built on,
    // so its x IS the arc and this is unused. A TROUSER panel's x = 0 is
    // the rise seam -- the edge that joins the other leg at the body's
    // midline -- which is nowhere near the front of the leg, and is a
    // different distance from the outseam at every height. Measuring its
    // arc from the outseam instead is what makes the outseam meet.
    const std::vector<float>* shift = nullptr;
    float shiftAt(double y) const {
        return shift ? Profile::lerp(*shift, y, 0.f) : 0.f;
    }
    // How far OUTSIDE that cylinder the cloth starts.
    //
    // The radius comes from the garment's own circumference, which puts
    // the cloth exactly on the body -- so anywhere the body is locally
    // wider than average (bust, shoulders, seat) it begins INSIDE, and
    // collision ejects it violently on the first step. Starting it stood
    // off and letting it fall inward is both stabler and closer to how a
    // garment is actually put on.
    //
    // The angle is still computed from `radius`, so the two sides of every
    // seam stay lined up: inflating moves them outward together.
    float inflate = 0.f;
    float theta0 = 0.f;      // angle the panel's center line sits at
    // Which way round the tube the panel's +x runs. A FRONT panel goes
    // from center front outward one way; the BACK it meets goes from
    // center back outward the OTHER way, because the two travel toward
    // each other to meet at the side seam. Getting this backwards puts a
    // back panel's side seam on the far side of the body from the front's,
    // and the seam between them then spans the whole garment.
    float dir = 1.f;

    // The tube's axis, as a point and a direction.
    //
    // A torso tube stands straight up and a leg hangs straight down, so for
    // years this only needed a y offset and an x. A SLEEVE does not: it
    // lies along an arm, which on these avatars is angled out and down, and
    // by a different amount on each of them. Carrying the axis explicitly
    // lets a sleeve be placed on the arm that is actually there instead of
    // on a guessed angle -- and the torso and leg cases are the same
    // formula with a straight-down axis, so nothing else has to change.
    //
    // `e0` is where theta = 0 points and `e1` where theta = pi/2 does, both
    // perpendicular to the axis. For a torso that is front and side; for a
    // sleeve, front-of-arm and top-of-arm.
    glm::vec3 origin{ 0, 0, 0 };
    glm::vec3 axis{ 0, -1, 0 };
    glm::vec3 e0{ 0, 0, 1 }, e1{ 1, 0, 0 };
};

// Fills in e1 from e0 and the axis, keeping the handedness the torso case
// has always had (theta = pi/2 lands on +x for a straight-down axis).
void orient(Wrap& w, const glm::vec3& axis, const glm::vec3& front) {
    w.axis = glm::normalize(axis);
    glm::vec3 e0 = front - w.axis * glm::dot(front, w.axis);
    if (glm::length(e0) < 1e-3f) {
        glm::vec3 alt(0, 0, 1);
        e0 = alt - w.axis * glm::dot(alt, w.axis);
    }
    w.e0 = glm::normalize(e0);
    w.e1 = glm::normalize(glm::cross(w.e0, w.axis));
}

glm::vec3 place(const Wrap& w, const Pt& p, bool mirrored) {
    float a = w.radius, b = w.radius;
    if (w.prof) w.prof->at(p.y, a, b);
    a = std::max(std::max(a, w.armFloor), 2.5f);
    b = std::max(std::max(b, w.armFloor), 2.5f);
    // Held off the body by `inflate`, and measured on the tube it is
    // actually held off on.
    //
    // Measuring the arc on the tight tube and then placing the point on
    // the bigger one stretched every panel by the ratio of the two -- the
    // cloth was born 40% too big and the solver spent the drape hauling it
    // back. Using one tube for both keeps the panel exactly its drafted
    // size; what opens up instead is the seams, which is right. They are
    // not sewn yet.
    // Held off by at most the tube's own size. A standoff tuned for a
    // torso is most of a metre of extra circumference on a sleeve, which
    // would start it as a hoop in the air rather than a tube round the
    // arm -- and a sleeve that does not start around the arm cannot sew
    // itself onto one.
    float grow = std::min(w.inflate, std::min(a, b));
    double arc = w.dir * (p.x + w.shiftAt(p.y));
    double theta = angleAfterArc(a + grow, b + grow, w.theta0, arc);
    glm::vec3 v = w.origin
                + w.axis * (float)p.y
                + w.e0 * (float)((b + grow) * std::cos(theta))
                + w.e1 * (float)((a + grow) * std::sin(theta));
    // The left-hand copy of a panel is the right-hand one reflected. Doing
    // it here rather than by negating the panel's own x means the wrap only
    // ever has to describe ONE side of the body -- which matters once the
    // axis is an arbitrary direction, because reflecting a tilted axis by
    // hand is easy to get subtly wrong (and was).
    if (mirrored) v.x = -v.x;
    return v;
}

// Is this piece a trouser leg?
//
// It was decided by the piece's CODE: a leading "P", which is what the
// jumpsuit calls its trouser panels. The standalone Pants garment calls
// them plain "F" and "B" -- so both its legs were wrapped as a torso,
// on one tube down the body's midline starting at the nape, and the
// outseam began 16 cm open with nothing about the drape meaning anything.
// A garment whose every shell piece is a leg is the other half of the
// question, and only the garment knows that.
bool isLeg(const Piece& p, const DesignResult& design) {
    if (design.garmentKey == "Pants") return p.code == "F" || p.code == "B";
    return p.code.size() > 1 && p.code[0] == 'P' && p.code != "PL";
}

// The cylinder a group of panels wraps around is decided by the PANELS,
// not by the body: the panels that go round a tube are exactly as wide as
// that tube's circumference, because that is what sewing them into a tube
// means. Using a body radius instead lets wide panels overlap themselves,
// and then welding a seam drags vertices clean across the garment.
Wrap wrapFor(const Piece& piece, const DesignResult& design,
             const std::vector<Capsule>& body, const Avatar* avatar,
             double tubeCircumferenceCm, const Profile* prof, double inflateCm) {
    const std::string& c = piece.code;
    bool trouser = isLeg(piece, design);
    std::string base = (c.size() > 1 && c[0] == 'P') ? c.substr(1) : c;
    double waistY = design.size.get("backWaistLength", 40.0);
    double hip = design.size.get("hip", 94.0);

    double torsoR = 12.0;
    for (auto& cap : body) torsoR = std::max(torsoR, (double)cap.r);
    double tubeR = tubeCircumferenceCm > 4.0
                 ? tubeCircumferenceCm / (2.0 * 3.14159265)
                 : torsoR + 1.5;

    Wrap w;
    w.inflate = (float)inflateCm;
    w.prof = prof;
    const float kPi = 3.14159265f;
    if (base.rfind("SL", 0) == 0) {
        // A tube around the arm -- placed on the arm the avatar actually
        // has, because the pose differs between bodies and a sleeve that
        // misses the arm ends up wadded against the chest.
        // With no avatar the arm is a capsule, and the sleeve should follow
        // THAT rather than a hard-coded angle -- otherwise the capsule
        // preview and the avatar preview disagree about where the sleeve is.
        glm::vec3 shoulder((float)(design.shoulderX), (float)-block::kShoulderDropCm, 0.f);
        glm::vec3 dir = glm::normalize(glm::vec3(0.32f, -0.95f, 0.f));
        float armR = 6.f;
        const Capsule* arm = nullptr;
        for (auto& cap : body) {
            // Arms and legs both stand off centre; the arms are the pair
            // that start near the top of the body.
            if (cap.a.x <= 1.f) continue;
            if (!arm || cap.a.y > arm->a.y) arm = &cap;
        }
        if (arm && glm::length(arm->b - arm->a) > 5.f) {
            shoulder = arm->a;
            dir = glm::normalize(arm->b - arm->a);
            armR = arm->r;
        }
        if (avatar) {
            glm::vec3 s2, d2;
            float r2 = 6.f;
            if (avatar->armAxis(s2, d2, r2)) {
                shoulder = s2; dir = d2; armR = r2;
                if (tracing())
                ofLogNotice() << "SimTest:   arm axis: shoulder (" << s2.x << ", " << s2.y
                              << ", " << s2.z << ") dir (" << d2.x << ", " << d2.y << ", "
                              << d2.z << ") radius " << r2 << " cm";
            }
        }
        w.radius = (float)tubeR;
        // A sleeve starts ON the arm: laid over it, open underneath, and
        // closed round it by its own underarm seam while the body panels
        // travel across to meet the cap.
        //
        // So it is held just clear of the limb and no further. The torso's
        // standoff is the wrong number here twice over -- it is most of a
        // sleeve's circumference, and it would push the sleeve out into a
        // hoop in the air that happens to be near an arm.
        w.armFloor = armR + 1.f;
        w.inflate = std::min(w.inflate, 1.5f);
        // x = 0 on a sleeve is the cap crown, which sits on TOP of the
        // shoulder; the two ends of the panel meet under the arm.
        w.theta0 = kPi * 0.5f;
        // Front of the arm is whatever is left of +z once the arm's own
        // direction is taken out.
        orient(w, dir, glm::vec3(0, 0, 1));
        // p.y = 0 is the crown, which belongs at the shoulder -- one radius
        // off the arm's centre line, which is where theta0 puts it.
        w.origin = shoulder;
        return w;
    }
    if (trouser) {
        // Each leg is its own tube, centred on the leg rather than on a
        // guess: the avatar's legs are where they are, and a tube 3 cm
        // inboard of one starts the cloth inside the thigh.
        w.radius = (float)tubeR;
        double legX = std::max(tubeR, hip / 12.0);
        if (avatar) {
            float hw = 0.f, hd = 0.f;
            // A hand's width below the crotch, where the two legs have
            // separated but not yet tapered.
            if (avatar->sectionAt(waistY + design.size.get("rise", 27.0) + 10.0, hw, hd))
                legX = hw * 0.5;   // half the outer span is one leg's centre
        }
        w.origin = glm::vec3((float)legX, (float)-waistY, 0.f);
        // Both panels are measured from the OUTSEAM, which is put on the
        // outside of the leg. Anchoring them at x = 0 instead -- the rise
        // seam -- puts the front's outseam and the back's outseam at
        // unrelated angles, because the rise is a different distance from
        // the outseam at the waist than it is at the crotch. From the
        // outseam the front travels round the front of the leg and the
        // back round the back, and they meet at the inseam exactly,
        // because together they are the leg's circumference.
        bool isBack = (base.rfind("B", 0) == 0);
        w.theta0 = kPi * 0.5f;
        w.dir = isBack ? 1.f : -1.f;
        return w;
    }
    w.radius = (float)tubeR;
    w.origin = glm::vec3(0, 0, 0);
    // Backs wrap around the back of the body, fronts around the front,
    // and they travel toward each other to meet at the side seams.
    bool isBack = (base.rfind("B", 0) == 0);
    w.theta0 = isBack ? kPi : 0.f;
    w.dir = isBack ? -1.f : 1.f;
    return w;
}

const Piece* pieceByCode(const DesignResult& d, const std::string& code) {
    for (auto& p : d.pieces) if (p.code == code) return &p;
    return nullptr;
}

} // namespace

const char* fabricName(Fabric f) {
    switch (f) {
        case Fabric::Cotton:  return "Cotton poplin";
        case Fabric::Wool:    return "Wool suiting";
        case Fabric::Silk:    return "Silk crepe";
        case Fabric::Denim:   return "Denim";
        case Fabric::Jersey:  return "Cotton jersey";
        case Fabric::Leather: return "Leather";
    }
    return "Cloth";
}

void applyFabric(SimSettings& s, int fabric) {
    // bendCompliance is the one that shows: high means the cloth folds
    // readily and hangs in many small folds, low means it holds a plane.
    // stretchCompliance separates a woven (barely stretches) from a knit.
    switch ((Fabric)fabric) {
        case Fabric::Wool:    s.bendCompliance = 2.2e-4; s.stretchCompliance = 8e-8;  s.damping = 0.984; break;
        case Fabric::Silk:    s.bendCompliance = 4.0e-3; s.stretchCompliance = 6e-8;  s.damping = 0.990; break;
        case Fabric::Denim:   s.bendCompliance = 2.5e-5; s.stretchCompliance = 3e-8;  s.damping = 0.978; break;
        case Fabric::Jersey:  s.bendCompliance = 1.6e-3; s.stretchCompliance = 9e-6;  s.damping = 0.988; break;
        case Fabric::Leather: s.bendCompliance = 1.2e-5; s.stretchCompliance = 2e-8;  s.damping = 0.972; break;
        case Fabric::Cotton:
        default:              s.bendCompliance = 5.0e-4; s.stretchCompliance = 1e-7;  s.damping = 0.985; break;
    }
}

std::vector<Capsule> bodyFromSize(const SizePreset& size) {
    std::vector<Capsule> body;
    double bust = size.get("bust", 86.4), waist = size.get("waist", 68.6);
    double hip = size.get("hip", 94.0);
    double bwl = size.get("backWaistLength", 40.0);
    double hipDepth = size.get("hipDepth", 20.0);
    double shoulder = size.get("shoulder", 12.5);
    double inseam = size.get("inseam", 74.0);
    double rise = size.get("rise", 27.0);

    // Radii from circumferences, flattened front-to-back the way a torso
    // actually is -- a round body would hold a garment off the chest.
    auto r = [](double circ) { return (float)(circ / (2.0 * 3.14159265) * 0.86); };

    body.push_back({ glm::vec3(0, -6, 0),            glm::vec3(0, (float)-bwl, 0), r(bust) });
    body.push_back({ glm::vec3(0, (float)-bwl, 0),   glm::vec3(0, (float)-(bwl + hipDepth), 0), r(std::max(waist, hip)) });
    // Arms, angled down and out from the shoulder point.
    for (int s : { -1, 1 }) {
        glm::vec3 top((float)(s * shoulder), -4.f, 0.f);
        glm::vec3 bot((float)(s * (shoulder + 18.0)), (float)-(bwl + 18.0), 0.f);
        body.push_back({ top, bot, 6.5f });
    }
    // Legs from the crotch down.
    double crotchY = bwl + rise;
    for (int s : { -1, 1 }) {
        glm::vec3 top((float)(s * hip / 12.0), (float)-crotchY, 0.f);
        glm::vec3 bot((float)(s * hip / 12.0), (float)-(crotchY + inseam), 0.f);
        body.push_back({ top, bot, 9.0f });
    }
    return body;
}

SimGarment build(const DesignResult& design, const SimSettings& s, const Avatar* avatar) {
    SimGarment g;
    g.body = bodyFromSize(design.size);
    g.avatar = (avatar && avatar->valid()) ? avatar : nullptr;

    // --- mesh every panel that is part of the garment shell -----------
    struct PanelMesh { std::string code; Mesh2D m; int base = 0; bool mirrored = false;
                       bool fromFold = false; };
    // Every vertex's position on the FLAT pattern. Rest lengths come from
    // here, never from the 3D arrangement: cloth keeps the dimensions it
    // was cut to, and the 3D placement is only a starting guess. Measuring
    // rest from the placed-and-welded mesh records the distortion of that
    // guess as the fabric's natural shape -- the panel is then "born"
    // stretched at its seams, and the solver has no way back.
    std::vector<Pt> flat2D;
    std::vector<int> triPanel;   // which panel each triangle was cut from
    std::vector<PanelMesh> panels;
    std::map<std::string, std::vector<int>> instancesOf;   // code -> indices into `panels`

    // Measure each tube first: how wide are the panels that go round it.
    auto groupOf = [&design](const Piece& p) {
        bool trouser = isLeg(p, design);
        const std::string& c = p.code;
        std::string base = (c.size() > 1 && c[0] == 'P') ? c.substr(1) : c;
        if (!trouser && base.rfind("SL", 0) == 0) return std::string("sleeve");
        return trouser ? std::string("leg") : std::string("torso");
    };
    // How much of its tube's circumference a panel covers at a level.
    //
    // A torso or sleeve panel is centred on the line its tube is built on
    // -- the centre front, the centre back, the sleeve crown -- so it
    // reaches the same distance each way and covers twice that. A TROUSER
    // panel is not: its centre line sits at the front of the leg with the
    // outseam off to -x and the inseam to +x, so what it covers is simply
    // its width. Doubling it, as the centred rule does, made every leg
    // tube twice the size of the leg.
    auto arcAtLevel = [&](const Piece& p, double y) {
        if (groupOf(p) == "leg")
            return std::max(0.0, geo::maxXAtY(p.sewing, y) - geo::minXAtY(p.sewing, y));
        return 2.0 * std::max(0.0, geo::maxXAtY(p.sewing, y));
    };
    std::map<std::string, double> tubeWidth;
    for (auto& piece : design.pieces) {
        if (piece.isDerived() || !piece.inMuslin) continue;
        Pt lo, hi;
        geo::bounds(piece.sewing, lo, hi);
        double w = hi.x - lo.x;
        // A piece stored as half a panel goes round the tube twice: a fold
        // piece is mirrored into a whole one, and a mirrored pair is two
        // halves of the SAME tube. Two trouser legs are two tubes, not two
        // halves of one, so they are not doubled.
        if (piece.foldAtCF || (piece.cutMirrored && piece.cutCount > 1 && groupOf(piece) != "leg"))
            w *= 2.0;
        // A sleeve is one panel closing on itself; a torso or a leg is a
        // front and a back, so their widths add up around the tube.
        tubeWidth[groupOf(piece)] += w;
    }

    // The same measurement, level by level, which is what actually gets
    // used -- the single number above only survives as a fallback for a
    // group whose panels turn out to have no width anywhere.
    //
    // Each panel's REACH at a height (how far its outline gets from the
    // line the tube is built on) is the arc it has to cover there. A fold
    // panel covers that arc on both sides of centre front; a mirrored pair
    // is two panels doing the same; a sleeve reaches both ways from the
    // crown. So in every case the group's circumference at that level is
    // twice the sum of its panels' reaches.
    std::map<std::string, Profile> profiles;
    {
        std::map<std::string, double> deepest;
        for (auto& piece : design.pieces) {
            if (piece.isDerived() || !piece.inMuslin) continue;
            Pt lo, hi;
            geo::bounds(piece.sewing, lo, hi);
            double& d = deepest[groupOf(piece)];
            d = std::max(d, (double)hi.y);
        }
        for (auto& kv : deepest) {
            Profile& pr = profiles[kv.first];
            int n = std::max(2, (int)std::ceil(kv.second) + 2);
            // Arc the panels have to cover at each level, which is the
            // perimeter of the tube there.
            std::vector<float> arc(n, 0.f);
            for (int i = 0; i < n; ++i) {
                double y = i * pr.step, round = 0.0;
                for (auto& piece : design.pieces) {
                    if (piece.isDerived() || !piece.inMuslin) continue;
                    if (groupOf(piece) != kv.first) continue;
                    Pt lo, hi;
                    geo::bounds(piece.sewing, lo, hi);
                    if (y < lo.y - 1e-3 || y > hi.y + 1e-3) continue;
                    round += arcAtLevel(piece, y);
                }
                arc[i] = (float)round;
            }
            // A sleeve's widest point is the bicep, and everything above it
            // is CAP -- which is not a tube at all. It is an open curve
            // that gets sewn into the armhole, and it rises out of the
            // sleeve rather than closing round anything. Letting the tube
            // taper to nothing through the cap wrapped those rows onto a
            // circle a third of the arm's size and stretched them by 70%.
            // Hold the arm's own tube up through the cap instead.
            if (kv.first == "sleeve") {
                int pk = (int)(std::max_element(arc.begin(), arc.end()) - arc.begin());
                for (int i = 0; i < pk; ++i) arc[i] = arc[pk];
            }

            // A panel's outline can touch a level at a single point -- the
            // neck point of a sloping shoulder line, the crown of a sleeve
            // cap -- and the reach there says nothing about how wide the
            // garment is. Take the widest reach nearby instead.
            std::vector<float> wide = arc;
            for (int i = 0; i < n; ++i)
                for (int k = std::max(0, i - 2); k <= std::min(n - 1, i + 2); ++k)
                    wide[i] = std::max(wide[i], arc[k]);
            arc.swap(wide);

            // And limit how fast the tube may narrow going up.
            //
            // This is the number that decides whether the garment starts
            // torn. The panel's x becomes a POSITION AROUND the tube, so
            // where the tube pinches the cloth at the far edge swings round
            // it -- a whole panel's width of travel for a couple of
            // centimetres of height. Raising the pinched levels (never
            // lowering the wide ones) keeps the cone shallow; cloth that
            // starts slightly too far out simply falls in, which is the
            // easy direction.
            const float kMaxSlope = 0.30f * 2.f * 3.14159265f;   // cm of arc per cm of height
            for (int i = 1; i < n; ++i) arc[i] = std::max(arc[i], arc[i - 1] - kMaxSlope * pr.step);
            for (int i = n - 2; i >= 0; --i) arc[i] = std::max(arc[i], arc[i + 1] - kMaxSlope * pr.step);

            // A light smoothing pass: the drafted outline has corners, and
            // a step in the tube shows up as a crease in the cloth.
            std::vector<float> sm = arc;
            for (int i = 1; i + 1 < n; ++i)
                sm[i] = (arc[i - 1] + 2.f * arc[i] + arc[i + 1]) * 0.25f;
            arc.swap(sm);

            // Split that arc into a width and a depth.
            //
            // A limb is round, so its two semi-axes are equal. A torso is
            // not, and using the BODY's proportions at each level is what
            // puts the side seam at the side and the shoulder seam over the
            // shoulder. Ramanujan's perimeter approximation inverts
            // cleanly once the ratio is known, because perimeter scales
            // linearly with the ellipse.
            bool limb = (kv.first != "torso");
            std::vector<float> ratio(n, 1.f);
            for (int i = 0; i < n && !limb; ++i) {
                float bw = 0.f, bd = 0.f;
                ratio[i] = (g.avatar && g.avatar->sectionAt(i * pr.step, bw, bd) && bd > 0.5f)
                         ? std::min(3.f, std::max(1.f, bw / bd))
                         : 1.5f;   // a torso, roughly, when there is no body to measure
            }
            if (!limb) {
                // At the very top the body being measured is the NECK,
                // which is round -- but the garment up there spans the
                // shoulders. Carry the shoulder's proportions up over it.
                for (int i = std::min(n - 2, (int)(12.f / pr.step)); i >= 0; --i)
                    ratio[i] = std::max(ratio[i], ratio[i + 1]);
                // Then smooth hard. A tube whose SHAPE changes quickly
                // shears the cloth wrapped on it just as one whose size
                // does, and a body's proportions really do change slowly.
                for (int pass = 0; pass < 8; ++pass) {
                    std::vector<float> t = ratio;
                    for (int i = 1; i + 1 < n; ++i)
                        ratio[i] = (t[i - 1] + 2.f * t[i] + t[i + 1]) * 0.25f;
                }
            }
            pr.half.assign(n, 0.f);
            pr.depth.assign(n, 0.f);
            for (int i = 0; i < n; ++i) {
                double k = ratio[i];
                double f = 3.14159265 * (3.0 * (k + 1.0)
                         - std::sqrt((3.0 * k + 1.0) * (k + 3.0)));
                double b = arc[i] / std::max(1e-3, f);
                pr.depth[i] = (float)b;
                pr.half[i] = (float)(k * b);
            }
            if (tracing()) {
                std::string row;
                for (int i = 0; i < n; i += 5)
                    row += ofToString(pr.half[i], 1) + "/" + ofToString(pr.depth[i], 1) + " ";
                ofLogNotice() << "SimTest:   wrap profile [" << kv.first
                              << "] half-width/half-depth every 5 cm: " << row;
            }
        }
    }

    // Where the ARMHOLE ended up on the body, so the sleeve can be hung
    // from it rather than from the shoulder joint.
    //
    // The two are not the same place. The arm's centre line starts inside
    // the shoulder, and a sleeve cap is measured flat, so a sleeve placed
    // straight onto the arm axis hangs its underarm five centimetres below
    // the armhole and a hand's breadth outboard -- which is a seam that
    // starts 16 cm open and has to drag the cap into place. Matching the
    // armhole's own two landmarks instead costs nothing and starts the
    // seam nearly shut.
    bool haveArmhole = false;
    glm::vec3 armholeTop(0), armholeBottom(0);
    for (auto& piece : design.pieces) {
        if (piece.isDerived() || !piece.inMuslin) continue;
        if (groupOf(piece) != "torso") continue;
        Pt tip, under;
        if (!common::findLandmark(piece, "shoulder tip", tip) ||
            !common::findLandmark(piece, "underarm", under)) continue;
        Wrap w = wrapFor(piece, design, g.body, g.avatar, tubeWidth["torso"],
                         &profiles["torso"], s.startInflateCm);
        armholeTop = place(w, tip, false);
        armholeBottom = place(w, under, false);
        haveArmhole = true;
        break;
    }

    for (auto& piece : design.pieces) {
        if (piece.isDerived() || !piece.inMuslin) continue;   // shell pieces only
        // A piece cut on the fold stores half the panel. The garment
        // contains the WHOLE panel, so mirror it and mesh that -- otherwise
        // the front of the dress is half a front and nothing closes.
        Ring outline = piece.sewing;
        if (piece.foldAtCF) {
            Ring mirrored = piece.sewing;
            for (auto& p : mirrored) p.x = -p.x;
            auto joined = geo::unionRings({ piece.sewing, mirrored });
            if (joined.size() == 1 && joined[0].size() >= 3) outline = joined[0];
        }
        Mesh2D m = triangulate(outline, s.resolutionCm);
        if (m.tris.empty()) continue;

        // A leg panel's arc is measured from its outseam, so record where
        // that edge is at each height. Smoothed, because the outline has
        // corners and a step here is a crease in the cloth.
        std::vector<float> outseamShift;
        if (groupOf(piece) == "leg") {
            Pt lo, hi;
            geo::bounds(piece.sewing, lo, hi);
            int n = std::max(2, (int)std::ceil(hi.y) + 2);
            outseamShift.assign(n, 0.f);
            for (int i = 0; i < n; ++i) {
                double y = std::min((double)hi.y, std::max((double)lo.y, (double)i));
                outseamShift[i] = -(float)geo::minXAtY(piece.sewing, y);
            }
            for (int pass = 0; pass < 3; ++pass) {
                std::vector<float> t = outseamShift;
                for (int i = 1; i + 1 < n; ++i)
                    outseamShift[i] = (t[i - 1] + 2.f * t[i] + t[i + 1]) * 0.25f;
            }
        }
        // Two panels wherever the garment has two: a mirrored pair, and
        // also a plain "CUT 2" like a sleeve -- there are two arms. Only
        // the mirrored-pair case was being made, so every sleeved garment
        // was simulated with one sleeve.
        //
        // A fold piece is already the whole panel, so it stays at one.
        int copies = piece.foldAtCF ? 1 : std::min(2, std::max(1, piece.cutCount));
        for (int k = 0; k < copies; ++k) {
            PanelMesh pm;
            pm.code = piece.code;
            pm.m = m;
            pm.mirrored = (k == 1);
            pm.fromFold = piece.foldAtCF;
            pm.base = (int)g.pos.size();
            Wrap w = wrapFor(piece, design, g.body, g.avatar, tubeWidth[groupOf(piece)],
                             &profiles[groupOf(piece)], s.startInflateCm);
            if (!outseamShift.empty()) w.shift = &outseamShift;
            if (haveArmhole && groupOf(piece) == "sleeve") {
                // Roll the tube so the cap crown faces the top of the
                // armhole. The sleeve has to follow the ARM -- that is what
                // makes it hang right -- but which way up it is around that
                // arm is free, and pointing the crown at the shoulder tip
                // instead of straight up costs nothing and stops the cap
                // arriving a quarter turn out of register.
                glm::vec3 up = armholeTop - w.origin;
                up -= w.axis * glm::dot(up, w.axis);
                if (glm::length(up) > 1.f) {
                    w.e1 = glm::normalize(up);
                    w.e0 = glm::cross(w.axis, w.e1);
                }
                // Then slide it along the arm until the cap sits on the
                // armhole on average. The cap's two corresponding points
                // are the crown, which meets the shoulder tip, and the
                // widest point of the cap, which meets the underarm.
                //
                // Only a translation. Bending the cap so both ends land
                // exactly was tried and is worse: it buys about a
                // centimetre of seam gap and costs three times the
                // placement distortion, because the correction it applies
                // varies fast across a part of the panel that is already
                // the most curved thing in the garment.
                Pt crown(0, 0), bicep(0, 0);
                for (auto& v : piece.sewing) if (v.x > bicep.x) bicep = v;
                glm::vec3 capTop = place(w, crown, false);
                glm::vec3 capBottom = place(w, bicep, false);
                glm::vec3 off = ((armholeTop + armholeBottom) - (capTop + capBottom)) * 0.5f;
                // ALONG the arm only.
                //
                // The full offset slides the sleeve sideways until its cap
                // sits on the armhole -- and takes it off the arm doing it,
                // 18 cm clear of the limb it is supposed to be a sleeve
                // for. Keeping only the component along the arm puts the
                // cap at the right HEIGHT while the sleeve stays a tube
                // round the arm; closing the rest of that gap is the
                // armhole seam's job, and it is the body panels that
                // should travel, because they are the ones with somewhere
                // to travel from.
                w.origin += w.axis * glm::dot(off, w.axis);

            }
            // The same wrap with the panel sitting ON the body, so the
            // sewing phase knows which way "in" is for every vertex.
            Wrap tight = w;
            tight.inflate = 0.f;
            for (auto& v : m.verts) {
                glm::vec3 out = place(w, v, pm.mirrored);
                g.deflate.push_back(place(tight, v, pm.mirrored) - out);
                g.pos.push_back(out);
                g.panelOf.push_back((int)panels.size());
                flat2D.push_back(v);     // the pattern, which is what cloth remembers
            }
            for (size_t t = 0; t < m.tris.size(); t += 3) {
                int a = pm.base + m.tris[t], b = pm.base + m.tris[t + 1], c = pm.base + m.tris[t + 2];
                g.tris.push_back(a); g.tris.push_back(pm.mirrored ? c : b);
                g.tris.push_back(pm.mirrored ? b : c);
                triPanel.push_back((int)panels.size());
            }
            instancesOf[piece.code].push_back((int)panels.size());
            g.panelNames.push_back(piece.code + (pm.mirrored ? " (mirrored)" : ""));
            panels.push_back(pm);
        }
    }
    if (g.pos.empty()) return g;

    // --- weld the seams -----------------------------------------------
    //
    // A seam is not a spring here: the two edges' boundary vertices are
    // merged, so the panels become one sheet. Springs across a seam
    // stretch under load and buzz; a welded seam simply holds, which is
    // what a sewn seam does.
    // NOT welded. Two panels that start 12-30 cm apart cannot be snapped
    // together without tearing every triangle near the seam -- the damage
    // is done before the solver runs, and relaxing afterwards cannot undo
    // it. Instead each matched pair becomes a CONSTRAINT that pulls the
    // two sides together over time, which is what sewing actually is: the
    // panels travel to meet each other while the cloth keeps its shape.
    double weldGapSum = 0.0, weldGapMax = 0.0; int weldGapN = 0;
    auto weld = [&](int ra, int rb) {
        if (ra == rb) return;
        // How far apart the two sides START. Small means the panels were
        // laid out consistently with their seams; large means the initial
        // arrangement disagrees with how the garment is actually sewn, and
        // welding will tear the mesh before the solver sees it.
        double gap = glm::distance(g.pos[ra], g.pos[rb]);
        weldGapSum += gap; weldGapMax = std::max(weldGapMax, gap); ++weldGapN;
        // Keep the lower index, and meet in the middle, as GarmentCode
        // does -- otherwise one panel snaps onto the other's position.
        g.seamPairs.push_back({ ra, rb });
        ++g.weldedPairs;
    };

    // Boundary vertices of one edge of a panel, in order.
    auto edgeVerts = [&](const PanelMesh& pm, const Piece& piece,
                         const std::string& from, const std::string& to,
                         bool mirrorLandmarks, std::vector<int>& out) {
        Pt A, B;
        if (!common::findLandmark(piece, from, A) || !common::findLandmark(piece, to, B)) return false;
        // A fold piece is stored as half a panel but simulated whole, so
        // its landmarks describe one side only. The other side is their
        // reflection -- and it is a DIFFERENT seam, joining the other of
        // the two panels opposite it.
        if (mirrorLandmarks) { A.x = -A.x; B.x = -B.x; }
        double total = common::seamEndLength(piece, from, to);
        if (total < 1.0) return false;
        // Walk the panel's boundary and keep the run between the two
        // landmarks, measured the short way.
        int ia = -1, ib = -1;
        double da = 1e9, db = 1e9;
        for (size_t k = 0; k < pm.m.boundary.size(); ++k) {
            Pt v = pm.m.verts[pm.m.boundary[k]];
            double d1 = glm::distance(v, A), d2 = glm::distance(v, B);
            if (d1 < da) { da = d1; ia = (int)k; }
            if (d2 < db) { db = d2; ib = (int)k; }
        }
        if (ia < 0 || ib < 0 || ia == ib) return false;
        int n = (int)pm.m.boundary.size();
        int fwd = (ib - ia + n) % n, back = (ia - ib + n) % n;
        out.clear();
        if (fwd <= back) { for (int k = 0; k <= fwd; ++k) out.push_back(pm.m.boundary[(ia + k) % n]); }
        else             { for (int k = 0; k <= back; ++k) out.push_back(pm.m.boundary[(ia - k + n) % n]); }
        return out.size() >= 2;
    };

    // How many physical joins this seam makes: a garment has a left and a
    // right side seam, and they join different panels.
    auto sidesOf = [&](const std::string& code) -> int {
        const Piece* p = pieceByCode(design, code);
        if (!p) return 0;
        if (!instancesOf.count(code)) return 0;
        // A fold panel is one mesh with two sides; a mirrored pair is two
        // meshes with one side each.
        return p->foldAtCF ? 2 : (int)instancesOf[code].size();
    };

    for (auto& seam : design.seams) {
        const Piece* pa = pieceByCode(design, seam.a.piece);
        const Piece* pb = pieceByCode(design, seam.b.piece);
        if (!pa || !pb || !instancesOf.count(seam.a.piece) || !instancesOf.count(seam.b.piece)) {
            ++g.seamsSkipped; continue;
        }
        auto& ia = instancesOf[seam.a.piece];
        auto& ib = instancesOf[seam.b.piece];
        int sides = std::min(sidesOf(seam.a.piece), sidesOf(seam.b.piece));
        if (seam.toMirrorOfSelf) sides = 1;
        if (sides < 1) { ++g.seamsSkipped; continue; }

        for (int k = 0; k < sides; ++k) {
            // Pick the panel and which side of it this join uses.
            int idxA = pa->foldAtCF ? ia[0] : ia[std::min((size_t)k, ia.size() - 1)];
            int idxB;
            if (seam.toMirrorOfSelf) {
                idxB = pb->foldAtCF ? ib[0] : ib[std::min<size_t>(1, ib.size() - 1)];
            } else {
                idxB = pb->foldAtCF ? ib[0] : ib[std::min((size_t)k, ib.size() - 1)];
            }
            const PanelMesh& A = panels[idxA];
            const PanelMesh& B = panels[idxB];

            // Landmarks are searched in the panel's own 2D coordinates.
            //
            // A FOLD panel is meshed as the whole mirrored outline, so it
            // really does have a -x side and its landmarks (recorded on
            // the stored half) must be reflected to address it.
            //
            // A mirrored COPY of a half panel is different: its 2D
            // coordinates are identical to the original, and only its
            // placement in 3D is reflected. Reflecting its landmarks too
            // sent the search to -x, where that panel has no outline at
            // all -- so it matched near center back instead of the side
            // seam, and the side of the garment was never sewn.
            bool mirrA = pa->foldAtCF && (k == 1);
            bool mirrB;
            if (seam.toMirrorOfSelf) mirrB = pb->foldAtCF;
            else                     mirrB = pb->foldAtCF && (k == 1);

            // Skip only a seam that joins an edge to ITSELF. A sleeve's
            // underarm seam joins two different edges of the same panel --
            // that is how a flat sleeve becomes a tube -- and rejecting it
            // for sharing a panel left every sleeve as an open sheet.
            bool sameEdge = (seam.a.from == seam.b.from && seam.a.to == seam.b.to);
            if (idxA == idxB && mirrA == mirrB && sameEdge) { ++g.seamsSkipped; continue; }

            int nBefore = weldGapN;
            std::vector<int> ea, eb;
            if (!edgeVerts(A, *pa, seam.a.from, seam.a.to, mirrA, ea) ||
                !edgeVerts(B, *pb, seam.b.from, seam.b.to, mirrB, eb)) { ++g.seamsSkipped; continue; }

            // Match by FRACTION along each edge, not by index: the two
            // edges rarely have the same number of vertices, and a seam
            // with ease designed in (a sleeve cap) genuinely has more on
            // one side.
            //
            // WHICH WAY ROUND matters as much as where. The two edges are
            // walked from their own landmarks, and nothing guarantees the
            // two walks run the same way along the seam -- if they oppose,
            // the top of one edge gets welded to the bottom of the other
            // and the seam is twisted. No amount of relaxing fixes that;
            // the mesh is impossible. So try both pairings and keep the
            // one that has less work to do. (GarmentCode carries this as
            // an explicit `swap` on the stitch.)
            int n = (int)std::max(ea.size(), eb.size());
            auto pairCost = [&](bool reversed) {
                double sum = 0.0;
                for (int i = 0; i < n; ++i) {
                    double f = (n == 1) ? 0.0 : (double)i / (n - 1);
                    double fb = reversed ? 1.0 - f : f;
                    int va = ea[std::min((size_t)std::llround(f * (ea.size() - 1)), ea.size() - 1)];
                    int vb = eb[std::min((size_t)std::llround(fb * (eb.size() - 1)), eb.size() - 1)];
                    sum += glm::distance(g.pos[A.base + va], g.pos[B.base + vb]);
                }
                return sum;
            };
            bool reversed = pairCost(true) < pairCost(false);
            for (int i = 0; i < n; ++i) {
                double f = (n == 1) ? 0.0 : (double)i / (n - 1);
                double fb = reversed ? 1.0 - f : f;
                int va = ea[std::min((size_t)std::llround(f * (ea.size() - 1)), ea.size() - 1)];
                int vb = eb[std::min((size_t)std::llround(fb * (eb.size() - 1)), eb.size() - 1)];
                weld(A.base + va, B.base + vb);
            }
            if (reversed) ++g.seamsReversed;
            // Report per seam, so a bad one can be NAMED. An average over
            // the whole garment says the assembly is strained somewhere
            // and nothing about where, which is no use at all when one
            // seam out of eleven is the problem.
            double mySum = 0.0, myMax = 0.0;
            for (int i = nBefore; i < weldGapN; ++i) {
                double d = glm::distance(g.pos[g.seamPairs[i].first], g.pos[g.seamPairs[i].second]);
                mySum += d;
                myMax = std::max(myMax, d);
            }
            int made = weldGapN - nBefore;
            g.seamReport.push_back({
                seam.name + (sides > 1 ? (k == 0 ? " (right)" : " (left)") : ""),
                made ? (float)(mySum / made) : 0.f, (float)myMax, made, reversed });
            ++g.seamsApplied;
        }
    }
    g.weldGapAvg = weldGapN ? (float)(weldGapSum / weldGapN) : 0.f;
    g.weldGapMax = (float)weldGapMax;

    // Every vertex keeps its own flat position, because nothing was
    // merged -- which makes rest lengths simply correct.
    std::vector<int> remap(g.pos.size());
    for (size_t i = 0; i < remap.size(); ++i) remap[i] = (int)i;
    std::vector<int> origPanel = g.panelOf;
    std::map<std::pair<int, int>, Pt> flatOf;
    for (size_t i = 0; i < remap.size(); ++i)
        flatOf.emplace(std::make_pair((int)i, origPanel[i]), flat2D[i]);
    auto restLen = [&](int a, int b, int panel) -> float {
        auto ia = flatOf.find(std::make_pair(a, panel));
        auto ib = flatOf.find(std::make_pair(b, panel));
        if (ia != flatOf.end() && ib != flatOf.end())
            return glm::distance(ia->second, ib->second);
        return glm::distance(g.pos[a], g.pos[b]);
    };

    // --- constraints, measured on the FLAT pattern --------------------
    g.prev = g.pos;
    g.vel.assign(g.pos.size(), glm::vec3(0));
    g.invMass.assign(g.pos.size(), 1.f);

    // What the sewing phase has to close. Each pair's target walks from
    // here to zero over the assembly, so the two sides travel toward each
    // other at a rate the cloth between them can follow.
    g.seamGap0.resize(g.seamPairs.size());
    for (size_t i = 0; i < g.seamPairs.size(); ++i)
        g.seamGap0[i] = glm::distance(g.pos[g.seamPairs[i].first], g.pos[g.seamPairs[i].second]);
    g.assemblyStep = 0;
    g.assemblyTotal = g.seamPairs.empty() ? 0 : std::max(0, s.assemblySteps);
    if (g.deflate.size() != g.pos.size()) g.deflate.assign(g.pos.size(), glm::vec3(0));

    std::map<std::pair<int, int>, std::vector<int>> edgeTris;
    auto key = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
    std::set<std::pair<int, int>> seen;
    for (size_t t = 0; t < g.tris.size(); t += 3) {
        int v[3] = { g.tris[t], g.tris[t + 1], g.tris[t + 2] };
        for (int e = 0; e < 3; ++e) {
            auto k = key(v[e], v[(e + 1) % 3]);
            edgeTris[k].push_back((int)t);
            if (!seen.insert(k).second) continue;
            float rest = restLen(k.first, k.second, triPanel[t / 3]);
            if (rest > 1e-4f) g.stretch.push_back({ k.first, k.second, rest });
        }
    }
    // --- settle the assembly before anything is asked to drape ---------
    //
    // The panels are laid out roughly and then welded, so the mesh starts
    // badly distorted -- seam sides begin centimetres apart and the weld
    // drags them together, stretching everything nearby. Dropping gravity
    // on THAT is why the garment looked wrong: it was draping a shape that
    // had never been allowed to become the garment.
    //
    // So: relax the stretch constraints on their own first, with no
    // gravity and no velocity, letting the mesh pull itself back to the
    // dimensions the flat pattern says it has. This is the assembly step a
    // pattern goes through before anyone puts it on.
    for (auto& kv : edgeTris) {
        if (kv.second.size() != 2) continue;
        auto opposite = [&](int t) {
            for (int i = 0; i < 3; ++i) {
                int v = g.tris[t + i];
                if (v != kv.first.first && v != kv.first.second) return v;
            }
            return -1;
        };
        int o1 = opposite(kv.second[0]), o2 = opposite(kv.second[1]);
        if (o1 < 0 || o2 < 0 || o1 == o2) continue;
        float rest = restLen(o1, o2, triPanel[kv.second[0] / 3]);
        if (rest > 1e-4f) g.bend.push_back({ o1, o2, rest });
    }
    return g;
}

// Where the time goes, so optimisation aims at the right thing.
SimProfile gProfile;

void step(SimGarment& g, const SimSettings& s, double dt) {
    if (g.empty()) return;
    int sub = std::max(1, s.substeps);
    double h = dt / sub;

    // Sewing, then draping. While the seams are still being drawn
    // together the garment is not hanging on anyone -- it is being made --
    // so gravity is off and the cloth is damped hard, which keeps the
    // panels from swinging as they travel. `sew` runs 0 -> 1 across the
    // phase and is what each seam's target is scaled by.
    bool sewing = g.assembling();
    float sew = g.assemblyTotal > 0
              ? std::min(1.f, (float)g.assemblyStep / (float)g.assemblyTotal) : 1.f;
    if (sewing) ++g.assemblyStep;

    // Coming in to the body, spread evenly across the sewing. Applied to
    // the positions rather than as a force, because this is not physics --
    // it is the wearer putting the thing on. Collision still runs after
    // it, so nothing is driven through the body on the way.
    if (sewing && g.assemblyTotal > 0 && g.deflate.size() == g.pos.size()) {
        float k = 1.f / (float)g.assemblyTotal;
        for (size_t i = 0; i < g.pos.size(); ++i) g.pos[i] += g.deflate[i] * k;
    }

    for (int it = 0; it < sub; ++it) {
        for (size_t i = 0; i < g.pos.size(); ++i) {
            if (g.invMass[i] <= 0.f) continue;
            if (!sewing) g.vel[i].y += (float)(s.gravity * h);
            g.vel[i] *= (float)(sewing ? 0.8 : s.damping);
            g.prev[i] = g.pos[i];
            g.pos[i] += g.vel[i] * (float)h;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        double aStretch = s.stretchCompliance / (h * h);
        double aBend = s.bendCompliance / (h * h);
        double aSeam = s.seamCompliance / (h * h);
        for (int k = 0; k < s.iterations; ++k) {
            for (auto& c : g.stretch) {
                glm::vec3 d = g.pos[c.a] - g.pos[c.b];
                float len = glm::length(d);
                if (len < 1e-6f) continue;
                float w = g.invMass[c.a] + g.invMass[c.b];
                if (w <= 0.f) continue;
                float corr = (len - c.rest) / (w + (float)aStretch);
                glm::vec3 n = d / len;
                g.pos[c.a] -= n * corr * g.invMass[c.a];
                g.pos[c.b] += n * corr * g.invMass[c.b];
            }
            for (size_t si = 0; si < g.seamPairs.size(); ++si) {
                auto& sp = g.seamPairs[si];
                // A sewn seam holds the two sides together, solved the
                // same way every other constraint is so that it converges
                // WITH the cloth rather than overpowering it.
                //
                // While sewing, it holds them at a distance that shrinks
                // instead: the seam is being closed, not yet closed. Going
                // straight to zero from 20 cm apart is the snap that used
                // to shred every triangle near a seam before the drape had
                // begun.
                float target = 0.f;
                if (sewing && si < g.seamGap0.size()) target = g.seamGap0[si] * (1.f - sew);
                glm::vec3 d = g.pos[sp.second] - g.pos[sp.first];
                float len = glm::length(d);
                if (len < 1e-6f) continue;
                float w = g.invMass[sp.first] + g.invMass[sp.second];
                if (w <= 0.f) continue;
                float corr = (len - target) / (w + (float)aSeam);
                glm::vec3 n = d / len;
                g.pos[sp.first] += n * corr * g.invMass[sp.first];
                g.pos[sp.second] -= n * corr * g.invMass[sp.second];
            }
            for (auto& c : g.bend) {
                glm::vec3 d = g.pos[c.a] - g.pos[c.b];
                float len = glm::length(d);
                if (len < 1e-6f) continue;
                float w = g.invMass[c.a] + g.invMass[c.b];
                if (w <= 0.f) continue;
                float corr = (len - c.rest) / (w + (float)aBend);
                glm::vec3 n = d / len;
                g.pos[c.a] -= n * corr * g.invMass[c.a];
                g.pos[c.b] += n * corr * g.invMass[c.b];
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        // Collision ONCE per substep, not once per solver iteration, and
        // spread across cores. Each vertex is pushed out independently --
        // nothing is shared or accumulated -- so this parallelises without
        // any locking. It is 93% of the frame, so it is the only part
        // worth threading.
        {
            const int nv = (int)g.pos.size();
            unsigned hw = std::thread::hardware_concurrency();
            int threads = std::max(1, std::min<int>(hw ? hw : 4, 8));
            if (nv < 400) threads = 1;
            auto chunk = [&](int lo, int hi) {
                for (int i = lo; i < hi; ++i) {
                    glm::vec3& p = g.pos[i];
                    if (g.avatar) {
                        glm::vec3 before = p;
                        if (g.avatar->pushOut(p, (float)s.bodyMarginCm)) {
                            glm::vec3 n = p - before;
                            float l = glm::length(n);
                            if (l > 1e-5f) {
                                n /= l;
                                glm::vec3 moved = p - g.prev[i];
                                glm::vec3 tang = moved - n * glm::dot(moved, n);
                                p -= tang * (float)s.bodyFriction;
                            }
                        }
                        continue;
                    }
                    for (auto& cap : g.body) {
                        glm::vec3 ab = cap.b - cap.a;
                        float l2 = glm::dot(ab, ab);
                        float t = l2 < 1e-6f ? 0.f : glm::clamp(glm::dot(p - cap.a, ab) / l2, 0.f, 1.f);
                        glm::vec3 c = cap.a + ab * t;
                        glm::vec3 d = p - c;
                        float dist = glm::length(d);
                        float want = cap.r + (float)s.bodyMarginCm;
                        if (dist >= want || dist <= 1e-5f) continue;
                        glm::vec3 nn = d / dist;
                        p = c + nn * want;
                        glm::vec3 moved = p - g.prev[i];
                        glm::vec3 tang = moved - nn * glm::dot(moved, nn);
                        p -= tang * (float)s.bodyFriction;
                    }
                }
            };
            if (threads <= 1) {
                chunk(0, nv);
            } else {
                std::vector<std::thread> pool;
                int per = (nv + threads - 1) / threads;
                for (int t = 0; t < threads; ++t) {
                    int lo = t * per, hi = std::min(nv, lo + per);
                    if (lo < hi) pool.emplace_back(chunk, lo, hi);
                }
                for (auto& th : pool) th.join();
            }
        }
        auto t2 = std::chrono::high_resolution_clock::now();
        gProfile.constraintsMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        gProfile.collisionMs += std::chrono::duration<double, std::milli>(t2 - t1).count();

        for (size_t i = 0; i < g.pos.size(); ++i) {
            g.vel[i] = (g.pos[i] - g.prev[i]) / (float)h;
            // A collision push-out can be large, and deriving velocity
            // from it hands the next substep an enormous speed that tears
            // the mesh. Cap it: cloth does not move faster than this.
            float sp = glm::length(g.vel[i]);
            const float kMaxSpeed = 900.f;   // cm/s
            if (sp > kMaxSpeed) g.vel[i] *= kMaxSpeed / sp;
        }
    }

    // How well the sewing actually closed, recorded on the last frame of
    // the phase. Worth reporting rather than assuming: a seam that is
    // still open when gravity arrives will be pulled open further.
    if (sewing && !g.assembling() && !g.seamPairs.empty()) {
        double sum = 0.0;
        for (auto& sp : g.seamPairs) sum += glm::distance(g.pos[sp.first], g.pos[sp.second]);
        g.seamGapAfterAssembly = (float)(sum / g.seamPairs.size());
    }
}

} } // namespace pf::sim
