#include "waypoint_path.hpp"

#include <algorithm>
#include <limits>

namespace wf {

namespace {
// Squared distance from p to segment ab (squared avoids the sqrt and keeps
// comparisons exact for the argmin below).
float pointSegmentDistSq(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float len2 = dot(ab, ab);
    if (len2 <= 0.0f) {                     // degenerate segment: distance to a
        const Vec3 d = p - a;
        return dot(d, d);
    }
    float t = dot(p - a, ab) / len2;
    t = std::max(0.0f, std::min(1.0f, t));
    const Vec3 closest = a + ab * t;
    const Vec3 d = p - closest;
    return dot(d, d);
}
} // namespace

void appendNode(WaypointPath& path, WaypointNode node) {
    path.nodes.push_back(std::move(node));
}

size_t insertNodeNearestSegment(WaypointPath& path, const Vec3& p) {
    WaypointNode node;
    node.pos = p;
    if (path.nodes.size() < 2) {            // no segments yet: just append
        path.nodes.push_back(std::move(node));
        return path.nodes.size() - 1;
    }
    // Argmin over the open segments i -> i+1. A closed-loop-intent path still
    // stores open nodes, so the wrap-around leg is never a candidate.
    size_t best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (size_t i = 0; i + 1 < path.nodes.size(); ++i) {
        const float d = pointSegmentDistSq(p, path.nodes[i].pos, path.nodes[i + 1].pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    // Insert AFTER the segment's first endpoint: the new node becomes best+1.
    path.nodes.insert(path.nodes.begin() + (best + 1), std::move(node));
    return best + 1;
}

void moveNode(WaypointPath& path, size_t i, const Vec3& pos) {
    if (i < path.nodes.size()) path.nodes[i].pos = pos;
}

void removeNode(WaypointPath& path, size_t i) {
    if (i < path.nodes.size()) path.nodes.erase(path.nodes.begin() + i);
}

void reverse(WaypointPath& path) {
    std::reverse(path.nodes.begin(), path.nodes.end());
}

std::vector<LosResult> validateSegments(const WaypointPath& path,
                                        const std::function<bool(Vec3, Vec3)>& losFn) {
    std::vector<LosResult> out;
    if (path.nodes.size() < 2 || !losFn) return out;
    out.reserve(path.nodes.size() - 1);
    for (size_t i = 0; i + 1 < path.nodes.size(); ++i) {
        LosResult r;
        r.fromIdx = static_cast<int>(i);
        r.blocked = !losFn(path.nodes[i].pos, path.nodes[i + 1].pos);
        out.push_back(r);
    }
    return out;
}

} // namespace wf
