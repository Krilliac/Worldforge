// ---------------------------------------------------------------------------
// Vendor inventory codec round-trip tests (src/net/vendor_list.hpp).
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/vendor_list.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_vendor_list() {
    std::printf("[net.vendor_list]\n");

    // CMSG_LIST_INVENTORY: just the vendor guid.
    {
        std::vector<uint8_t> req = encodeListInventoryRequest(0xF13000000000ABCDull);
        CHECK(req.size() == 8);
        ByteReader r(req.data(), req.size());
        CHECK(decodeListInventoryRequest(r) == 0xF13000000000ABCDull && r.remaining() == 0);
    }

    // Populated vendor: two items, one unlimited + one limited stock.
    {
        VendorList in;
        in.vendorGuid = 0xF13000000000ABCDull;
        in.items.push_back({ 1, 4540, 6410, kVendorUnlimited, 25,  0, 1 });  // bread
        in.items.push_back({ 2, 2589, 6835, 5,               10, 55, 20 }); // linen x20
        std::vector<uint8_t> b = encodeVendorList(in);
        CHECK(b.size() == 8 + 1 + 2 * 7 * 4);   // guid + count + 2 items

        ByteReader r(b.data(), b.size());
        VendorList out = decodeVendorList(r);
        CHECK(r.remaining() == 0);
        CHECK(out.vendorGuid == in.vendorGuid && out.items.size() == 2);
        CHECK(out.items[0].slot == 1 && out.items[0].itemId == 4540);
        CHECK(out.items[0].remainingCount == kVendorUnlimited && out.items[0].price == 25);
        CHECK(out.items[1].itemId == 2589 && out.items[1].remainingCount == 5);
        CHECK(out.items[1].buyCount == 20 && out.items[1].maxDurability == 55);
    }

    // Empty vendor: count 0 then a trailing error byte.
    {
        VendorList in; in.vendorGuid = 0x123; in.emptyError = 0;
        std::vector<uint8_t> b = encodeVendorList(in);
        CHECK(b.size() == 8 + 1 + 1);           // guid + count(0) + error byte
        ByteReader r(b.data(), b.size());
        VendorList out = decodeVendorList(r);
        CHECK(out.items.empty() && out.emptyError == 0 && r.remaining() == 0);
    }
}
