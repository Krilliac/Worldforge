#pragma once
// ---------------------------------------------------------------------------
// Decoders for the two opcodes that actually put entities on the map and move
// them, vanilla 1.12.1 (build 5875):
//
//   SMSG_UPDATE_OBJECT  (0x0A9) -- create/update objects (the player, NPCs, GOs)
//   SMSG_MONSTER_MOVE   (0x0DD) -- spline movement for a creature
//   SMSG_DESTROY_OBJECT (0x0AA) -- remove an object
//
// Every function here is PURE (byte_reader only, no sockets) and emits records
// shaped like src/server/world_sim.hpp's SimObject, so the decoded server state
// flows straight into the existing offline world_view / WorldSim tick. Layouts
// cross-checked against CMaNGOS Object::BuildCreateUpdateBlockForPlayer /
// BuildMovementUpdate / _BuildValuesUpdate, the vanilla UpdateFields.h, and the
// GTKer / wowdev wire docs; see docs/research/3-online-login-world-entry.md
// sections 2.6-2.8.
//
// Scope note: this decodes the common "standing / simply-moving" entity case
// (UPDATEFLAG_LIVING with no transport/jump/spline sub-blocks, and the
// non-living HAS_POSITION case for gameobjects), which is what the world-entry
// burst is dominated by. The values block is read by mask so any field set is
// consumed correctly even when we only surface a few of them; exotic movement
// sub-blocks would need extending (they never appear for a standing spawn).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "byte_reader.hpp"
#include "server/world_sim.hpp"

