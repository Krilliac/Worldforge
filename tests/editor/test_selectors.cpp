#include "test.hpp"
#include "imgui.h"

#include "editor/Selectors.hpp"
#include "dbc_defs.hpp"
#include "dbc_index.hpp"
#include "wow_files.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace wf;
using namespace wf::editor;

namespace {

constexpr Option kKinds[] = {
    {0, "None"},
    {1, "Creature"},
    {2, "GameObject"},
};

constexpr FlagBit kAreaFlags[] = {
    {0x1, "Indoor"},
    {0x2, "Outdoor"},
    {0x8, "Sanctuary"},
};

std::vector<uint32_t> ids(const std::vector<std::pair<uint32_t, std::string>>& m) {
    std::vector<uint32_t> out;
    for (const auto& [id, name] : m) { (void)name; out.push_back(id); }
    return out;
}

} // namespace

void test_selectors() {
    std::printf("[editor.selectors]\n");

    // --- enumLabel (the EnumCombo pure core) ---------------------------------
    CHECK(std::string(enumLabel(1, kKinds, 3)) == "Creature");
    CHECK(std::string(enumLabel(0, kKinds, 3)) == "None");
    CHECK(enumLabel(9, kKinds, 3) == nullptr);             // unlisted value

    // --- flagsSummary golden strings -----------------------------------------
    CHECK(flagsSummary(0x0, kAreaFlags) == "None");
    CHECK(flagsSummary(0x1, kAreaFlags) == "Indoor");
    CHECK(flagsSummary(0x9, kAreaFlags) == "Indoor|Sanctuary");
    CHECK(flagsSummary(0xB, kAreaFlags) == "Indoor|Outdoor|Sanctuary");
    CHECK(flagsSummary(0x12, kAreaFlags) == "Outdoor|0x10");   // unknown bit kept
    CHECK(flagsSummary(0x10, kAreaFlags) == "0x10");
    CHECK(flagsSummary(0x5, kAreaFlags, 0) == "0x5");          // no named bits

    // --- rankMatches: exact id first, stable substring order, cap -------------
    {
        SearchSource src = makeSearchSource({
            {5,   "Murloc"},
            {10,  "Kobold"},
            {50,  "Sea Murloc"},
            {105, "Rock"},
        });
        CHECK(src.count() == 4);
        CHECK(src.at(2).first == 50 && src.at(2).second == "Sea Murloc");

        // Case-insensitive substring, stable source order.
        CHECK(ids(rankMatches(src, "murloc", 8)) == std::vector<uint32_t>({5, 50}));
        CHECK(ids(rankMatches(src, "MURLOC", 8)) == std::vector<uint32_t>({5, 50}));
        // Numeric needle: the exact-id hit ranks first (no name contains "5").
        CHECK(ids(rankMatches(src, "5", 8)) == std::vector<uint32_t>({5}));
        CHECK(ids(rankMatches(src, "10", 8)) == std::vector<uint32_t>({10}));
        // Empty needle lists everything, in order.
        CHECK(ids(rankMatches(src, "", 8)) == std::vector<uint32_t>({5, 10, 50, 105}));
        // maxResults respected.
        CHECK(ids(rankMatches(src, "o", 2)) == std::vector<uint32_t>({5, 10}));
        CHECK(rankMatches(src, "o", 0).empty());
        // No match at all.
        CHECK(rankMatches(src, "zzz", 8).empty());
    }
    {
        // Exact-id hit precedes name-substring hits and is never duplicated.
        SearchSource src = makeSearchSource({
            {70, "7th Legion"},
            {7,  "Seven"},
        });
        CHECK(ids(rankMatches(src, "7", 8)) == std::vector<uint32_t>({7, 70}));
        // A broken source (no callbacks) yields nothing rather than crashing.
        CHECK(rankMatches(SearchSource{}, "7", 8).empty());
    }

    // --- adapters over MapIndex / AreaIndex (fixtures per test_dbc_index) -----
    {
        DbcBuilder b(5);   // Map.dbc: id@0, directory@1, instanceType@2, name@4
        uint32_t dirA = b.addString("Azeroth");
        uint32_t namA = b.addString("Eastern Kingdoms");
        uint32_t dirK = b.addString("Kalimdor");
        uint32_t namK = b.addString("Kalimdor");
        b.addRecord({0, dirA, 0, 0, namA});
        b.addRecord({1, dirK, 0, 0, namK});
        const Dbc mapDbc = Dbc::parse(b.build());
        const MapIndex mi = MapIndex::build(mapDbc);

        SearchSource src = mapSearchSource(mi, {0, 1, 999});
        CHECK(src.count() == 3);
        CHECK(src.at(0).second == "Eastern Kingdoms");
        CHECK(src.at(2).second.empty());                    // unknown id: no name
        CHECK(ids(rankMatches(src, "kalim", 8)) == std::vector<uint32_t>({1}));
        CHECK(ids(rankMatches(src, "999", 8)) == std::vector<uint32_t>({999}));
    }
    {
        DbcBuilder b(12);  // AreaTable.dbc: id@0, parent@2, name@11
        std::vector<uint32_t> elwynn(12, 0);
        elwynn[0] = 12; elwynn[11] = b.addString("Elwynn Forest");
        b.addRecord(elwynn);
        std::vector<uint32_t> goldshire(12, 0);
        goldshire[0] = 87; goldshire[2] = 12;
        goldshire[11] = b.addString("Goldshire");
        b.addRecord(goldshire);
        const Dbc areaDbc = Dbc::parse(b.build());
        const AreaIndex ai = AreaIndex::build(areaDbc);

        SearchSource src = areaSearchSource(ai, {12, 87});
        CHECK(src.count() == 2);
        CHECK(src.at(1).second == "Goldshire, Elwynn Forest");   // fullName chain
        CHECK(ids(rankMatches(src, "goldshire", 8)) == std::vector<uint32_t>({87}));
        CHECK(ids(rankMatches(src, "elwynn", 8)) == std::vector<uint32_t>({12, 87}));
    }

    // --- widgets under the headless ImGui frame loop --------------------------
    {
        int32_t  kind = 1;
        uint32_t mask = 0x9;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Always);
        ImGui::Begin("selectors");
        const bool comboChanged = EnumCombo("Kind", &kind, kKinds);
        const bool flagsChanged = FlagsPopup("Flags", &mask, kAreaFlags);
        ImGui::End();
        ImGui::Render();
        CHECK(ImGui::GetDrawData() != nullptr && ImGui::GetDrawData()->Valid);
        CHECK(!comboChanged && !flagsChanged);              // closed popups: no edits
        CHECK(kind == 1 && mask == 0x9);

        // State transition: the collapsed button label follows the mask.
        mask |= 0x2;
        ImGui::NewFrame();
        ImGui::Begin("selectors");
        FlagsPopup("Flags", &mask, kAreaFlags);
        int32_t odd = 9;                                    // unlisted enum value
        EnumCombo("Kind", &odd, kKinds);
        ImGui::End();
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
        CHECK(flagsSummary(mask, kAreaFlags) == "Indoor|Outdoor|Sanctuary");
    }

    // --- SearchPicker: modal draws; picks drain edge-triggered ------------------
    {
        SearchSource src = makeSearchSource({{5, "Murloc"}, {10, "Kobold"}});
        SearchPicker picker;
        uint32_t id = 0;
        CHECK(!picker.takePicked(id));

        picker.open();
        ImGui::NewFrame();
        const bool picked = picker.draw("Pick entry", src);
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
        CHECK(!picked);                                     // nothing clicked headless

        picker.pick(10);                                    // programmatic pick
        CHECK(picker.picked() == 10);
        CHECK(picker.takePicked(id) && id == 10);
        CHECK(!picker.takePicked(id));                      // drained
    }
}
