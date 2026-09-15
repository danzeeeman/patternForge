#include "Clo3dScript.h"

namespace pf { namespace exportx {

const std::string& clo3dBuildScript() {
    static const std::string kScript = R"PY(#!/usr/bin/env python3
"""Build this pattern as a sewn 3D garment in CLO3D.

Run it from CLO's Python console (or Script > Run Script):

    exec(open('/path/to/clo3d_build.py').read())

or check it outside CLO first, which needs nothing but Python:

    python3 clo3d_build.py --dry-run

WHAT IT DOES
    Reads Geometry-and-Checks.json from the folder it sits in, creates one
    CLO pattern per piece, mirrors the ones cut in pairs or on the fold,
    arranges them around the avatar, sews them along the seam list, and
    simulates.

    The sewing is the point. Importing Pattern.dxf gets you flat pieces
    lying next to each other and nothing joined -- CLO cannot infer which
    edge meets which. The package's "seams" list says exactly that, as
    spans between named landmarks on each outline, so this script can
    resolve every seam to two runs of points and sew them.

WHAT IS VERIFIED AND WHAT IS NOT
    Everything up to the CLO calls -- reading the package, resolving each
    seam to two edges, checking those edges are the lengths they claim,
    working out mirrors and placement -- runs under --dry-run and has been
    tested on every package this tool exports.

    The CLO calls themselves have NOT been run against CLO. They live in
    one class, CloAdapter, and nowhere else. CLO's Python API differs
    between versions, so check the method bodies there against the API
    docs for your build; everything below that class is version-neutral.
"""

import json
import math
import os
import sys

CM_TO_MM = 10.0


# ---------------------------------------------------------------------
# Reading the package
# ---------------------------------------------------------------------

def package_dir():
    """The folder this script sits in, which is the package folder."""
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except NameError:
        # CLO's console exec()s the file, so __file__ may not be set.
        return os.getcwd()


def load_package(folder):
    path = os.path.join(folder, "Geometry-and-Checks.json")
    if not os.path.exists(path):
        raise SystemExit("No Geometry-and-Checks.json in %s" % folder)
    with open(path) as f:
        return json.load(f)


# ---------------------------------------------------------------------
# Resolving the seam graph onto the outlines
# ---------------------------------------------------------------------

def landmarks(piece):
    return {m["label"]: (m["x"], m["y"])
            for m in piece.get("marks", []) if m["kind"] == "landmark"}


def notches(piece):
    return [(m["label"], m["x"], m["y"])
            for m in piece.get("marks", []) if m["kind"] == "notch"]


def nearest_index(ring, pt):
    best, bestd = 0, float("inf")
    for i, (x, y) in enumerate(ring):
        d = (x - pt[0]) ** 2 + (y - pt[1]) ** 2
        if d < bestd:
            best, bestd = i, d
    return best


def span_length(ring, idx):
    return sum(math.dist(ring[idx[k]], ring[idx[k + 1]]) for k in range(len(idx) - 1))


def resolve_edge(ring, start_pt, end_pt):
    """Point indices from start to end, the short way round the outline.

    An edge between two corners is the near side of the outline; going the
    long way would wrap a seam around the whole piece.
    """
    i, j = nearest_index(ring, start_pt), nearest_index(ring, end_pt)
    n = len(ring)
    fwd = [(i + k) % n for k in range((j - i) % n + 1)]
    back = [(i - k) % n for k in range((i - j) % n + 1)]
    return fwd if span_length(ring, fwd) <= span_length(ring, back) else back


def piece_by_code(pkg, code):
    for p in pkg["new_pieces"]:
        if p["code"] == code:
            return p
    return None


def resolve_seams(pkg):
    """[(seam, a_piece, a_indices, b_piece, b_indices)], plus any problems."""
    out, problems = [], []
    for seam in pkg.get("seams", []):
        a = piece_by_code(pkg, seam["a"]["piece"])
        b = piece_by_code(pkg, seam["b"]["piece"])
        if a is None or b is None:
            problems.append("%s: unknown piece" % seam["name"])
            continue
        la, lb = landmarks(a), landmarks(b)
        for end, lm in ((seam["a"], la), (seam["b"], lb)):
            for key in ("from", "to"):
                if end[key] not in lm:
                    problems.append("%s: %s has no landmark %r"
                                    % (seam["name"], end["piece"], end[key]))
        if problems and problems[-1].startswith(seam["name"]):
            continue

        ra = [(q[0], q[1]) for q in a["sewing_cm"]]
        rb = [(q[0], q[1]) for q in b["sewing_cm"]]
        ia = resolve_edge(ra, la[seam["a"]["from"]], la[seam["a"]["to"]])
        ib = resolve_edge(rb, lb[seam["b"]["from"]], lb[seam["b"]["to"]])
        if len(ia) < 2 or len(ib) < 2:
            problems.append("%s: edge resolved to a single point" % seam["name"])
            continue

        # The lengths must match, allowing for ease the pattern designed in
        # (a sleeve cap is cut longer than its armhole on purpose).
        ease = seam.get("easeFactor", 1.0)
        want = span_length(ra, ia) * ease
        got = span_length(rb, ib)
        if not seam.get("toMirrorOfSelf") and abs(want - got) > 1.0:
            problems.append("%s: edges %.1f vs %.1f cm" % (seam["name"], want, got))
        out.append((seam, a, ia, b, ib))
    return out, problems


# ---------------------------------------------------------------------
# How each piece is cut, and where it goes on the avatar
# ---------------------------------------------------------------------

def copies_of(piece):
    """One entry per PHYSICAL panel: (label, mirrored).

    The package now carries the counts directly (`cutCount`, `cutMirrored`),
    parsed once by the app, so this no longer reads them out of the prose
    cutting list. A piece cut on the fold is one whole panel; a mirrored
    pair is two, the second reflected.
    """
    count = int(piece.get("cutCount", 1) or 1)
    mirrored_n = count // 2 if piece.get("cutMirrored") else 0
    out = []
    for i in range(count):
        mirrored = i >= count - mirrored_n
        if count == 1:
            label = "whole-on-fold" if piece.get("foldAtCF") else "single"
        else:
            label = f"{i + 1}/{count}" + (" mirrored" if mirrored else "")
        out.append((label, mirrored))
    return out


def placement(code, piece):
    """Rough (x, y, z) in cm to hang the piece on before simulating.

    CLO needs pieces positioned around the avatar before it sews: two
    panels that start on top of each other simulate into a knot. Fronts go
    in front, backs behind, sleeves out to the sides.
    """
    base = code.lstrip("P")  # trouser pieces carry a P prefix
    trouser = code.startswith("P") and code != "PL"
    y = -60.0 if trouser else 0.0
    if base.startswith("SL"):
        return (35.0, y, 0.0)
    if base.startswith("B"):
        return (0.0, y, -20.0)
    return (0.0, y, 20.0)


# ---------------------------------------------------------------------
# The CLO API adapter -- the only version-specific code in this file
# ---------------------------------------------------------------------

class CloAdapter:
    """Wraps the handful of CLO operations this script needs.

    CHECK THESE AGAINST YOUR CLO VERSION'S PYTHON API before relying on
    them. They are written to the documented shape of the CLO API, but
    have not been run against CLO here, and the names have changed between
    releases. Everything outside this class is plain Python.

    The operations needed are:
        add_pattern(points_mm, name)   -> handle for a new pattern piece
        mirror(handle)                 -> handle for its mirrored copy
        place(handle, x, y, z)         -> position it around the avatar
        sew(h1, pts1, h2, pts2)        -> sew two runs of points together
        simulate()                     -> drape it
    """

    # Module names to try. These are GUESSES -- see report_environment(),
    # which prints what this CLO build actually exposes.
    CANDIDATES = ("CLOAPI", "CLO", "clo", "clo3d", "MV", "MVAPI", "marvelous", "API", "api")

    def __init__(self):
        self.api = None
        self.module_name = None
        for name in self.CANDIDATES:
            try:
                self.api = __import__(name)
                self.module_name = name
                break
            except Exception:
                continue
        self.available = self.api is not None

    def add_pattern(self, points_mm, name):
        return self.api.AddPattern(points_mm, name)

    def mirror(self, handle):
        return self.api.MirrorPattern(handle)

    def place(self, handle, x_cm, y_cm, z_cm):
        self.api.SetPatternPosition(handle,
                                    x_cm * CM_TO_MM, y_cm * CM_TO_MM, z_cm * CM_TO_MM)

    def sew(self, h1, pts1_mm, h2, pts2_mm):
        self.api.AddSewing(h1, pts1_mm, h2, pts2_mm)

    def simulate(self):
        self.api.Simulate()


def report_environment(log=print):
    """Print what CLO API surface this interpreter has.

    The adapter above guesses at module names, because CLO's Python API is
    named differently across versions. This prints what is actually here,
    so the adapter can be pointed at the real thing instead of guessed at.
    """
    log("")
    log("  --- CLO API discovery -------------------------------------")
    log("  python %s" % sys.version.split()[0])

    hits = sorted(n for n in sys.modules
                  if any(k in n.lower() for k in ("clo", "marvelous", "mv"))
                  and not n.startswith("_"))
    log("  loaded modules that look CLO-ish: %s" % (", ".join(hits) if hits else "(none)"))

    names = sorted(n for n in globals()
                   if any(k in n.lower() for k in ("clo", "marvelous", "mv", "api"))
                   and not n.startswith("_"))
    log("  CLO-ish names already in scope:   %s" % (", ".join(names) if names else "(none)"))

    for name in CloAdapter.CANDIDATES:
        try:
            mod = __import__(name)
        except Exception:
            continue
        funcs = [f for f in dir(mod) if not f.startswith("_")]
        log("  import %s -> OK, %d names" % (name, len(funcs)))
        interesting = [f for f in funcs if any(
            k in f.lower() for k in ("pattern", "sew", "simulate", "avatar",
                                     "import", "mirror", "position"))]
        log("    pattern/sewing-related: %s" % (", ".join(interesting[:40]) or "(none)"))
        if not interesting:
            log("    first 40 names: %s" % ", ".join(funcs[:40]))
    log("  -----------------------------------------------------------")
    log("  If nothing was found, check CLO's Python API docs for the")
    log("  module name and edit CloAdapter -- nothing else needs changing.")
    log("")


# ---------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------

def build(pkg, adapter, log=print):
    resolved, problems = resolve_seams(pkg)
    for p in problems:
        log("  PROBLEM  %s" % p)

    handles = {}
    for piece in pkg["new_pieces"]:
        ring_mm = [(q[0] * CM_TO_MM, q[1] * CM_TO_MM) for q in piece["sewing_cm"]]
        for which, mirrored in copies_of(piece):
            name = "%s (%s)" % (piece["code"], which)
            if adapter.available:
                h = adapter.add_pattern(ring_mm, name)
                if mirrored:
                    h = adapter.mirror(h)
                x, y, z = placement(piece["code"], piece)
                adapter.place(h, -x if mirrored else x, y, z)
            else:
                h = name
            handles.setdefault(piece["code"], []).append(h)
        log("  piece %-4s %d point(s), %s"
            % (piece["code"], len(ring_mm),
               " + ".join(w for w, _ in copies_of(piece))))

    for seam, a, ia, b, ib in resolved:
        # A seam to the piece's own mirror joins copy 0 to copy 1; when the
        # piece is cut on the fold it is one panel and the seam is internal
        # to it, so there is nothing to join.
        ha = handles[a["code"]][0]
        hb = handles[b["code"]][-1] if seam.get("toMirrorOfSelf") else handles[b["code"]][0]
        if seam.get("toMirrorOfSelf") and len(handles[b["code"]]) < 2:
            log("  skip seam %-22s (piece is one panel; seam is internal)" % seam["name"])
            continue
        if adapter.available:
            pa = [(a["sewing_cm"][i][0] * CM_TO_MM, a["sewing_cm"][i][1] * CM_TO_MM) for i in ia]
            pb = [(b["sewing_cm"][i][0] * CM_TO_MM, b["sewing_cm"][i][1] * CM_TO_MM) for i in ib]
            adapter.sew(ha, pa, hb, pb)
        log("  sew  %-22s %s[%d pts] <-> %s[%d pts]"
            % (seam["name"], a["code"], len(ia), b["code"], len(ib)))

    if adapter.available:
        adapter.simulate()
        log("  simulated")
    return len(problems)


def main(argv):
    dry = "--dry-run" in argv
    if "--report" in argv:
        report_environment()
    folder = package_dir()
    pkg = load_package(folder)
    print("%s  (%d pieces, %d seams)"
          % (os.path.basename(folder), len(pkg["new_pieces"]), len(pkg.get("seams", []))))

    adapter = CloAdapter()
    if dry:
        adapter.available = False
        print("  dry run: resolving the pattern only, no CLO calls")
    elif not adapter.available:
        print("  CLO's Python API was not found under any name this script knows,")
        print("  so the pattern is resolved but nothing is built.")
        report_environment()
    else:
        print("  using CLO API module %r" % adapter.module_name)

    problems = build(pkg, adapter)
    print("  %s" % ("OK" if problems == 0 else "%d problem(s)" % problems))
    return 1 if problems else 0


if __name__ == "__main__":
    _status = main(sys.argv[1:])
    # Only a real shell run should set an exit code. CLO's console exec()s
    # this file into a live session, where sys.exit() raises SystemExit and
    # surfaces as a traceback even when the run succeeded.
    if os.path.basename(sys.argv[0] or "").startswith("clo3d_build"):
        sys.exit(_status)
)PY";
    return kScript;
}

} } // namespace pf::exportx
