#include "test.hpp"
#include "dbc_defs.hpp"
#include "m2.hpp"          // M2Submesh / selectGeosets (T3.3 geoset composition)
#include "gridmap.hpp"
#include "navmesh.hpp"
#include "vmap.hpp"
#include "mpq.hpp"
#include "wow_files.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace wf;

namespace {
void p8 (std::vector<uint8_t>& b, uint8_t v){ b.push_back(v); }
void p16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void p32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void pf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); p32(b,v); }
void praw(std::vector<uint8_t>& b, const char* s){ for(int i=0;i<4;i++) b.push_back((uint8_t)s[i]); }

// Build a WDBC blob: records is a list of rows, each `fieldCount` uint32 values
// (floats are pre-encoded as their bit pattern). strblock is the string table.
std::vector<uint8_t> makeDbc(uint32_t fieldCount,
                             const std::vector<std::vector<uint32_t>>& records,
                             const std::string& strblock) {
    std::vector<uint8_t> b;
    praw(b, "WDBC");
    p32(b, (uint32_t)records.size());
    p32(b, fieldCount);
    p32(b, fieldCount * 4);                 // recordSize
    p32(b, (uint32_t)strblock.size());      // stringSize
    for (const auto& rec : records)
        for (uint32_t f = 0; f < fieldCount; ++f)
            p32(b, f < rec.size() ? rec[f] : 0u);
    b.insert(b.end(), strblock.begin(), strblock.end());
    return b;
}
uint32_t fbits(float f){ uint32_t v; std::memcpy(&v,&f,4); return v; }
} // namespace

