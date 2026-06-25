#include "test.hpp"
#include "placement_io.hpp"

#include <string>
#include <vector>

using namespace wf;

void test_placement_io() {
    std::printf("[placement_io]\n");

    // --- round-trip ----------------------------------------------------------
    std::vector<ScenePlacement> in;
    in.push_back({"World/Tree.m2",        {12.5f, -3.25f, 100.0f},   45.0f, 1.0f,  false});
    in.push_back({"Buildings/Keep.wmo",   {-200.75f, 64.0f, -8.5f}, 270.0f, 2.5f,  true});
    in.push_back({"World/Rock.m2",        {0.0f, 0.0f, 0.0f},        0.0f,  0.125f, false});

    std::string text = saveScene(in);
    std::vector<ScenePlacement> out = loadScene(text);

    CHECK(out.size() == in.size());
    for (size_t i = 0; i < out.size() && i < in.size(); ++i) {
        CHECK(out[i].model == in[i].model);
        CHECK(out[i].isWmo == in[i].isWmo);
        CHECK_APPROX(out[i].pos.x, in[i].pos.x);
        CHECK_APPROX(out[i].pos.y, in[i].pos.y);
        CHECK_APPROX(out[i].pos.z, in[i].pos.z);
        CHECK_APPROX(out[i].rotZ,  in[i].rotZ);
        CHECK_APPROX(out[i].scale, in[i].scale);
    }

    // Round-trip must be exact for representable floats (not just approx).
    CHECK(out[1].pos.x == in[1].pos.x);
    CHECK(out[1].rotZ  == in[1].rotZ);
    CHECK(out[2].scale == in[2].scale);

    // --- garbage / empty input return empty without crashing -----------------
    CHECK(loadScene("").empty());
    CHECK(loadScene("garbage\nlines").empty());

    // Header alone yields no placements; blank lines are tolerated.
    CHECK(loadScene("WFSCENE 1\n").empty());
    CHECK(loadScene("WFSCENE 1\n\n\n").empty());
}
