// ---------------------------------------------------------------------------
// Implementation of the scene-placement text format (see placement_io.hpp).
// Floats are written with full round-trip precision (max_digits10) so loading
// reproduces the exact bit pattern for any representable value; std::to_string
// would truncate to 6 fractional digits and lose data.
// ---------------------------------------------------------------------------
#include "placement_io.hpp"

#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace wf {

namespace {

constexpr const char* kMagic = "WFSCENE 1";

// Format a float with enough precision to round-trip exactly.
std::string fmtFloat(float v) {
    std::ostringstream os;
    os.precision(std::numeric_limits<float>::max_digits10);
    os << v;
    return os.str();
}

// Strip trailing CR/LF/space/tab so records survive Windows line endings and
// stray whitespace.
std::string rtrim(const std::string& s) {
    size_t end = s.size();
    while (end > 0) {
        char c = s[end - 1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') --end;
        else break;
    }
    return s.substr(0, end);
}

} // namespace

std::string saveScene(const std::vector<ScenePlacement>& placements) {
    std::ostringstream os;
    os << kMagic << '\n';
    for (const auto& p : placements) {
        os << (p.isWmo ? "WMO" : "M2") << '\t'
           << p.model                  << '\t'
           << fmtFloat(p.pos.x)        << '\t'
           << fmtFloat(p.pos.y)        << '\t'
           << fmtFloat(p.pos.z)        << '\t'
           << fmtFloat(p.rotZ)         << '\t'
           << fmtFloat(p.scale)        << '\n';
    }
    return os.str();
}

std::vector<ScenePlacement> loadScene(const std::string& text) {
    std::vector<ScenePlacement> out;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        std::string line = rtrim(raw);
        if (line.empty())            continue;   // blank line
        if (line == kMagic)          continue;   // header

        // Split on tabs.
        std::vector<std::string> fields;
        std::istringstream ls(line);
        std::string field;
        while (std::getline(ls, field, '\t')) fields.push_back(field);

        // kind, model, x, y, z, rotZ, scale -> 7 fields required.
        if (fields.size() < 7) continue;

        ScenePlacement p;
        p.isWmo = (fields[0] == "WMO");
        p.model = fields[1];
        try {
            p.pos.x = std::stof(fields[2]);
            p.pos.y = std::stof(fields[3]);
            p.pos.z = std::stof(fields[4]);
            p.rotZ  = std::stof(fields[5]);
            p.scale = std::stof(fields[6]);
        } catch (...) {
            continue;   // non-numeric field -> malformed, skip
        }
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace wf