void test_dbc_defs() {
    std::printf("[dbc_defs]\n");

    // string block: offset 0 = "", then "Azeroth" @1, "Elwynn Forest" @9.
    std::string strs;
    strs.push_back('\0');
    size_t offAzeroth = strs.size(); strs += "Azeroth"; strs.push_back('\0');
    size_t offElwynn  = strs.size(); strs += "Elwynn Forest"; strs.push_back('\0');

    // --- Map.dbc: id, dir(str), instanceType, _, name(str) ---
    {
        std::vector<uint32_t> rec(42, 0);
        rec[0] = 0;                                  // id
        rec[1] = (uint32_t)offAzeroth;               // directory
        rec[2] = 0;                                  // instanceType (world)
        rec[4] = (uint32_t)offAzeroth;               // name (enUS)
        Dbc dbc = Dbc::parse(makeDbc(42, { rec }, strs));
        MapEntry e = mapEntry(dbc, 0);
        CHECK(e.id == 0 && e.instanceType == 0);
        CHECK(e.directory == "Azeroth");
        CHECK(e.name == "Azeroth");
    }

    // --- AreaTable.dbc: id, map, parent, bit, flags, ... level@10, name@11 ---
    {
        std::vector<uint32_t> rec(25, 0);
        rec[0]  = 12;                                // id
        rec[1]  = 0;                                 // map
        rec[2]  = 1519;                              // parent area
        rec[3]  = 5;                                 // area bit
        rec[4]  = 0x40;                              // flags
        rec[10] = 1;                                 // exploration level
        rec[11] = (uint32_t)offElwynn;               // name enUS
        Dbc dbc = Dbc::parse(makeDbc(25, { rec }, strs));
        AreaEntry e = areaEntry(dbc, 0);
        CHECK(e.id == 12 && e.parentAreaId == 1519);
        CHECK(e.flags == 0x40 && e.explorationLevel == 1);
        CHECK(e.name == "Elwynn Forest");
    }

    // --- LiquidType.dbc: id, liquidId, type, spell ---
    {
        std::vector<uint32_t> rec = { 5, 23, 3, 0 };  // water
        Dbc dbc = Dbc::parse(makeDbc(4, { rec }, std::string(1, '\0')));
        LiquidTypeEntry e = liquidTypeEntry(dbc, 0);
        CHECK(e.id == 5 && e.liquidId == 23 && e.type == 3);
    }

    // --- Light.dbc: id, map, x,y,z, falloffStart/End, params[5] ---
    // Real vanilla 1.12 Light.dbc has exactly 12 fields => 5 LightParams refs
    // (fields 7..11). Reading 8 params (the old layout) overran fields 12..14 and
    // crashed on the real dbc; this fixture matches the real shape so the field
    // count is a regression guard.
    {
        std::vector<uint32_t> rec(12, 0);
        rec[0] = 1; rec[1] = 0;
        rec[2] = fbits(-9000.0f); rec[3] = fbits(100.0f); rec[4] = fbits(50.0f);
        rec[5] = fbits(0.0f); rec[6] = fbits(1000.0f);
        rec[7] = 396; rec[11] = 7;                    // first and last LightParams
        Dbc dbc = Dbc::parse(makeDbc(12, { rec }, std::string(1, '\0')));
        CHECK(dbc.fieldCount() == 12);
        LightEntry e = lightEntry(dbc, 0);
        CHECK(e.id == 1 && e.mapId == 0);
        CHECK_APPROX(e.x, -9000.0f);
        CHECK_APPROX(e.falloffEnd, 1000.0f);
        CHECK(e.lightParams.size() == 5);
        CHECK(e.lightParams[0] == 396 && e.lightParams[4] == 7);
    }

    // --- CreatureModelData.dbc: id, _, modelPath@2(str) ---
    {
        std::string ms;
        ms.push_back('\0');
        size_t offCat = ms.size(); ms += "Creature\\Cat\\Cat.mdx"; ms.push_back('\0');
        std::vector<uint32_t> rec(10, 0);
        rec[0] = 815;                                 // id
        rec[2] = (uint32_t)offCat;                    // modelPath
        Dbc dbc = Dbc::parse(makeDbc(10, { rec }, ms));
        CreatureModelDataEntry e = creatureModelDataEntry(dbc, 0);
        CHECK(e.id == 815);
        CHECK(e.modelPath == "Creature\\Cat\\Cat.mdx");
        CHECK(normalizeModelPath(e.modelPath) == "Creature\\Cat\\Cat.m2");
    }

    // --- CreatureDisplayInfo.dbc: id, modelId@1, _, _, scale@4 ---
    {
        std::vector<uint32_t> rec(13, 0);
        rec[0] = 1234;                                // id (Displayid)
        rec[1] = 815;                                 // modelId -> CreatureModelData.id
        rec[4] = fbits(1.5f);                         // scale (VERIFY-FLAGGED)
        Dbc dbc = Dbc::parse(makeDbc(13, { rec }, std::string(1, '\0')));
        CreatureDisplayInfoEntry e = creatureDisplayInfoEntry(dbc, 0);
        CHECK(e.id == 1234 && e.modelId == 815);
        CHECK_APPROX(e.scale, 1.5f);
    }

    // --- GameObjectDisplayInfo.dbc: id, modelName@1(str) ---
    {
        std::string gs;
        gs.push_back('\0');
        size_t offWmo = gs.size(); gs += "World\\wmo\\Azeroth\\Buildings\\Tower.wmo"; gs.push_back('\0');
        std::vector<uint32_t> rec(18, 0);
        rec[0] = 42;                                  // id (Displayid)
        rec[1] = (uint32_t)offWmo;                    // modelName
        Dbc dbc = Dbc::parse(makeDbc(18, { rec }, gs));
        GameObjectDisplayInfoEntry e = gameObjectDisplayInfoEntry(dbc, 0);
        CHECK(e.id == 42);
        CHECK(e.modelName == "World\\wmo\\Azeroth\\Buildings\\Tower.wmo");
        CHECK(normalizeModelPath(e.modelName) == "World\\wmo\\Azeroth\\Buildings\\Tower.wmo");
    }

    // --- GroundEffectDoodad.dbc: id, modelPath@1(str) ---
    {
        std::string ds;
        ds.push_back('\0');
        size_t offDoodad = ds.size(); ds += "Detail\\Grass01.mdx"; ds.push_back('\0');
        std::vector<uint32_t> rec(2, 0);
        rec[0] = 777;                                 // id
        rec[1] = (uint32_t)offDoodad;                 // modelPath
        Dbc dbc = Dbc::parse(makeDbc(2, { rec }, ds));
        GroundEffectDoodadEntry e = groundEffectDoodadEntry(dbc, 0);
        CHECK(e.id == 777);
        CHECK(e.modelPath == "Detail\\Grass01.mdx");
        CHECK(normalizeModelPath(e.modelPath) == "Detail\\Grass01.m2");
    }

    // --- GroundEffectTexture.dbc: id, doodadIds@1..4, ... density@9 (VERIFY) ---
    {
        std::vector<uint32_t> rec(11, 0);
        rec[0] = 300;                                 // id
        rec[1] = 777;                                 // doodadIds[0] -> GroundEffectDoodad.id
        rec[2] = 778;
        rec[3] = 0;
        rec[4] = 779;
        rec[9] = 32;                                  // density (VERIFY-FLAGGED)
        Dbc dbc = Dbc::parse(makeDbc(11, { rec }, std::string(1, '\0')));
        GroundEffectTextureEntry e = groundEffectTextureEntry(dbc, 0);
        CHECK(e.id == 300);
        CHECK(e.doodadIds[0] == 777 && e.doodadIds[1] == 778);
        CHECK(e.doodadIds[2] == 0 && e.doodadIds[3] == 779);
        CHECK(e.density == 32);
    }

    // --- normalizeModelPath: .mdx/.mdl -> .m2 (case-insensitive); others unchanged ---
    CHECK(normalizeModelPath("X\\Y.mdx") == "X\\Y.m2");
    CHECK(normalizeModelPath("a.MDL") == "a.m2");
    CHECK(normalizeModelPath("z.wmo") == "z.wmo");

    // --- ItemDisplayInfo: displayId -> icon / model (23 fields x 92) --------
    {
        DbcBuilder idi(23);
        uint32_t m0 = idi.addString("Sword_2H_Claymore_A_01.mdx");
        uint32_t t0 = idi.addString("Sword_2H_Claymore_A_01");
        uint32_t ic = idi.addString("INV_Sword_09");
        std::vector<uint32_t> row(23, 0);
        row[0] = 30606; row[1] = m0; row[3] = t0; row[5] = ic;  // id, model, tex, icon
        idi.addRecord(row);
        Dbc idiDbc = Dbc::parse(idi.build());
        ItemDisplayInfoEntry e = itemDisplayInfoEntry(idiDbc, 0);
        CHECK(e.id == 30606 && e.inventoryIcon == "INV_Sword_09");
        CHECK(e.modelName[0] == "Sword_2H_Claymore_A_01.mdx");
        CHECK(e.modelTexture[0] == "Sword_2H_Claymore_A_01");

        ItemDisplayDb db;
        db.build(idiDbc);
        CHECK(db.size() == 1);
        CHECK(db.icon(30606) == "INV_Sword_09");     // displayId -> icon name
        CHECK(db.icon(999) == "");                    // unknown -> empty
        CHECK(db.get(30606) && db.get(30606)->modelName[0] == "Sword_2H_Claymore_A_01.mdx");
    }

    // --- Emotes / EmotesText: the /emote pipeline ---------------------------
    {
        // Emotes.dbc (7 fields): id, name(str), anim, flags, type, standState, sound.
        // Real rec0 = 233, <name>, 136, 8192, 2, 0, 3782.
        DbcBuilder em(7);
        uint32_t nOff = em.addString("EMOTE_ONESHOT_DANCE");
        em.addRecord({ 233, nOff, 136, 8192, 2, 0, 3782 });
        Dbc emDbc = Dbc::parse(em.build());
        EmotesEntry e = emotesEntry(emDbc, 0);
        CHECK(e.id == 233 && e.animId == 136 && e.soundId == 3782);
        CHECK(e.name == "EMOTE_ONESHOT_DANCE");

        // EmotesText.dbc (19 fields): id, name(command str), emoteId, then 16 more.
        DbcBuilder et(19);
        uint32_t cOff = et.addString("dance");
        std::vector<uint32_t> row(19, 0);
        row[0] = 10; row[1] = cOff; row[2] = 34;      // id, "dance", textEmote 34
        et.addRecord(row);
        Dbc etDbc = Dbc::parse(et.build());
        EmotesTextEntry te = emotesTextEntry(etDbc, 0);
        CHECK(te.id == 10 && te.name == "dance" && te.emoteId == 34);

        // EmoteDb ties them together: /command -> textEmote id, emote id -> anim.
        EmoteDb db;
        db.build(&emDbc, &etDbc);
        CHECK(db.emoteCount() == 1 && db.commandCount() == 1);
        CHECK(db.commandToTextEmote("dance") == 34);
        CHECK(db.commandToTextEmote("DANCE") == 34);     // case-insensitive
        CHECK(db.commandToTextEmote("wave") == -1);      // unknown -> -1
        const EmotesEntry* row233 = db.emote(233);
        CHECK(row233 && row233->animId == 136 && row233->soundId == 3782);
        CHECK(db.emote(999) == nullptr);
    }

    // --- FactionTemplate reaction (real 1.12.1 layout: 14 fields x 56) ------
    {
        // Real rec1 shape: id 188, faction 148, flags 1025, ourMask 0,
        // friendMask 8, enemyMask 0, enemies[4]=0, friends[4]=(148,28,0,0).
        DbcBuilder ft(14);
        ft.addRecord({ 188, 148, 1025, 0, 8, 0,  0,0,0,0,  148,28,0,0 });
        Dbc ftDbc = Dbc::parse(ft.build());
        FactionTemplateEntry e = factionTemplateEntry(ftDbc, 0);
        CHECK(e.id == 188 && e.faction == 148 && e.friendMask == 8);
        CHECK(e.friends[0] == 148 && e.friends[1] == 28 && e.enemies[0] == 0);

        // Reaction logic (pure). Set up: A hates group bit 0x1 and specifically
        // likes faction 50; B is in group 0x1 with faction 50.
        FactionTemplateEntry A{}; A.id = 1; A.enemyMask = 0x1; A.friends = {50,0,0,0};
        FactionTemplateEntry B{}; B.id = 2; B.faction = 50; B.ourMask = 0x1;
        // Specific friend (50) wins over the enemy mask -> friendly, not hostile.
        CHECK(!factionIsHostile(A, B));
        CHECK(factionIsFriendly(A, B));
        CHECK(factionReaction(A, B) == FactionReaction::Friendly);

        // Same masks but B's faction is a specific ENEMY -> hostile (and hostile
        // beats any friend-mask overlap).
        FactionTemplateEntry C{}; C.id = 3; C.enemies = {50,0,0,0}; C.friendMask = 0x1;
        CHECK(factionIsHostile(C, B));
        CHECK(factionReaction(C, B) == FactionReaction::Hostile);

        // Pure group masks, no specific lists: enemy mask overlap -> hostile.
        FactionTemplateEntry D{}; D.id = 4; D.enemyMask = 0x2;
        FactionTemplateEntry E{}; E.id = 5; E.ourMask = 0x2;   // faction 0 -> masks only
        CHECK(factionReaction(D, E) == FactionReaction::Hostile);
        // No overlap at all -> neutral.
        FactionTemplateEntry F{}; F.id = 6; F.ourMask = 0x4;
        CHECK(factionReaction(D, F) == FactionReaction::Neutral);

        // FactionTemplateDb: reaction by id; unknown id -> neutral.
        DbcBuilder ft2(14);
        ft2.addRecord({ 1, 0, 0, 0, 0, 0x1,  0,0,0,0,  0,0,0,0 });   // id1: hates group1
        ft2.addRecord({ 2, 0, 0, 0x1, 0, 0,   0,0,0,0,  0,0,0,0 });  // id2: in group1
        Dbc ft2Dbc = Dbc::parse(ft2.build());
        FactionTemplateDb db;
        db.build(ft2Dbc);
        CHECK(db.size() == 2);
        CHECK(db.reaction(1, 2) == FactionReaction::Hostile);
        CHECK(db.reaction(2, 1) == FactionReaction::Neutral);   // asymmetric
        CHECK(db.reaction(1, 999) == FactionReaction::Neutral); // unknown id
    }

    // --- spell-support DBCs (real 1.12.1 layouts: 4 fields x 16 bytes) ------
    {
        // SpellCastTimes: id, base(ms), perLevel, min(ms). Real rec0 = 153,3400,0,3400.
        DbcBuilder ct(4);
        ct.addRecord({ 153, 3400, 0, 3400 });
        ct.addRecord({ 2, 250, 0, 250 });
        Dbc ctDbc = Dbc::parse(ct.build());
        SpellCastTimesEntry ce = spellCastTimesEntry(ctDbc, 0);
        CHECK(ce.id == 153 && ce.baseMs == 3400 && ce.minMs == 3400);

        // SpellDuration: id, base, perLevel, max. Real rec2 = 285,1000,0,6000.
        DbcBuilder dur(4);
        dur.addRecord({ 285, 1000, 0, 6000 });
        Dbc durDbc = Dbc::parse(dur.build());
        SpellDurationEntry de = spellDurationEntry(durDbc, 0);
        CHECK(de.id == 285 && de.baseMs == 1000 && de.maxMs == 6000);

        // SpellRadius: id + three floats. Real rec0 = 15, 3.0, 0, 3.0.
        auto fbits = [](float f){ uint32_t u; std::memcpy(&u, &f, 4); return u; };
        DbcBuilder rad(4);
        rad.addRecord({ 15, fbits(3.0f), fbits(0.0f), fbits(3.0f) });
        Dbc radDbc = Dbc::parse(rad.build());
        SpellRadiusEntry re = spellRadiusEntry(radDbc, 0);
        CHECK(re.id == 15);
        CHECK_APPROX(re.radius, 3.0f);
        CHECK_APPROX(re.maxRadius, 3.0f);

        // SpellRange: id, minRange(f), maxRange(f), flags, then (unused) name cols.
        DbcBuilder rng(22);
        rng.addRecord({ 114, fbits(8.0f), fbits(35.0f), 0 });   // rest padded to 0
        Dbc rngDbc = Dbc::parse(rng.build());
        SpellRangeEntry ge = spellRangeEntry(rngDbc, 0);
        CHECK(ge.id == 114);
        CHECK_APPROX(ge.minRange, 8.0f);
        CHECK_APPROX(ge.maxRange, 35.0f);

        // SpellSupportDb indexes all four by id; misses return nullptr.
        SpellSupportDb db;
        db.build(&ctDbc, &durDbc, &radDbc, &rngDbc);
        CHECK(db.castTime(153) && db.castTime(153)->baseMs == 3400);
        CHECK(db.castTime(2)  && db.castTime(2)->baseMs == 250);
        CHECK(db.duration(285) && db.duration(285)->maxMs == 6000);
        CHECK(db.radius(15) && db.radius(15)->radius > 2.9f);
        CHECK(db.range(114) && db.range(114)->maxRange > 34.0f);
        CHECK(db.castTime(999) == nullptr && db.range(999) == nullptr);
    }

    // --- Dbc::getU32 robustness: out-of-range FIELD -> 0, RECORD -> throw ----
    // Real/patched client DBCs can carry fewer fields than a reader expects; a
    // missing field must read as 0 (not crash the editor). An out-of-range
    // record is still a caller bug and throws.
    {
        DbcBuilder b(3);
        b.addRecord({ 10, 20, 30 });
        Dbc dbc = Dbc::parse(b.build());
        CHECK(dbc.getU32(0, 2) == 30);          // in range
        CHECK(dbc.getU32(0, 3) == 0);           // field past fieldCount -> 0
        CHECK(dbc.getU32(0, 999) == 0);
        CHECK(dbc.getString(0, 5).empty());     // getString over a missing field -> ""
        bool threw = false;
        try { (void)dbc.getU32(1, 0); } catch (const std::out_of_range&) { threw = true; }
        CHECK(threw);                           // out-of-range record still throws
    }

    // --- CharHairGeosets (T3.3): typed read + resolver ----------------------
    // Layout matches the real 1.12.1 client (6 fields x 24 bytes):
    // id, race, sex, variation, geosetId, showScalp.
    {
        DbcBuilder chg(6);
        chg.addRecord({ 241, 1, 0, 0, 1, 0 });   // Human male, style 0 -> geoset 1
        chg.addRecord({ 242, 1, 0, 1, 5, 1 });   // Human male, style 1 -> geoset 5, scalp
        chg.addRecord({ 300, 1, 1, 0, 2, 0 });   // Human female, style 0 -> geoset 2
        chg.addRecord({ 301, 3, 0, 0, 7, 0 });   // Dwarf male, style 0 -> geoset 7
        Dbc dbc = Dbc::parse(chg.build());

        CharHairGeosetEntry e = charHairGeosetEntry(dbc, 1);
        CHECK(e.id == 242 && e.raceId == 1 && e.sexId == 0 && e.variation == 1);
        CHECK(e.geosetId == 5 && e.showScalp);

        HairGeosetResolver res;
        res.build(dbc);
        CHECK(res.size() == 4);
        CHECK(res.hairGeoset(1, 0, 0) == 1);        // Human male style 0
        CHECK(res.hairGeoset(1, 0, 1) == 5);        // Human male style 1
        CHECK(res.showScalp(1, 0, 1));              // that one shows scalp
        CHECK(!res.showScalp(1, 0, 0));
        CHECK(res.hairGeoset(1, 1, 0) == 2);        // Human female (distinct sex key)
        CHECK(res.hairGeoset(3, 0, 0) == 7);        // Dwarf male
        CHECK(res.hairGeoset(1, 0, 9) == -1);       // unknown variation -> miss
        CHECK(res.hairGeoset(9, 0, 0) == -1);       // unknown race -> miss
        CHECK(res.variationCount(1, 0) == 2);       // Human male has 2 styles here
        CHECK(res.variationCount(1, 1) == 1);

        // The resolved hair geoset composes with T2.2 selectGeosets: hair is
        // geoset group 0, so the chosen hair id is added alongside the base (0).
        std::vector<M2Submesh> subs;
        auto mk = [](uint16_t id){ M2Submesh s; s.id = id; return s; };
        subs.push_back(mk(0));                       // base skin
        subs.push_back(mk(1));                       // hair geoset 1
        subs.push_back(mk(5));                       // hair geoset 5
        int hair = res.hairGeoset(1, 0, 1);          // -> 5
        // Select base + only the chosen hair variation (group 0, variation = id).
        std::unordered_map<uint16_t, uint16_t> chosen{ { 0, (uint16_t)hair } };
        std::vector<uint32_t> vis = selectGeosets(subs, chosen);
        CHECK((vis == std::vector<uint32_t>{ 0, 2 }));   // base (idx0) + hair id 5 (idx2)
    }
}