namespace wf {
namespace net {

// ---- update block types (UPDATETYPE_*) -------------------------------------
enum class UpdateType : uint8_t {
    VALUES             = 0,
    MOVEMENT           = 1,
    CREATE_OBJECT      = 2,
    CREATE_OBJECT2     = 3,
    OUT_OF_RANGE_OBJECTS = 4,
    NEAR_OBJECTS       = 5,
};

// ---- object types (per CREATE_OBJECT block) --------------------------------
enum class ObjectType : uint8_t {
    OBJECT = 0, ITEM = 1, CONTAINER = 2, UNIT = 3,
    PLAYER = 4, GAMEOBJECT = 5, DYNAMICOBJECT = 6, CORPSE = 7,
};

// ---- vanilla movement update_flags (UPDATEFLAG_*) --------------------------
enum UpdateFlag : uint8_t {
    UPDATEFLAG_SELF         = 0x01,
    UPDATEFLAG_TRANSPORT    = 0x02,
    UPDATEFLAG_HAS_TARGET   = 0x04,
    UPDATEFLAG_LOWGUID      = 0x10,
    UPDATEFLAG_LIVING       = 0x20,
    UPDATEFLAG_HAS_POSITION = 0x40,
};

// ---- vanilla movement_flags subset we must understand to stay byte-aligned -
enum MoveFlag : uint32_t {
    MOVEFLAG_ONTRANSPORT      = 0x00000200,
    MOVEFLAG_SWIMMING         = 0x00200000,
    MOVEFLAG_JUMPING          = 0x00002000,
    MOVEFLAG_SPLINE_ELEVATION = 0x02000000,
    MOVEFLAG_SPLINE_ENABLED   = 0x04000000,
};

// ---- a handful of vanilla UpdateFields.h indices we surface -----------------
// (Everything else in the values block is read-and-skipped by mask.)
enum UpdateField : uint32_t {
    OBJECT_FIELD_GUID    = 0x00,   // 2 dwords
    OBJECT_FIELD_TYPE    = 0x02,
    OBJECT_FIELD_ENTRY   = 0x03,
    OBJECT_FIELD_SCALE_X = 0x04,   // float
    UNIT_FIELD_DISPLAYID = 0x29,   // vanilla 1.12 unit display model id
    UNIT_FIELD_LEVEL     = 0x36,
};

// A decoded object block, shaped like SimObject so it drops into WorldSim.
struct ObjectUpdate {
    UpdateType  type   = UpdateType::VALUES;
    uint64_t    guid   = 0;
    EntityKind  kind   = EntityKind::Creature;
    bool        hasPosition = false;
    Vec3        pos;
    float       orientation = 0.0f;
    bool        isSelf  = false;       // UPDATEFLAG_SELF (the logged-in player)
    // values pulled from the UpdateMask (0 if the field was not present):
    uint32_t    entry     = 0;
    uint32_t    displayId = 0;
    uint32_t    level     = 0;
    float       scale     = 1.0f;
};

struct UpdateObjectResult {
    std::vector<ObjectUpdate> objects;        // CREATE/VALUES/MOVEMENT blocks
    std::vector<uint64_t>     outOfRange;      // guids leaving range (type 4)
};

// ---- packed GUID (wowdev Packed_GUID) --------------------------------------
// u8 mask, then for each set bit i (LSB first) the i-th byte of the 8-byte LE
// GUID; zero bytes are omitted.
inline uint64_t readPackedGuid(ByteReader& r) {
    uint8_t mask = r.u8();
    uint64_t guid = 0;
    for (int i = 0; i < 8; ++i) {
        if (mask & (1u << i)) {
            guid |= static_cast<uint64_t>(r.u8()) << (8 * i);
        }
    }
    return guid;
}

// Map a CREATE_OBJECT object_type to the SimObject EntityKind.
inline EntityKind objectTypeToKind(ObjectType t) {
    switch (t) {
        case ObjectType::PLAYER:     return EntityKind::Player;
        case ObjectType::GAMEOBJECT: return EntityKind::GameObject;
        default:                     return EntityKind::Creature;
    }
}

// Read the MovementBlock (vanilla, common case). Fills position/orientation when
// present and advances the reader past every conditional sub-block so the values
// block that follows stays aligned. Returns the update_flags byte.
inline uint8_t readMovementBlock(ByteReader& r, ObjectUpdate& o) {
    uint8_t flags = r.u8();
    o.isSelf = (flags & UPDATEFLAG_SELF) != 0;

    if (flags & UPDATEFLAG_LIVING) {
        uint32_t moveFlags = r.u32();
        r.u32();                                   // time (ms)
        o.pos = { r.f32(), r.f32(), r.f32() };
        o.orientation = r.f32();
        o.hasPosition = true;

        if (moveFlags & MOVEFLAG_ONTRANSPORT) {
            readPackedGuid(r);                     // transport guid
            r.f32(); r.f32(); r.f32(); r.f32();    // transport offset x,y,z,o
            r.u32();                               // transport time
        }
        if (moveFlags & MOVEFLAG_SWIMMING) {
            r.f32();                               // pitch
        }
        r.f32();                                   // fall time

        if (moveFlags & MOVEFLAG_JUMPING) {
            r.f32(); r.f32(); r.f32(); r.f32();    // z-speed, cos, sin, xy-speed
        }
        if (moveFlags & MOVEFLAG_SPLINE_ELEVATION) {
            r.f32();                               // spline elevation
        }
        // 6 movement speeds (walk, run, run-back, swim, swim-back, turn-rate).
        for (int i = 0; i < 6; ++i) r.f32();

        if (moveFlags & MOVEFLAG_SPLINE_ENABLED) {
            // Full spline block does not appear for the simple standing-spawn
            // case this decoder targets; bail loudly if it ever does so a caller
            // does not silently consume garbage.
            throw std::runtime_error("update_object: spline-in-movement not supported");
        }
    } else if (flags & UPDATEFLAG_HAS_POSITION) {
        // Non-living object (e.g. gameobject): a bare x,y,z,o.
        o.pos = { r.f32(), r.f32(), r.f32() };
        o.orientation = r.f32();
        o.hasPosition = true;
    }

    if (flags & UPDATEFLAG_LOWGUID) {
        r.u32();                                   // low guid / first value
    }
    if (flags & UPDATEFLAG_HAS_TARGET) {
        readPackedGuid(r);                         // victim guid
    }
    return flags;
}

// Read the UpdateMask values block and surface the few fields we care about.
inline void readValuesBlock(ByteReader& r, ObjectUpdate& o) {
    uint8_t blocks = r.u8();
    std::vector<uint32_t> mask(blocks);
    for (uint8_t i = 0; i < blocks; ++i) mask[i] = r.u32();

    auto isSet = [&](uint32_t field) -> bool {
        uint32_t word = field / 32, bit = field % 32;
        return word < mask.size() && (mask[word] & (1u << bit)) != 0;
    };

    // Fields stream in ascending index order; read every set field, capturing
    // the ones we expose and discarding the rest, so we stay aligned regardless.
    uint32_t totalFields = static_cast<uint32_t>(blocks) * 32;
    for (uint32_t field = 0; field < totalFields; ++field) {
        if (!isSet(field)) continue;
        uint32_t raw = r.u32();
        switch (field) {
            case OBJECT_FIELD_ENTRY:   o.entry     = raw; break;
            case UNIT_FIELD_DISPLAYID: o.displayId = raw; break;
            case UNIT_FIELD_LEVEL:     o.level     = raw; break;
            case OBJECT_FIELD_SCALE_X: {
                float f; std::memcpy(&f, &raw, sizeof(f)); o.scale = f; break;
            }
            default: break;                        // consumed, not surfaced
        }
    }
}

// Decode an SMSG_UPDATE_OBJECT body (already decompressed if it arrived as
// SMSG_COMPRESSED_UPDATE_OBJECT).
inline UpdateObjectResult decodeUpdateObject(const std::vector<uint8_t>& body) {
    UpdateObjectResult out;
    ByteReader r(body);
    uint32_t count = r.u32();
    r.u8();                                        // has_transport (vanilla bool)

    for (uint32_t i = 0; i < count; ++i) {
        UpdateType type = static_cast<UpdateType>(r.u8());

        if (type == UpdateType::OUT_OF_RANGE_OBJECTS ||
            type == UpdateType::NEAR_OBJECTS) {
            uint32_t n = r.u32();
            for (uint32_t j = 0; j < n; ++j) {
                uint64_t g = readPackedGuid(r);
                if (type == UpdateType::OUT_OF_RANGE_OBJECTS) out.outOfRange.push_back(g);
            }
            continue;
        }

        ObjectUpdate o;
        o.type = type;
        o.guid = readPackedGuid(r);

        if (type == UpdateType::CREATE_OBJECT || type == UpdateType::CREATE_OBJECT2) {
            ObjectType ot = static_cast<ObjectType>(r.u8());
            o.kind = objectTypeToKind(ot);
            readMovementBlock(r, o);
            readValuesBlock(r, o);
        } else if (type == UpdateType::MOVEMENT) {
            readMovementBlock(r, o);
        } else { // VALUES
            readValuesBlock(r, o);
        }
        out.objects.push_back(std::move(o));
    }
    return out;
}

// Merge a decoded CREATE/VALUES block into a SimObject (overwriting only the
// fields the block actually carried). Lets a client apply incremental updates.
inline void applyToSimObject(const ObjectUpdate& u, SimObject& o) {
    o.guid = u.guid;
    o.kind = u.kind;
    if (u.entry)     o.entry = u.entry;
    if (u.hasPosition) { o.pos = u.pos; o.orientation = u.orientation; }
    if (u.scale != 1.0f) o.radius = u.scale;
}

// ---- SMSG_MONSTER_MOVE (0x0DD) ---------------------------------------------
enum class MonsterMoveType : uint8_t {
    NORMAL = 0, STOP = 1, FACING_SPOT = 2, FACING_TARGET = 3, FACING_ANGLE = 4,
};

struct MonsterMove {
    uint64_t          guid     = 0;
    Vec3              start;                  // spline start point
    uint32_t          splineId = 0;
    MonsterMoveType   moveType = MonsterMoveType::NORMAL;
    uint32_t          splineFlags = 0;
    uint32_t          durationMs  = 0;
    std::vector<Vec3> points;                 // waypoints (vanilla: full Vec3s)
    bool              stop = false;
};

inline MonsterMove decodeMonsterMove(const std::vector<uint8_t>& body) {
    MonsterMove m;
    ByteReader r(body);
    m.guid  = readPackedGuid(r);
    m.start = { r.f32(), r.f32(), r.f32() };
    m.splineId = r.u32();
    m.moveType = static_cast<MonsterMoveType>(r.u8());

    switch (m.moveType) {
        case MonsterMoveType::FACING_SPOT:   r.f32(); r.f32(); r.f32(); break;
        case MonsterMoveType::FACING_TARGET: r.u64(); break;
        case MonsterMoveType::FACING_ANGLE:  r.f32(); break;
        default: break;
    }

    m.splineFlags = r.u32();
    if (m.moveType == MonsterMoveType::STOP) {  // STOP omits duration + points
        m.stop = true;
        return m;
    }
    m.durationMs = r.u32();
    uint32_t n = r.u32();
    m.points.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
        m.points.push_back({ r.f32(), r.f32(), r.f32() });
    return m;
}

// Apply a decoded monster-move to a SimObject: load its waypoints + final
// position so the existing WorldSim::tick() interpolates the same path.
inline void applyMonsterMove(const MonsterMove& m, SimObject& o) {
    o.guid = m.guid;
    if (m.stop) {
        o.moving = false;
        o.waypoints.clear();
        return;
    }
    o.waypoints = m.points;
    o.wpIndex = 0;
    o.moving = !m.points.empty();
    if (!m.points.empty()) {
        o.pos = m.points.back();                 // server's authoritative end
    }
}

// ---- SMSG_DESTROY_OBJECT (0x0AA) -------------------------------------------
// CMaNGOS writes a plain (non-packed) u64 here in vanilla.
inline uint64_t decodeDestroyObject(const std::vector<uint8_t>& body) {
    ByteReader r(body);
    return r.u64();
}

} // namespace net
} // namespace wf
