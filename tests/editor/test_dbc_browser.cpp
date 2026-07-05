#include "test.hpp"
#include "imgui.h"

#include "editor/DbcBrowserPanel.hpp"
#include "dbc_defs.hpp"
#include "wow_files.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace wf;
using namespace wf::editor;

namespace {

using CT = ColType;

// Synthetic table exercising every column kind: id, FK, signed, float, string,
// locstring, an all-zero (and unverified) column, and an FK array.
// Field layout: ID@0, MapID@1, Level@2, Scale@3, Name@4, Tag_lang@5..13
// (enUS slot 5), Pad@14, Refs@15..16 -- 17 fields.
constexpr ColumnDef kTestCols[] = {
    {"ID", CT::U32, true},
    {"MapID", CT::U32, false, "Map", "ID"},
    {"Level", CT::I32},
    {"Scale", CT::F32},
    {"Name", CT::Str},
    {"Tag_lang", CT::LocStr},
    {"Pad", CT::U32, false, nullptr, nullptr, 1, false},   // unverified + empty
    {"Refs", CT::U32, false, "Map", "ID", 2},
};
constexpr TableSchema kTestSchema{"TestTable", kTestCols,
                                  sizeof(kTestCols) / sizeof(kTestCols[0])};

constexpr ColumnDef kNoIdCols[] = {
    {"Value", CT::U32},
};
constexpr TableSchema kNoIdSchema{"NoId", kNoIdCols, 1};

uint32_t f2u(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

Dbc makeTestDbc() {
    DbcBuilder b(17);
    // rec 0: id 5000, map 1, level -5, scale 1.5, "Test Name" / "Alpha", refs {1,3}
    std::vector<uint32_t> r0(17, 0);
    r0[0] = 5000; r0[1] = 1; r0[2] = (uint32_t)(int32_t)-5; r0[3] = f2u(1.5f);
    r0[4] = b.addString("Test Name"); r0[5] = b.addString("Alpha");
    r0[15] = 1; r0[16] = 3;
    b.addRecord(r0);
    // rec 1: id 6000, map 0, level 3, scale 2.5, "Other" / "beta thing"
    std::vector<uint32_t> r1(17, 0);
    r1[0] = 6000; r1[2] = 3; r1[3] = f2u(2.5f);
    r1[4] = b.addString("Other"); r1[5] = b.addString("beta thing");
    b.addRecord(r1);
    // rec 2: id 42, map 1, level -5, "test name two" / "Gamma"
    std::vector<uint32_t> r2(17, 0);
    r2[0] = 42; r2[1] = 1; r2[2] = (uint32_t)(int32_t)-5;
    r2[4] = b.addString("test name two"); r2[5] = b.addString("Gamma");
    b.addRecord(r2);
    return Dbc::parse(b.build());
}

QueryTerm qt(std::string col, std::string val, bool rx = false) {
    QueryTerm t;
    t.column  = std::move(col);
    t.value   = std::move(val);
    t.isRegex = rx;
    return t;
}

bool rowsEq(const std::vector<uint32_t>& a, std::vector<uint32_t> b) {
    return a == b;
}

} // namespace

void test_dbc_browser() {
    std::printf("[editor.dbc_browser]\n");

    const Dbc dbc = makeTestDbc();

    // --- parseQuery: binding, quoting, bare terms, dropped empties ----------
    {
        std::vector<QueryTerm> t = parseQuery("id:5000 name:\"test name\" foo");
        CHECK(t.size() == 3);
        CHECK(t[0].column == "id" && t[0].value == "5000");
        CHECK(t[1].column == "name" && t[1].value == "test name");
        CHECK(t[2].column.empty() && t[2].value == "foo");
        CHECK(!t[0].isRegex && !t[2].isRegex);

        CHECK(parseQuery("").empty());
        CHECK(parseQuery("   ").empty());
        CHECK(parseQuery("name:").empty());              // empty value: ignored
        CHECK(parseQuery("name:\"\"").empty());
        CHECK(parseQuery("name: id:7").size() == 1);     // only the bound id term

        std::vector<QueryTerm> q = parseQuery("\"two words\"");
        CHECK(q.size() == 1 && q[0].column.empty() && q[0].value == "two words");

        std::vector<QueryTerm> rx = parseQuery("name:te.t", true);
        CHECK(rx.size() == 1 && rx[0].isRegex);          // regex mode stamped
    }

    // --- schema helpers ------------------------------------------------------
    CHECK(findColumn(kTestSchema, "mapid") == 1);        // case-insensitive
    CHECK(findColumn(kTestSchema, "Tag_LANG") == 5);
    CHECK(findColumn(kTestSchema, "bogus") == -1);
    CHECK(schemaIdColumn(kTestSchema) == 0);
    CHECK(schemaIdColumn(kNoIdSchema) == -1);
    CHECK(nextFreeId(dbc, kTestSchema) == 6001);         // max id 6000 + 1
    CHECK(nextFreeId(dbc, kNoIdSchema) == 0);            // no id column

    // --- filterRows ----------------------------------------------------------
    {
        // Numeric exact match on the id column.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("ID", "5000")}), {0}));
        // Numeric column + non-numeric value: matches nothing.
        CHECK(filterRows(dbc, kTestSchema, {qt("ID", "abc")}).empty());
        // Unknown column name: matches nothing.
        CHECK(filterRows(dbc, kTestSchema, {qt("bogus", "5")}).empty());
        // String substring, case-insensitive.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("Name", "TEST")}), {0, 2}));
        // LocStr compares the enUS slot.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("Tag_lang", "beta")}), {1}));
        // Bare term matches ANY Str/LocStr column.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("", "alpha")}), {0}));
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("", "other")}), {1}));
        // AND semantics across terms.
        CHECK(rowsEq(filterRows(dbc, kTestSchema,
                                {qt("Level", "3"), qt("Name", "other")}), {1}));
        CHECK(filterRows(dbc, kTestSchema,
                         {qt("Level", "3"), qt("Name", "test")}).empty());
        // Signed I32 match.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("Level", "-5")}), {0, 2}));
        // F32 match.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("Scale", "2.5")}), {1}));
        // Array column: any element matches.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("Refs", "3")}), {0}));
        // Hex numeric value (0x1388 == 5000).
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("ID", "0x1388")}), {0}));
        // Regex: icase; an INVALID pattern matches nothing (no throw).
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {qt("Name", "^te.t", true)}), {0, 2}));
        CHECK(filterRows(dbc, kTestSchema, {qt("Name", "((((bad", true)}).empty());
        // No terms: every record, ascending record order.
        CHECK(rowsEq(filterRows(dbc, kTestSchema, {}), {0, 1, 2}));
    }

    // --- sortRows: numeric vs lexicographic, stability, out-of-range ---------
    {
        std::vector<uint32_t> rows = {0, 1, 2};
        sortRows(dbc, kTestSchema, rows, 0, true);         // by ID: 42,5000,6000
        CHECK(rowsEq(rows, {2, 0, 1}));
        sortRows(dbc, kTestSchema, rows, 0, false);
        CHECK(rowsEq(rows, {1, 0, 2}));

        rows = {0, 1, 2};
        sortRows(dbc, kTestSchema, rows, 4, true);         // by Name, icase lexicographic
        CHECK(rowsEq(rows, {1, 0, 2}));                    // Other < test name < test name two

        rows = {0, 1, 2};
        sortRows(dbc, kTestSchema, rows, 2, true);         // by Level: -5,-5,3
        CHECK(rowsEq(rows, {0, 2, 1}));                    // stable: 0 stays before 2

        rows = {0, 1, 2};
        sortRows(dbc, kTestSchema, rows, 99, true);        // out-of-range: no-op
        CHECK(rowsEq(rows, {0, 1, 2}));
    }

    // --- columnIsEmpty --------------------------------------------------------
    CHECK(columnIsEmpty(dbc, kTestSchema, 6));             // Pad: all zero
    CHECK(!columnIsEmpty(dbc, kTestSchema, 0));            // ID
    CHECK(!columnIsEmpty(dbc, kTestSchema, 4));            // Name
    CHECK(!columnIsEmpty(dbc, kTestSchema, 5));            // Tag_lang (enUS strings)
    CHECK(!columnIsEmpty(dbc, kTestSchema, 7));            // Refs (rec 0 nonzero)
    CHECK(columnIsEmpty(dbc, kTestSchema, 99));            // out of range == empty

    // --- panel: debounced query recompute (injectable clock) ------------------
    DbcBrowserPanel panel;
    uint64_t now = 1000;
    panel.setClock([&now] { return now; });
    panel.setTable(&kTestSchema, &dbc);
    CHECK(panel.schema() == &kTestSchema);
    CHECK(rowsEq(panel.visibleRows(), {0, 1, 2}));
    CHECK(panel.columnEmpty(6) && !panel.columnEmpty(0));

    panel.setQueryText("name:test");
    CHECK(!panel.refresh());                               // 0 ms since the edit
    CHECK(rowsEq(panel.visibleRows(), {0, 1, 2}));         // stale until debounced
    now += 100;
    CHECK(!panel.refresh());                               // 100 < 150 ms
    now += 100;
    CHECK(panel.refresh());                                // 200 ms: recomputed
    CHECK(rowsEq(panel.visibleRows(), {0, 2}));
    CHECK(!panel.refresh());                               // clean: no rerun
    panel.setQueryText("name:test");                       // identical text: no-op
    CHECK(!panel.refresh());

    // --- per-column filter feeds a column-bound term ---------------------------
    panel.setQueryText("");
    panel.setColumnFilter(4, "other");
    now += 1000;
    CHECK(panel.refresh());
    CHECK(rowsEq(panel.visibleRows(), {1}));
    // Regex mode applies to the column filters too; invalid regex = nothing.
    panel.regexMode_ = true;
    panel.setColumnFilter(4, "((((bad");
    now += 1000;
    CHECK(panel.refresh());
    CHECK(panel.visibleRows().empty());
    panel.regexMode_ = false;
    panel.setColumnFilter(4, "");                          // "" clears the filter
    now += 1000;
    CHECK(panel.refresh());
    CHECK(rowsEq(panel.visibleRows(), {0, 1, 2}));

    // --- sortBy applies immediately and survives recompute ---------------------
    panel.sortBy(0, true);
    CHECK(rowsEq(panel.visibleRows(), {2, 0, 1}));
    panel.setQueryText("level:-5");
    now += 1000;
    CHECK(panel.refresh());
    CHECK(rowsEq(panel.visibleRows(), {2, 0}));            // filtered AND sorted
    panel.setQueryText("");
    now += 1000;
    panel.refresh();
    panel.sortBy(-1, true);                                // back to record order
    panel.setQueryText("x");                               // force one recompute
    panel.setQueryText("");
    now += 1000;
    panel.refresh();
    CHECK(rowsEq(panel.visibleRows(), {0, 1, 2}));

    // --- cell text: hex toggle, arrays, FK resolution ---------------------------
    panel.setFkResolver([](std::string_view table, uint32_t id) -> std::string {
        if (table == "Map" && id == 1) return "Azeroth";
        return "";
    });
    CHECK(panel.cellText(0, 0) == "5000");
    CHECK(panel.cellText(0, 2) == "-5");                   // I32 renders signed
    CHECK(panel.cellText(0, 3) == "1.5");
    CHECK(panel.cellText(0, 4) == "Test Name");
    CHECK(panel.cellText(0, 5) == "Alpha");                // LocStr enUS slot
    CHECK(panel.cellText(0, 1) == "1 (Azeroth)");          // FK resolved
    CHECK(panel.cellText(1, 1) == "0");                    // resolver knows no 0
    CHECK(panel.cellText(0, 7) == "1 (Azeroth), 3");       // array joined ", "
    panel.hexDisplay_ = true;
    CHECK(panel.cellText(0, 0) == "0x1388");
    CHECK(panel.cellText(0, 3) == "1.5");                  // F32 stays a float
    panel.hexDisplay_ = false;

    // --- FkNav event (edge-triggered, drained by the host) ----------------------
    {
        FkNav nav;
        CHECK(!panel.takeFkNav(nav));
        panel.clickFk(0, 1);
        CHECK(panel.takeFkNav(nav));
        CHECK(nav.table == "Map" && nav.id == 1);
        CHECK(!panel.takeFkNav(nav));                      // drained
        panel.clickFk(0, 4);                               // Name: not an FK column
        CHECK(!panel.takeFkNav(nav));
        panel.clickFk(0, 7);                               // array FK: first element
        CHECK(panel.takeFkNav(nav) && nav.table == "Map" && nav.id == 1);
        panel.clickFk(99, 1);                              // out-of-range rec: no-op
        CHECK(!panel.takeFkNav(nav));
    }

    // --- table request event ------------------------------------------------------
    {
        std::string name;
        CHECK(!panel.takeTableRequest(name));
        panel.requestTable("AreaTable");
        CHECK(panel.takeTableRequest(name) && name == "AreaTable");
        CHECK(!panel.takeTableRequest(name));
    }

    // --- duplicate row: next-free-id allocation + deep copy -------------------------
    CHECK(panel.canDuplicate());
    CHECK(panel.duplicateRow(0) == 6001);                  // max id 6000 + 1
    CHECK(panel.addedRows().size() == 1);
    CHECK(panel.addedRows()[0].fields[0] == 6001);         // id field rewritten
    CHECK(panel.addedCellText(0, 0) == "6001");
    CHECK(panel.addedCellText(0, 4) == "Test Name");       // strings via source rec
    CHECK(panel.addedCellText(0, 5) == "Alpha");
    CHECK(panel.duplicateRow(1) == 6002);                  // counts prior duplicates
    CHECK(panel.duplicateRow(99) == -1);                   // rec out of range
    // The copy is INDEPENDENT: mutating it never touches the source record.
    panel.addedRows()[0].fields[2] = 77;
    CHECK(dbc.getU32(0, 2) == (uint32_t)(int32_t)-5);
    CHECK(panel.cellText(0, 2) == "-5");
    CHECK(panel.addedCellText(0, 2) == "77");

    // --- duplicate is disabled without an isId column -------------------------------
    {
        DbcBuilder b(1);
        b.addRecord({7});
        const Dbc noId = Dbc::parse(b.build());
        DbcBrowserPanel p2;
        p2.setClock([&now] { return now; });
        p2.setTable(&kNoIdSchema, &noId);
        CHECK(!p2.canDuplicate());
        CHECK(p2.duplicateRow(0) == -1);
        CHECK(p2.addedRows().empty());
    }

    // --- zero-row table stays calm ----------------------------------------------------
    {
        DbcBuilder b(17);
        const Dbc empty = Dbc::parse(b.build());
        CHECK(filterRows(empty, kTestSchema, {qt("", "x")}).empty());
        CHECK(nextFreeId(empty, kTestSchema) == 1);
        CHECK(columnIsEmpty(empty, kTestSchema, 0));
        DbcBrowserPanel p3;
        p3.setTable(&kTestSchema, &empty);
        CHECK(p3.visibleRows().empty());
        ImGui::NewFrame();
        p3.draw();
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
    }

    // --- headless draw: loaded grid, hide-empty + hex toggles, and no table -----------
    {
        panel.hideEmpty_ = true;
        panel.hexDisplay_ = true;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_Always);
        panel.draw();
        ImGui::Render();
        CHECK(ImGui::GetDrawData() != nullptr && ImGui::GetDrawData()->Valid);
        panel.hideEmpty_ = false;
        panel.hexDisplay_ = false;

        DbcBrowserPanel detached;
        ImGui::NewFrame();
        detached.draw();                                   // "No table loaded."
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->Valid);
    }

    // --- detach resets everything -------------------------------------------------------
    panel.setTable(nullptr, nullptr);
    CHECK(panel.schema() == nullptr);
    CHECK(panel.visibleRows().empty() && panel.addedRows().empty());
    CHECK(!panel.canDuplicate());
}