void test_gridmap() {
    std::printf("[gridmap]\n");

    std::vector<uint8_t> area, height, liquid, holes;

    // AREA: non-uniform 16x16, cell (r,c) = r*16+c.
    praw(area, "AREA"); p16(area, 0); p16(area, 0);
    for (int i = 0; i < 16 * 16; ++i) p16(area, (uint16_t)i);

    // MHGT: absolute floats; V9[y*129+x] = x + y, V8 = 0.
    praw(height, "MHGT"); p32(height, 0); pf(height, 0.0f); pf(height, 500.0f);
    for (int y = 0; y < GRIDMAP_V9; ++y)
        for (int x = 0; x < GRIDMAP_V9; ++x) pf(height, (float)(x + y));
    for (int i = 0; i < GRIDMAP_V8 * GRIDMAP_V8; ++i) pf(height, 0.0f);

    // MLIQ: typed + height, 4x4 surface at level 42.
    praw(liquid, "MLIQ"); p16(liquid, 0); p16(liquid, 8 /*water*/);
    p8(liquid, 0); p8(liquid, 0); p8(liquid, 4); p8(liquid, 4); pf(liquid, 42.0f);
    for (int i = 0; i < 16 * 16; ++i) p16(liquid, 1);   // entry
    for (int i = 0; i < 16 * 16; ++i) p8(liquid, 0);    // flags
    for (int i = 0; i < 4 * 4; ++i) pf(liquid, 42.0f);  // surface heights

    // HOLES: 16x16 uint16; cell 0 = 0xFFFF.
    p16(holes, 0xFFFF);
    for (int i = 1; i < 16 * 16; ++i) p16(holes, 0);

    // Assemble header + sections with computed offsets.
    const uint32_t hdr = 44;
    const uint32_t areaOfs = hdr;
    const uint32_t heightOfs = areaOfs + (uint32_t)area.size();
    const uint32_t liquidOfs = heightOfs + (uint32_t)height.size();
    const uint32_t holesOfs  = liquidOfs + (uint32_t)liquid.size();

    std::vector<uint8_t> buf;
    praw(buf, "MAPS"); p32(buf, 0x352e317a /*'z1.5'*/); p32(buf, 5875);
    p32(buf, areaOfs);   p32(buf, (uint32_t)area.size());
    p32(buf, heightOfs); p32(buf, (uint32_t)height.size());
    p32(buf, liquidOfs); p32(buf, (uint32_t)liquid.size());
    p32(buf, holesOfs);  p32(buf, (uint32_t)holes.size());
    buf.insert(buf.end(), area.begin(),   area.end());
    buf.insert(buf.end(), height.begin(), height.end());
    buf.insert(buf.end(), liquid.begin(), liquid.end());
    buf.insert(buf.end(), holes.begin(),  holes.end());

    GridMap m = parseGridMap(buf);
    CHECK(m.buildMagic == 5875);
    CHECK(m.hasArea && !m.area.uniform);
    CHECK(m.area.grid[16 + 2] == 18);             // cell (1,2)
    CHECK(m.hasHeightSection && m.height.present);
    CHECK_APPROX(m.heightV9(0, 0), 0.0f);
    CHECK_APPROX(m.heightV9(10, 5), 15.0f);       // x+y
    CHECK_APPROX(m.heightV9(128, 128), 256.0f);
    CHECK(m.hasLiquid && m.liquid.present);
    CHECK(m.liquid.liquidType == 8 && m.liquid.width == 4);
    CHECK_APPROX(m.liquid.liquidLevel, 42.0f);
    CHECK(m.liquid.heightMap.size() == 16);
    CHECK(m.liquid.entry.size() == 256 && m.liquid.entry[0] == 1);
    CHECK(m.hasHoles && m.holes[0] == 0xFFFF && m.holes[1] == 0);

    // --- uniform-area + NO_HEIGHT variant ---
    std::vector<uint8_t> area2, height2;
    praw(area2, "AREA"); p16(area2, 0x0001 /*NO_AREA*/); p16(area2, 77);
    praw(height2, "MHGT"); p32(height2, 0x0001 /*NO_HEIGHT*/); pf(height2, 64.0f); pf(height2, 64.0f);
    std::vector<uint8_t> buf2;
    praw(buf2, "MAPS"); p32(buf2, 0); p32(buf2, 0);
    uint32_t a2 = 44, h2 = a2 + (uint32_t)area2.size();
    p32(buf2, a2); p32(buf2, (uint32_t)area2.size());
    p32(buf2, h2); p32(buf2, (uint32_t)height2.size());
    p32(buf2, 0); p32(buf2, 0); p32(buf2, 0); p32(buf2, 0);
    buf2.insert(buf2.end(), area2.begin(), area2.end());
    buf2.insert(buf2.end(), height2.begin(), height2.end());
    GridMap m2 = parseGridMap(buf2);
    CHECK(m2.area.uniform && m2.area.uniformArea == 77);
    CHECK(!m2.height.present);
    CHECK_APPROX(m2.heightV9(50, 50), 64.0f);     // flat fallback
    CHECK(!m2.hasLiquid && !m2.hasHoles);
}

