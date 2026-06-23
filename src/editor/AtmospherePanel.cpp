#include "editor/AtmospherePanel.hpp"

#include <string>

#include "imgui.h"

namespace wf::editor {

FxTarget AtmospherePanel::target() const {
    FxTarget t;
    t.scope  = static_cast<FxScope>(scope);
    t.guid   = targetGuid;
    t.zoneId = zoneId;
    return t;
}

WeatherFx AtmospherePanel::weatherOp() const {
    WeatherFx o;
    o.type    = static_cast<WeatherType>(weatherType);
    o.grade   = weatherGrade;
    o.soundId = static_cast<uint32_t>(weatherSound);
    o.instant = weatherInstant;
    o.target  = target();
    o.opId    = opId();
    return o;
}
SoundFx AtmospherePanel::soundOp() const {
    SoundFx o; o.soundId = static_cast<uint32_t>(soundId); o.music = soundIsMusic;
    o.target = target(); o.opId = opId(); return o;
}
CinematicFx AtmospherePanel::cinematicOp() const {
    CinematicFx o; o.cinematicId = static_cast<uint32_t>(cinematicId);
    o.target = target(); o.opId = opId(); return o;
}
WorldStateFx AtmospherePanel::worldStateOp() const {
    WorldStateFx o; o.field = static_cast<uint32_t>(worldStateField);
    o.value = static_cast<uint32_t>(worldStateValue);
    o.target = target(); o.opId = opId(); return o;
}
ScreenMsgFx AtmospherePanel::screenMsgOp() const {
    ScreenMsgFx o; o.kind = static_cast<uint8_t>(screenMsgKind);
    o.type = static_cast<uint32_t>(screenMsgType); o.text = screenMsgText;
    o.target = target(); o.opId = opId(); return o;
}
OverrideLight AtmospherePanel::lightOp() const {
    OverrideLight o;
    o.overrideLightId = static_cast<uint32_t>(lightOverride);
    o.fadeInMs = static_cast<uint32_t>(lightFadeMs);
    o.scope = static_cast<FxScope>(scope);
    o.targetGuid = targetGuid; o.zoneId = zoneId; o.opId = opId();
    return o;
}

void AtmospherePanel::draw(std::vector<std::vector<uint8_t>>& out) {
    ImGui::Begin("Atmosphere");

    // --- shared scope/target ---
    const char* scopes[] = { "Self", "Target", "Zone", "Server" };
    ImGui::Combo("Scope", &scope, scopes, IM_ARRAYSIZE(scopes));
    if (scope == 1) {
        ImGui::InputScalar("Target GUID", ImGuiDataType_U64, &targetGuid,
                           nullptr, nullptr, "%llX");
    } else if (scope == 2) {
        ImGui::InputInt("Zone ID", reinterpret_cast<int*>(&zoneId));
    }
    ImGui::Separator();

    auto apply = [&](std::vector<uint8_t> pkt) { out.push_back(std::move(pkt)); ++nextOpId; };

    if (ImGui::CollapsingHeader("Weather", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* types[] = { "Fine", "Rain", "Snow", "Sandstorm" };
        ImGui::Combo("Type", &weatherType, types, IM_ARRAYSIZE(types));
        ImGui::SliderFloat("Grade", &weatherGrade, 0.0f, 1.0f);
        ImGui::InputInt("Sound", &weatherSound);
        ImGui::Checkbox("Instant", &weatherInstant);
        if (ImGui::Button("Apply Weather")) apply(encode(weatherOp()));
    }
    if (ImGui::CollapsingHeader("Sound / Music")) {
        ImGui::InputInt("Sound ID", &soundId);
        ImGui::Checkbox("As music", &soundIsMusic);
        if (ImGui::Button("Play Sound")) apply(encode(soundOp()));
    }
    if (ImGui::CollapsingHeader("Cinematic")) {
        ImGui::InputInt("Cinematic ID", &cinematicId);
        if (ImGui::Button("Trigger Cinematic")) apply(encode(cinematicOp()));
    }
    if (ImGui::CollapsingHeader("World State")) {
        ImGui::InputInt("Field", &worldStateField);
        ImGui::InputInt("Value", &worldStateValue);
        if (ImGui::Button("Set World State")) apply(encode(worldStateOp()));
    }
    if (ImGui::CollapsingHeader("Screen Message")) {
        const char* kinds[] = { "Area Trigger", "Notification", "Server Msg" };
        ImGui::Combo("Kind", &screenMsgKind, kinds, IM_ARRAYSIZE(kinds));
        if (screenMsgKind == 2) ImGui::InputInt("Msg Type", &screenMsgType);
        ImGui::InputText("Text", screenMsgText, IM_ARRAYSIZE(screenMsgText));
        if (ImGui::Button("Send Message")) apply(encode(screenMsgOp()));
    }
    if (ImGui::CollapsingHeader("Override Light")) {
        ImGui::InputInt("Current Light", &lightCurrent);
        ImGui::InputInt("Override Light", &lightOverride);
        ImGui::InputInt("Fade (ms)", &lightFadeMs);
        if (ImGui::Button("Apply Light")) apply(encode(lightOp()));
    }

    ImGui::End();
}

} // namespace wf::editor
