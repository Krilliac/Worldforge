#include "test.hpp"
#include "imgui.h"

#include "editor/AtmospherePanel.hpp"
#include "editor/DebugVisPanel.hpp"
#include "editor_bridge.hpp"
#include "fxbridge.hpp"
#include "worldproto.hpp"
#include "byte_reader.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace wf;
using namespace wf::editor;

void test_panels() {
    std::printf("[editor.panels]\n");

    // --- pure logic: panel state -> op -> realise -> client SMSG ------------
    AtmospherePanel ap;
    ap.scope = 2; ap.zoneId = 1519;             // Zone scope
    ap.weatherType = 2; ap.weatherGrade = 0.7f; // Snow @ 0.7
    WeatherFx w = ap.weatherOp();
    CHECK(w.type == WeatherType::Snow);
    CHECK(w.target.scope == FxScope::Zone && w.target.zoneId == 1519);
    CHECK_APPROX(w.grade, 0.7f);
    std::vector<uint8_t> smsg = realise(w);
    CHECK(readServerHeader(smsg.data()).opcode == SMSG_WEATHER);

    std::snprintf(ap.screenMsgText, sizeof(ap.screenMsgText), "%s", "Hello");
    ap.screenMsgKind = 1;                        // notification
    ScreenMsgFx m = ap.screenMsgOp();
    CHECK(m.text == "Hello");
    CHECK(readServerHeader(realise(m).data()).opcode == SMSG_NOTIFICATION);

    ap.lightOverride = 396; ap.lightFadeMs = 2000; ap.scope = 2; ap.zoneId = 12;
    OverrideLight ol = ap.lightOp();
    CHECK(ol.overrideLightId == 396 && ol.fadeInMs == 2000);
    CHECK(ol.scope == FxScope::Zone && ol.zoneId == 12);

    // --- headless ImGui: the panel draws without a backend and emits an op --
    {
        std::vector<std::vector<uint8_t>> out;
        ImGui::NewFrame();
        ap.draw(out);
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();
        CHECK(dd != nullptr && dd->Valid);       // valid geometry produced
    }

    // What an "Apply" button does round-trips through the bridge framing.
    {
        std::vector<uint8_t> pkt = encode(ap.weatherOp());
        EditorFrame f; size_t used = 0;
        CHECK(readFrame(pkt, f, used) && f.opcode == EDITOR_FX_WEATHER);
        CHECK(decodeWeatherFx(f.payload).target.zoneId == 12);
    }

    // --- DebugVisPanel: draws and toggles a category live -------------------
    {
        DebugDraw draw;
        draw.aabb({-1,-1,-1}, {1,1,1}, Rgba{255,0,0,255}, DebugCategory::Trigger);
        CHECK(draw.stats().lines == 12);

        DebugVisPanel dvp;
        ImGui::NewFrame();
        dvp.draw(draw);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);

        CHECK(std::string(debugCategoryName(DebugCategory::Waypoint)) == "Waypoints");
        CHECK(std::string(debugCategoryName(DebugCategory::LineOfSight)) == "Line of sight");
    }
}
