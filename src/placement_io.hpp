#pragma once
// ---------------------------------------------------------------------------
// Scene placement persistence: save/load the editor's list of object
// placements (M2 doodads and WMO buildings) to/from a stable line-based text
// format. This is the storage layer behind the editor's "Save/Load Scene"
// commands -- deliberately GPU- and ImGui-independent so it is fully
// unit-testable headless. The format is a one-line header ("WFSCENE 1")
// followed by one tab-separated record per placement, which keeps it diffable
// and trivially round-trippable.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "math.hpp"

namespace wf {

// A single placed object in the scene. `model` is the archived path (.m2 or
// .wmo); `pos` is world space; `rotZ` is the yaw in degrees; `scale` is a
// uniform factor. `isWmo` selects the building vs. doodad code paths.
struct ScenePlacement {
    std::string model;          // archived path, .m2 or .wmo
    Vec3        pos;
    float       rotZ  = 0.0f;
    float       scale = 1.0f;
    bool        isWmo = false;
};

// Serialise to a stable line-based text format (one placement per line).
// Begins with the header line "WFSCENE 1". Each record is
// "<M2|WMO>\t<model>\t<x>\t<y>\t<z>\t<rotZ>\t<scale>". Round-trips exactly for
// representable floats.
std::string saveScene(const std::vector<ScenePlacement>& placements);

// Parse the text produced by saveScene. Tolerant of blank lines / trailing
// whitespace; ignores malformed lines (too few fields). Returns the placements
// (empty on garbage / empty input).
std::vector<ScenePlacement> loadScene(const std::string& text);

} // namespace wf
