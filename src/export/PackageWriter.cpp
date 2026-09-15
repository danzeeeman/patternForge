#include "PackageWriter.h"
#include "PdfWriter.h"
#include "FullSizeExport.h"
#include "TiledExport.h"
#include "InstructionsExport.h"
#include "GeometryJsonExport.h"
#include "DxfExport.h"
#include "../garments/MuslinDraft.h"
#include "Clo3dScript.h"
#include "ofMain.h"
#include <sstream>
#include <cstdlib>

namespace pf { namespace exportx {

static std::string readmeText(const DesignResult& design) {
    std::ostringstream ss;
    ss << "START WITH Instructions.pdf. Use only the cutting PDFs in this folder.\n";
    ss << design.garmentKey << " / size preset " << design.size.name
       << " / generative development draft; sew a toile.\n";
    ss << "Regeneration: open patternForge, click \"Load design-project.json...\" in the Design "
       << "panel and pick source/design-project.json, then export again -- every piece is rebuilt "
       << "from the measurements and style values stored there, not from a fixed drawing.\n";
    ss << "SEW THE MUSLIN FIRST. Muslin-Full-Size.pdf / Muslin-A4.pdf / Muslin-US-Letter.pdf are "
       << "the fitting muslin (toile): the pieces that carry the fit, cut with 2.5 cm seam "
       << "allowances so they can be let out on the body. Collars, cuffs, plackets, waistbands "
       << "and facings are left out on purpose -- they finish the garment, they do not change how "
       << "it hangs. Muslin-Instructions.pdf has the fitting steps.\n";
    ss << "clo3d_build.py builds the garment in CLO3D: run it from CLO's Python console "
       << "(exec(open('.../clo3d_build.py').read())) and it creates every piece, mirrors the ones "
       << "cut in pairs or on the fold, arranges them on the avatar, sews them along the seam list "
       << "in Geometry-and-Checks.json, and simulates. Check it first with "
       << "\"python3 clo3d_build.py --dry-run\", which needs no CLO. Importing the DXF instead "
       << "gets you flat pieces with nothing joined -- the seam list is what says which edge sews "
       << "to which.\n";
    ss << "Pattern.dxf is the same pieces for pattern CAD (CLO3D, Browzwear, Optitex, Gerber): "
       << "AAMA/ASTM-style DXF in millimetres, one BLOCK per piece, numbered layers "
       << "(1 boundary, 4 notches, 6 mirror, 7 grain, 13 text, 14 sew line). In CLO3D use "
       << "File > Import > DXF (AAMA/ASTM). Pieces cut on the fold are written as the whole "
       << "panel with the fold marked, which is the shape actually cut.\n";
    return ss.str();
}

PackageResult writePackage(const DesignResult& design, const std::string& outputRootDir,
                           const ofJson& outlineEdits, const std::string& stamp) {
    PackageResult result;
    result.stem = design.stem + (stamp.empty() ? "" : "-" + stamp);
    result.folderPath = outputRootDir + "/" + result.stem;

    ofDirectory dir(result.folderPath);
    dir.create(true);
    ofDirectory sourceDir(result.folderPath + "/source");
    sourceDir.create(true);

    pdfx::PdfWriter full;
    if (!full.open(result.folderPath + "/Full-Size.pdf")) { result.error = "could not open Full-Size.pdf for writing"; return result; }
    result.fullSizeBoards = writeFullSize(full, design);
    full.close();

    pdfx::PdfWriter a4;
    a4.open(result.folderPath + "/A4.pdf");
    result.a4Pages = writeTiled(a4, design, kA4, "A4");
    a4.close();

    pdfx::PdfWriter letter;
    letter.open(result.folderPath + "/US-Letter.pdf");
    result.letterPages = writeTiled(letter, design, kUSLetter, "US LETTER");
    letter.close();

    pdfx::PdfWriter instr;
    instr.open(result.folderPath + "/Instructions.pdf");
    result.instructionPages = writeInstructions(instr, design, result.fullSizeBoards, { result.a4Pages, result.letterPages });
    instr.close();

    // CAD / cutting-table copy of the same pieces.
    DxfResult dxf = writeDxf(design, result.folderPath + "/Pattern.dxf");
    if (!dxf.ok) { result.error = "DXF: " + dxf.error; return result; }
    result.dxfEntities = dxf.entities;
    result.dxfPanels = dxf.panels;

    // The fitting muslin: the same garment stripped to the pieces that
    // carry the fit, with seam allowances wide enough to let out on the
    // body. Every module's own instructions tell you to sew a toile before
    // cutting cloth, so the pattern for that toile ships with the pattern.
    DesignResult muslin = muslin::muslinFrom(design);
    result.muslinPieces = (int)muslin.pieces.size();
    if (!muslin.pieces.empty()) {
        pdfx::PdfWriter mFull;
        if (mFull.open(result.folderPath + "/Muslin-Full-Size.pdf")) {
            result.muslinBoards = writeFullSize(mFull, muslin);
            mFull.close();
        }
        pdfx::PdfWriter mA4;
        if (mA4.open(result.folderPath + "/Muslin-A4.pdf")) {
            result.muslinA4Pages = writeTiled(mA4, muslin, kA4, "A4");
            mA4.close();
        }
        pdfx::PdfWriter mLetter;
        if (mLetter.open(result.folderPath + "/Muslin-US-Letter.pdf")) {
            result.muslinLetterPages = writeTiled(mLetter, muslin, kUSLetter, "US LETTER");
            mLetter.close();
        }
        pdfx::PdfWriter mInstr;
        if (mInstr.open(result.folderPath + "/Muslin-Instructions.pdf")) {
            writeInstructions(mInstr, muslin, result.muslinBoards,
                              { result.muslinA4Pages, result.muslinLetterPages });
            mInstr.close();
        }
        writeDxf(muslin, result.folderPath + "/Muslin.dxf");
    }

    ofJson geometry = buildGeometryJson(design);
    geometry["muslin"] = buildGeometryJson(muslin);
    ofSavePrettyJson(result.folderPath + "/Geometry-and-Checks.json", geometry);

    // The design's own "source": everything needed to rebuild every piece
    // in-app, standing in for a source PDF when a garment is drafted
    // rather than remixed from one.
    ofJson project;
    project["garmentKey"] = design.garmentKey;
    project["sizePreset"] = design.size.name;
    ofJson sizeCm;
    for (auto& kv : design.size.cm) sizeCm[kv.first] = kv.second;
    project["sizeMeasurementsCm"] = sizeCm;
    ofJson styleJ;
    for (auto& kv : design.style.values) styleJ[kv.first] = kv.second;
    project["style"] = styleJ;
    if (!outlineEdits.empty()) project["outlineEdits"] = outlineEdits;
    ofSavePrettyJson(result.folderPath + "/source/design-project.json", project);

    // The CLO3D assembly script, alongside the seam list it reads.
    {
        ofFile clo(result.folderPath + "/clo3d_build.py", ofFile::WriteOnly);
        clo << clo3dBuildScript();
        clo.close();
    }

    ofFile readme(result.folderPath + "/READ-ME.txt", ofFile::WriteOnly);
    readme << readmeText(design);
    readme.close();

    result.zipPath = outputRootDir + "/" + result.stem + ".zip";
    std::string cmd = "cd \"" + outputRootDir + "\" && rm -f \"" + result.stem + ".zip\" && zip -r -q \"" +
                       result.stem + ".zip\" \"" + result.stem + "\"";
    int rc = std::system(cmd.c_str());
    if (rc != 0) { result.error = "zip command failed"; return result; }

    result.ok = true;
    return result;
}

} } // namespace pf::exportx
