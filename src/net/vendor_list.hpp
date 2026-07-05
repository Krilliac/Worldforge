#pragma once
// ---------------------------------------------------------------------------
// Vendor inventory codec for vanilla 1.12.1 (build 5875) -- the item list an NPC
// merchant shows when you open its "buy" window.
//
//   client -> CMSG_LIST_INVENTORY  (0x19E)  { u64 vendorGuid }
//   server -> SMSG_LIST_INVENTORY  (0x19F)  { u64 vendorGuid, u8 count,
//                                             count * VendorItem }
//     VendorItem = { u32 slot, u32 itemId, u32 displayId, u32 remainingCount,
//                    u32 price, u32 maxDurability, u32 buyCount }   (7 x u32)
//
// The slot is the merchant's 1-based display index. remainingCount 0xFFFFFFFF
// means unlimited stock. When the vendor is empty the count byte is 0 and a
// single trailing u8 error code follows ("Vendor has no inventory"). Verified vs
// mangos-zero SendListInventory; our own code. Pure over byte_reader/writer.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"

namespace wf {

inline constexpr uint32_t kVendorUnlimited = 0xFFFFFFFFu;   // remainingCount sentinel

// ---- CMSG_LIST_INVENTORY ----------------------------------------------------
inline std::vector<uint8_t> encodeListInventoryRequest(uint64_t vendorGuid) {
    ByteWriter w;
    w.u64(vendorGuid);
    return w.data();
}
inline uint64_t decodeListInventoryRequest(ByteReader& r) { return r.u64(); }

// ---- SMSG_LIST_INVENTORY ----------------------------------------------------
struct VendorItem {
    uint32_t slot           = 0;   // 1-based merchant display index
    uint32_t itemId         = 0;
    uint32_t displayId      = 0;
    uint32_t remainingCount = kVendorUnlimited;  // 0xFFFFFFFF = unlimited
    uint32_t price          = 0;   // copper (already reputation-discounted)
    uint32_t maxDurability  = 0;
    uint32_t buyCount       = 1;   // stack size bought per purchase
};

struct VendorList {
    uint64_t vendorGuid = 0;
    std::vector<VendorItem> items;
    uint8_t  emptyError = 0;       // only meaningful when items is empty
};

inline std::vector<uint8_t> encodeVendorList(const VendorList& v) {
    ByteWriter w;
    w.u64(v.vendorGuid);
    // count is a byte; vanilla caps a merchant page well under 256 items.
    uint8_t count = static_cast<uint8_t>(v.items.size());
    w.u8(count);
    for (const VendorItem& it : v.items) {
        w.u32(it.slot);
        w.u32(it.itemId);
        w.u32(it.displayId);
        w.u32(it.remainingCount);
        w.u32(it.price);
        w.u32(it.maxDurability);
        w.u32(it.buyCount);
    }
    if (count == 0) w.u8(v.emptyError);   // trailing "no inventory" error code
    return w.data();
}

inline VendorList decodeVendorList(ByteReader& r) {
    VendorList v;
    v.vendorGuid = r.u64();
    uint8_t count = r.u8();
    v.items.reserve(count);
    for (uint8_t i = 0; i < count; ++i) {
        VendorItem it;
        it.slot           = r.u32();
        it.itemId         = r.u32();
        it.displayId      = r.u32();
        it.remainingCount = r.u32();
        it.price          = r.u32();
        it.maxDurability  = r.u32();
        it.buyCount       = r.u32();
        v.items.push_back(it);
    }
    if (count == 0) v.emptyError = r.u8();
    return v;
}

}  // namespace wf
