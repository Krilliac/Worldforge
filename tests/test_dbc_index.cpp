#include "test.hpp"
#include "dbc_index.hpp"
#include "dbc_defs.hpp"
#include "wow_files.hpp"

#include <string>
#include <vector>

using namespace wf;

void test_dbc_index() {
    std::printf("[dbc_index]\n");

    // --- MapIndex ----------------------------------------------------------
    // Map.dbc: id @0, directory @1 (string), instanceType @2, name @4 (string).
    {
        DbcBuilder b(5);
        uint32_t dirA = b.addString("Azeroth");
        uint32_t namA = b.addString("Eastern Kingdoms");
        uint32_t dirK = b.addString("Kalimdor");
        uint32_t namK = b.addString("Kalimdor");
        b.addRecord({0, dirA, 0, 0, namA});      // id 0
        b.addRecord({1, dirK, 0, 0, namK});      // id 1
        Dbc dbc = Dbc::parse(b.build());

        MapIndex mi = MapIndex::build(dbc);
        CHECK(mi.size() == 2);
        const MapEntry* m0 = mi.find(0);
        CHECK(m0 != nullptr);
        CHECK(m0->directory == "Azeroth");
        CHECK(m0->name == "Eastern Kingdoms");
        const MapEntry* m1 = mi.find(1);
        CHECK(m1 != nullptr && m1->directory == "Kalimdor");
        CHECK(mi.find(999) == nullptr);          // miss
    }

    // --- AreaIndex + fullName parent chain ---------------------------------
    // AreaTable.dbc: id @0, mapId @1, parentAreaId @2, ..., name @11 (string).
    {
        DbcBuilder b(12);
        // Elwynn Forest (id 12), root: parentAreaId 0.
        auto elwynn = std::vector<uint32_t>(12, 0);
        elwynn[0] = 12; elwynn[1] = 0; elwynn[2] = 0;
        elwynn[11] = b.addString("Elwynn Forest");
        b.addRecord(elwynn);
        // Goldshire (id 87), child of Elwynn (parent 12).
        auto goldshire = std::vector<uint32_t>(12, 0);
        goldshire[0] = 87; goldshire[1] = 0; goldshire[2] = 12;
        goldshire[11] = b.addString("Goldshire");
        b.addRecord(goldshire);
        // Orphan (id 50) whose parent (id 999) does not exist.
        auto orphan = std::vector<uint32_t>(12, 0);
        orphan[0] = 50; orphan[2] = 999;
        orphan[11] = b.addString("Orphan Zone");
        b.addRecord(orphan);
        Dbc dbc = Dbc::parse(b.build());

        AreaIndex ai = AreaIndex::build(dbc);
        CHECK(ai.size() == 3);

        const AreaEntry* g = ai.find(87);
        CHECK(g != nullptr);
        CHECK(g->name == "Goldshire");
        CHECK(g->parentAreaId == 12);
        CHECK(ai.find(123456) == nullptr);       // miss

        // Child-first join up the chain.
        CHECK(ai.fullName(87) == "Goldshire, Elwynn Forest");
        // Root area has no parent: just its own name.
        CHECK(ai.fullName(12) == "Elwynn Forest");
        // Custom separator.
        CHECK(ai.fullName(87, " > ") == "Goldshire > Elwynn Forest");
        // Missing parent: walk stops after the child.
        CHECK(ai.fullName(50) == "Orphan Zone");
        // Absent id yields empty string.
        CHECK(ai.fullName(7777).empty());
    }

    // --- AreaIndex fullName cycle guard ------------------------------------
    // Two areas pointing at each other must terminate (each visited once).
    {
        DbcBuilder b(12);
        auto a = std::vector<uint32_t>(12, 0);
        a[0] = 1; a[2] = 2; a[11] = b.addString("A");
        b.addRecord(a);
        auto c = std::vector<uint32_t>(12, 0);
        c[0] = 2; c[2] = 1; c[11] = b.addString("B");
        b.addRecord(c);
        Dbc dbc = Dbc::parse(b.build());

        AreaIndex ai = AreaIndex::build(dbc);
        // Starts at 1 -> "A", then 2 -> "B", then back to 1 which is already
        // seen: stops without looping forever or repeating a name.
        CHECK(ai.fullName(1) == "A, B");
    }

    // --- LiquidTypeIndex ---------------------------------------------------
    // LiquidType.dbc: id @0, liquidId @1, type @2, spellId @3.
    {
        DbcBuilder b(4);
        b.addRecord({2, 23, 3, 0});      // water
        b.addRecord({8, 35, 0, 0});      // magma
        Dbc dbc = Dbc::parse(b.build());

        LiquidTypeIndex li = LiquidTypeIndex::build(dbc);
        CHECK(li.size() == 2);
        const LiquidTypeEntry* w = li.find(2);
        CHECK(w != nullptr);
        CHECK(w->liquidId == 23 && w->type == 3);
        const LiquidTypeEntry* mg = li.find(8);
        CHECK(mg != nullptr && mg->liquidId == 35);
        CHECK(li.find(0) == nullptr);    // miss
    }

    // --- Empty DBC edge case -----------------------------------------------
    {
        DbcBuilder b(5);
        Dbc dbc = Dbc::parse(b.build());
        MapIndex mi = MapIndex::build(dbc);
        CHECK(mi.size() == 0);
        CHECK(mi.find(0) == nullptr);
    }
}