void test_navmesh() {
    std::printf("[navmesh]\n");

    std::vector<uint8_t> buf;
    // MmapTileHeader (20): MMAP magic, dtVersion 7, mmapVersion 5, size, usesLiquids.
    p32(buf, 0x4d4d4150); p32(buf, 7); p32(buf, 5); p32(buf, 0); p32(buf, 0);
    // dtMeshHeader (100).
    p32(buf, 0x444E4156);                          // DT_NAVMESH_MAGIC
    p32(buf, 7);                                   // version
    p32(buf, 3); p32(buf, 4);                      // x, y
    p32(buf, 0); p32(buf, 0);                      // layer, userId
    p32(buf, 1); p32(buf, 4);                      // polyCount, vertCount
    for (int i = 0; i < 7; ++i) p32(buf, 0);       // 7 more int counts
    pf(buf, 1.0f); pf(buf, 0.5f); pf(buf, 0.2f);   // walkable h/r/climb
    pf(buf, 0); pf(buf, 0); pf(buf, 0);            // bmin
    pf(buf, 10); pf(buf, 10); pf(buf, 10);         // bmax
    pf(buf, 1.0f);                                 // bvQuantFactor
    // 4 vertices (Detour space rx,ry,rz).
    pf(buf, 0); pf(buf, 0); pf(buf, 0);
    pf(buf, 10); pf(buf, 0); pf(buf, 0);
    pf(buf, 10); pf(buf, 0); pf(buf, 10);
    pf(buf, 0); pf(buf, 0); pf(buf, 10);
    // 1 poly (32 bytes): a quad over verts 0..3.
    p32(buf, 0);                                   // firstLink
    p16(buf, 0); p16(buf, 1); p16(buf, 2); p16(buf, 3); p16(buf, 0); p16(buf, 0);
    for (int i = 0; i < 6; ++i) p16(buf, 0);       // neis
    p16(buf, 0x01);                                // flags (NAV_GROUND)
    p8(buf, 4);                                    // vertCount
    p8(buf, 0x09);                                 // areaAndType -> area 9

    NavTile t = parseMmTile(buf);
    CHECK(t.tileX == 3 && t.tileY == 4);
    CHECK(t.verts.size() == 4);
    CHECK(t.polys.size() == 1);
    CHECK(t.polys[0].vertCount == 4);
    CHECK(t.polys[0].area == 9);
    CHECK(t.polys[0].flags == 0x01);

    // Detour (rx,ry,rz) -> world (rz,rx,ry): vert 2 (10,0,10) -> (10,10,0).
    Vec3 w = navToWorld(t.verts[2]);
    CHECK_APPROX(w.x, 10.0f); CHECK_APPROX(w.y, 10.0f); CHECK_APPROX(w.z, 0.0f);

    // addNavMesh draws the quad: 4 edges -> 8 line vertices.
    DebugDraw dd;
    addNavMesh(dd, t, Rgba{0, 255, 0, 255}, DebugCategory::NavMesh);
    CHECK(dd.categoryBuffers(DebugCategory::NavMesh).lines.size() == 4u * 2u);
}

