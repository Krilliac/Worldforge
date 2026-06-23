#pragma once
// ---------------------------------------------------------------------------
// AtmospherePanel: the editor's live-world FX surface as a Dear ImGui panel.
// Weather / sound / cinematic / world-state / screen message / override light,
// each with a scope selector (self/target/zone/server). Applying a section
// appends the framed editor packet (fxbridge encode) to an output queue the
// host sends over the WorldForge<->server bridge; the server realises it into
// the matching clientfx SMSG.
//
// The UI state and the op-building logic are separated from the ImGui draw
// calls so the logic is unit-tested headless (the *Op() getters), while draw()
// is the thin widget layer.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "editor_bridge.hpp"
#include "fxbridge.hpp"

namespace wf::editor {

class AtmospherePanel {
public:
    // Render the panel; append any applied ops as framed editor packets.
    void draw(std::vector<std::vector<uint8_t>>& outPackets);

    // --- pure op builders from the current UI state (no ImGui) ---
    FxTarget      target() const;
    WeatherFx     weatherOp() const;
    SoundFx       soundOp() const;
    CinematicFx   cinematicOp() const;
    WorldStateFx  worldStateOp() const;
    ScreenMsgFx   screenMsgOp() const;
    OverrideLight lightOp() const;

    // --- UI state (the widgets read/write these; tests may set them) ---
    int      scope        = 2;        // FxScope: 0 self,1 target,2 zone,3 server
    uint64_t targetGuid   = 0;        // Self/Target scope
    uint32_t zoneId       = 0;        // Zone scope

    int      weatherType  = 1;        // WeatherType index (Fine/Rain/Snow/Sand)
    float    weatherGrade = 0.5f;
    int      weatherSound = 0;
    bool     weatherInstant = false;

    int      soundId      = 0;
    bool     soundIsMusic = false;

    int      cinematicId  = 0;

    int      worldStateField = 0;
    int      worldStateValue = 0;

    int      screenMsgKind = 0;       // 0 area-trigger,1 notification,2 server
    int      screenMsgType = 0;
    char     screenMsgText[256] = {0};

    int      lightCurrent  = 0;
    int      lightOverride = 0;
    int      lightFadeMs   = 1000;

    uint32_t nextOpId = 1;

private:
    uint32_t opId() const { return nextOpId; }
};

} // namespace wf::editor
