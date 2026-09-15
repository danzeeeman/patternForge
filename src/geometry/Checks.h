#pragma once
#include "Perimeter.h"
#include <string>
#include <vector>

// Generalizes the Python pipeline's checks{} dict + hard `assert`s into a
// non-fatal, inspectable result list: every geometric consistency claim
// ("these two seams are the same length", "these two panels tile exactly")
// becomes one CheckResult instead of a crash, so the GUI can show a
// pass/fail panel and exporting can still proceed with a documented flag.

namespace pf {

struct CheckResult {
    std::string name;
    std::string detail;
    bool pass = true;
};

class Checks {
public:
    void add(const std::string& name, const std::string& detail, bool pass);

    // Records pass = |a - b| <= tol, detail shows both values and the unit.
    void expectNear(const std::string& name, double a, double b, double tol, const std::string& unit = "cm");

    // Records pass = symmetricDifferenceArea(a, b) <= tolAreaCm2.
    void expectOutlineMatch(const std::string& name, const Ring& a, const Ring& b, double tolAreaCm2 = 0.025);

    const std::vector<CheckResult>& results() const { return results_; }
    bool allPass() const;

private:
    std::vector<CheckResult> results_;
};

} // namespace pf