void test_vmap() {
    std::printf("[vmap]\n");

    std::vector<uint8_t> buf;
    for (char c : std::string("VMAP_4.0")) buf.push_back((uint8_t)c);   // 8-byte magic
    praw(buf, "WMOD"); p32(buf, 8); p32(buf, 1234);   // chunkSize, rootWmoId

    // group meta (AABox 6 floats + flags + wmoId = 32 bytes) precedes VERT.
    pf(buf,-1); pf(buf,-1); pf(buf,-1); pf(buf,2); pf(buf,2); pf(buf,0);
    p32(buf, 0x4); p32(buf, 7);

    // VERT: 4 verts (a quad on z=0).
    praw(buf, "VERT"); p32(buf, 4 + 12*4); p32(buf, 4);
    pf(buf,0);pf(buf,0);pf(buf,0);  pf(buf,1);pf(buf,0);pf(buf,0);
    pf(buf,1);pf(buf,1);pf(buf,0);  pf(buf,0);pf(buf,1);pf(buf,0);
    // TRIM: 2 triangles.
    praw(buf, "TRIM"); p32(buf, 4 + 12*2); p32(buf, 2);
    p32(buf,0); p32(buf,1); p32(buf,2);
    p32(buf,0); p32(buf,2); p32(buf,3);

    VmapModel m = parseWorldModel(buf);
    CHECK(m.rootWmoId == 1234);
    CHECK(m.groups.size() == 1);
    const VmapGroup& g = m.groups[0];
    CHECK(g.flags == 0x4 && g.wmoId == 7);
    CHECK_APPROX(g.bmax.x, 2.0f);
    CHECK(g.vertices.size() == 4);
    CHECK(g.triangles.size() == 2);
    CHECK(g.triangles[1][1] == 2u);
    CHECK_APPROX(g.vertices[2].x, 1.0f);

    Mesh mesh = vmapGroupToMesh(g);
    CHECK(mesh.vertices.size() == 4 && mesh.indices.size() == 6);

    DebugDraw dd;
    addCollision(dd, m, Rgba{255,0,0,255});
    // 2 triangles * 3 edges * 2 verts = 12 line vertices.
    CHECK(dd.categoryBuffers(DebugCategory::Collision).lines.size() == 12u);
}

