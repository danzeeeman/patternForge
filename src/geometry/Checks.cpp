#include "Checks.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace pf {

void Checks::add(const std::string& name, const std::string& detail, bool pass) {
    results_.push_back({ name, detail, pass });
}

void Checks::expectNear(const std::string& name, double a, double b, double tol, const std::string& unit) {
    bool pass = std::fabs(a - b) <= tol;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << a << " vs " << b << " " << unit
       << " (tol " << tol << ")";
    add(name, ss.str(), pass);
}

void Checks::expectOutlineMatch(const std::string& name, const Ring& a, const Ring& b, double tolAreaCm2) {
    double diff = geo::symmetricDifferenceArea(a, b);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4) << diff << " cm2 difference (tol " << tolAreaCm2 << ")";
    add(name, ss.str(), diff <= tolAreaCm2);
}

bool Checks::allPass() const {
    for (auto& r : results_) if (!r.pass) return false;
    return true;
}

} // namespace pf
