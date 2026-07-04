// ---------------------------------------------------------------------------
// md5translate (baked-minimap index) parser tests. The sample lines are the
// exact real format (dir: sections, "<Map>\map<xx>_<yy>.blp<TAB><md5>.blp"),
// cross-checked against a real 0.5.3 md5translate.txt.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "minimap.hpp"

#include <string>

using namespace wf;

void test_minimap() {
    std::printf("[minimap.md5translate]\n");

    // Real-format sample: two dir sections, padded coords, a duplicate stored
    // hash (adjacent identical tiles share art -- seen in the real file), CRLF.
    const std::string trs =
        "dir: Azeroth\r\n"
        "Azeroth\\map00_00.blp\tc21521f5111a9b4446b52deb1fa9fe9d.blp\r\n"
        "Azeroth\\map25_29.blp\t9d7fe989cb572c88aff61fb79864f26f.blp\r\n"
        "Azeroth\\map25_30.blp\t9d7fe989cb572c88aff61fb79864f26f.blp\r\n"
        "\r\n"
        "dir: Kalimdor\n"
        "Kalimdor\\map32_48.blp\tf310d095b692cd319123b51b821a4773.blp\n";

    MinimapIndex idx = MinimapIndex::parse(trs);
    CHECK(idx.size() == 4);
    CHECK(!idx.empty());

    // Resolve tiles by (map, x, y) with the two-digit padding applied internally.
    CHECK(idx.tile("Azeroth", 0, 0)  == "c21521f5111a9b4446b52deb1fa9fe9d.blp");
    CHECK(idx.tile("Azeroth", 25, 29) == "9d7fe989cb572c88aff61fb79864f26f.blp");
    CHECK(idx.tile("Kalimdor", 32, 48) == "f310d095b692cd319123b51b821a4773.blp");

    // Case-insensitive map name (the client is case-insensitive over MPQ paths).
    CHECK(idx.tile("AZEROTH", 0, 0) == "c21521f5111a9b4446b52deb1fa9fe9d.blp");

    // A tile not in the table resolves to empty (the client draws nothing there).
    CHECK(idx.tile("Azeroth", 1, 1).empty());
    CHECK(idx.tile("Nowhere", 0, 0).empty());

    // Key + stored-path helpers.
    CHECK(MinimapIndex::tileKey("Azeroth", 9, 37) == "azeroth\\map09_37.blp");
    CHECK(MinimapIndex::storedPath("abc.blp") == "textures\\Minimap\\abc.blp");

    // Bare left column ("map<xx>_<yy>.blp" with no path) is prefixed by dir:.
    const std::string bare =
        "dir: Emerald\n"
        "map03_04.blp\tdeadbeef.blp\n";
    MinimapIndex b = MinimapIndex::parse(bare);
    CHECK(b.tile("Emerald", 3, 4) == "deadbeef.blp");

    // Forward-slash separators normalise to the same key as backslash.
    const std::string fs =
        "Azeroth/map05_06.blp\tfeed01.blp\n";
    MinimapIndex f = MinimapIndex::parse(fs);
    CHECK(f.tile("Azeroth", 5, 6) == "feed01.blp");

    // Duplicate logical key: last mapping wins (client reads top-to-bottom).
    const std::string dup =
        "Azeroth\\map00_00.blp\tfirst.blp\n"
        "Azeroth\\map00_00.blp\tsecond.blp\n";
    CHECK(MinimapIndex::parse(dup).tile("Azeroth", 0, 0) == "second.blp");

    // Junk / headerless lines are ignored, not mis-parsed.
    const std::string junk =
        "this line has no separator\n"
        "dir: Azeroth\n"
        "Azeroth\\map00_00.blp\tok.blp\n";
    MinimapIndex j = MinimapIndex::parse(junk);
    CHECK(j.size() == 1 && j.tile("Azeroth", 0, 0) == "ok.blp");
}
