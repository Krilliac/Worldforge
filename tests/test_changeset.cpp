#include "test.hpp"
#include "changeset.hpp"
#include "editor_bridge.hpp"   // reloadCommandFor

#include <stdexcept>
#include <string>

using namespace wf;

void test_changeset() {
    std::printf("[changeset]\n");

    // --- sqlQuote: wraps, doubles embedded quotes, rejects NUL ---------------
    CHECK(sqlQuote("Mangy Wolf") == "'Mangy Wolf'");
    CHECK(sqlQuote("Slim's Friend") == "'Slim''s Friend'");
    CHECK(sqlQuote("''") == "''''''");            // every quote doubled
    CHECK(sqlQuote("") == "''");
    // Backslash is MySQL's escape character: every one is doubled, so paths
    // survive and a trailing backslash cannot escape the closing quote.
    CHECK(sqlQuote("World\\Maps\\Azeroth") == "'World\\\\Maps\\\\Azeroth'");
    CHECK(sqlQuote("trail\\") == "'trail\\\\'");
    CHECK(sqlQuote("\\'") == "'\\\\'''");         // backslash + quote, both escaped
    bool threw = false;
    try { sqlQuote(std::string_view("bad\0nul", 7)); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // --- diffUpdate: ONLY the changed columns make it into the statement -----
    Row original = {
        { "minlevel", "3" },
        { "maxlevel", "4" },
        { "name",     sqlQuote("Young Wolf") },
    };
    Row current = original;
    current[0].second = "5";                       // minlevel 3 -> 5
    CHECK(diffUpdate("creature_template", "entry", "299", original, current) ==
          "UPDATE creature_template SET minlevel = 5 WHERE entry = 299;");

    // two changed columns keep the Row's order
    current[2].second = sqlQuote("Slim's Wolf");   // quoted value survives intact
    CHECK(diffUpdate("creature_template", "entry", "299", original, current) ==
          "UPDATE creature_template SET minlevel = 5, name = 'Slim''s Wolf' "
          "WHERE entry = 299;");

    // nothing changed -> no statement at all
    CHECK(diffUpdate("creature_template", "entry", "299", original, original).empty());

    // a column present only in `current` counts as changed
    Row grown = original;
    grown.push_back({ "faction", "35" });
    CHECK(diffUpdate("creature_template", "entry", "299", original, grown) ==
          "UPDATE creature_template SET faction = 35 WHERE entry = 299;");

    // --- deleteInsert: atomic DELETE + one bulk INSERT (golden, 3 rows) ------
    std::vector<Row> rows = {
        { { "entry", "299" }, { "item", "2589" }, { "ChanceOrQuestChance", "40" } },
        { { "entry", "299" }, { "item", "117"  }, { "ChanceOrQuestChance", "55" } },
        { { "entry", "299" }, { "item", "774"  }, { "ChanceOrQuestChance", "-100" } },
    };
    CHECK(deleteInsert("creature_loot_template", "entry", "299", rows) ==
          "DELETE FROM creature_loot_template WHERE entry = 299;\n"
          "INSERT INTO creature_loot_template (entry, item, ChanceOrQuestChance) VALUES\n"
          "(299, 2589, 40),\n"
          "(299, 117, 55),\n"
          "(299, 774, -100);");

    // no rows: just the DELETE (remove the whole entity)
    CHECK(deleteInsert("creature_loot_template", "entry", "299", {}) ==
          "DELETE FROM creature_loot_template WHERE entry = 299;");

    // --- reloadCommandFor: known / no-reload / unknown tables ----------------
    CHECK(std::string(reloadCommandFor("creature_loot_template")) ==
          ".reload creature_loot_template");
    CHECK(std::string(reloadCommandFor("npc_vendor")) == ".reload npc_vendor");
    CHECK(reloadCommandFor("creature") == nullptr);      // spawn row: next grid load
    CHECK(reloadCommandFor("gameobject") == nullptr);    // spawn row: next grid load
    CHECK(reloadCommandFor("no_such_table") == nullptr);

    // --- Changeset: touched tables + deduped reload commands -----------------
    Changeset cs("wolf tuning");
    CHECK(cs.empty());
    cs.add("UPDATE creature_template SET minlevel = 5 WHERE entry = 299;",
           "raise wolf level", "creature_template");
    cs.add("DELETE FROM creature WHERE guid = 50001;",
           "remove stray spawn", "creature");
    cs.add("UPDATE creature_template SET maxlevel = 6 WHERE entry = 299;",
           "", "creature_template");
    CHECK(!cs.empty());
    CHECK(cs.name() == "wolf tuning");
    CHECK(cs.entries().size() == 3);

    std::set<std::string> tables = cs.touchedTables();
    CHECK(tables.size() == 2);
    CHECK(tables.count("creature_template") == 1);
    CHECK(tables.count("creature") == 1);

    // creature_template touched twice -> ONE reload; creature needs none.
    std::vector<std::string> reloads = cs.reloadCommands();
    CHECK(reloads.size() == 1);
    CHECK(reloads[0] == ".reload creature_template");

    // --- serialize: golden script with a pinned timestamp --------------------
    CHECK(cs.serialize("2026-07-05 00:00:00") ==
          "-- Changeset: wolf tuning\n"
          "-- Generated: 2026-07-05 00:00:00\n"
          "-- raise wolf level\n"
          "UPDATE creature_template SET minlevel = 5 WHERE entry = 299;\n"
          "-- remove stray spawn\n"
          "DELETE FROM creature WHERE guid = 50001;\n"
          "UPDATE creature_template SET maxlevel = 6 WHERE entry = 299;\n"
          "-- Reload to apply live:\n"
          "-- .reload creature_template\n");

    // reload order follows the (sorted) touched-table order when several apply
    Changeset multi("loot + quests");
    multi.add("UPDATE quest_template SET RewXP = 100 WHERE entry = 33;",
              "xp bump", "quest_template");
    multi.add("DELETE FROM creature_loot_template WHERE entry = 299;",
              "clear loot", "creature_loot_template");
    multi.add("UPDATE quest_template SET RewMoney = 5 WHERE entry = 33;",
              "coin bump", "quest_template");
    std::vector<std::string> mr = multi.reloadCommands();
    CHECK(mr.size() == 2);
    CHECK(mr[0] == ".reload creature_loot_template");
    CHECK(mr[1] == ".reload quest_template");

    // an empty changeset still serializes to a valid (header-only) script
    Changeset none("empty");
    CHECK(none.serialize("2026-07-05 00:00:00") ==
          "-- Changeset: empty\n"
          "-- Generated: 2026-07-05 00:00:00\n");
}