void test_storage() {
    std::printf("[storage]\n");

    // --- DBC builder round-trips through Dbc::parse -------------------------
    DbcBuilder b(5);
    uint32_t off = b.addString("Azeroth");
    uint32_t off2 = b.addString("Azeroth");          // dedup -> same offset
    CHECK(off == off2 && off != 0);
    b.addRecord({ 0, off, 2, 0, 0 });
    b.addRecord({ 1, b.addString("Kalimdor"), 0, 0, 0 });
    std::vector<uint8_t> blob = b.build();

    Dbc dbc = Dbc::parse(blob);
    CHECK(dbc.recordCount() == 2 && dbc.fieldCount() == 5);
    CHECK(dbc.getU32(0, 0) == 0 && dbc.getU32(0, 2) == 2);
    CHECK(dbc.getString(0, 1) == "Azeroth");
    CHECK(dbc.getString(1, 1) == "Kalimdor");
    CHECK(dbc.getString(0, 4) == "");                // offset 0 -> empty

    // The typed view reads the built record.
    MapEntry me = mapEntry(dbc, 1);
    CHECK(me.id == 1);

    // --- patch-MPQ writer: write, reopen read-only, verify byte-identical ---
    const char* path = "wforge_test_patch.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "DBFilesClient\\Map.dbc", blob },
        { "Custom\\hello.txt", std::vector<uint8_t>{ 'h','i',0 } },
    };
    bool wrote = writeMpqArchive(path, files);
    CHECK(wrote);
    if (wrote) {
        MpqManager mgr;
        CHECK(mgr.addArchive(path));
        CHECK(mgr.contains("DBFilesClient\\Map.dbc"));
        std::vector<uint8_t> back;
        CHECK(mgr.readFile("DBFilesClient\\Map.dbc", back));
        CHECK(back == blob);                          // exact round-trip
        std::vector<uint8_t> txt;
        CHECK(mgr.readFile("Custom\\hello.txt", txt));
        CHECK(txt.size() == 3 && txt[0] == 'h');
    }
    std::remove(path);
}
