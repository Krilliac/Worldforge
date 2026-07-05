// ---------------------------------------------------------------------------
// Page-text query codec round-trip tests (src/net/page_text.hpp). Books/signs:
// a page + a link to the next page (0 = last).
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/page_text.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_page_text() {
    std::printf("[net.page_text]\n");

    // CMSG_PAGE_TEXT_QUERY: page id + the object guid.
    {
        std::vector<uint8_t> req = encodePageTextQuery(42, 0xF11000000000BEEFull);
        CHECK(req.size() == 4 + 8);
        ByteReader r(req.data(), req.size());
        PageTextRequest q = decodePageTextQuery(r);
        CHECK(q.pageId == 42 && q.guid == 0xF11000000000BEEFull && r.remaining() == 0);
    }

    // SMSG response: a middle page (links to the next).
    {
        PageTextResponse in;
        in.pageId = 42; in.text = "Chapter One"; in.nextPageId = 43;
        std::vector<uint8_t> b = encodePageTextResponse(in);
        CHECK(b.size() == 4 + 12 + 4);          // pageId + "Chapter One\0" + next
        ByteReader r(b.data(), b.size());
        PageTextResponse out = decodePageTextResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.pageId == 42 && out.text == "Chapter One" && out.nextPageId == 43);
        CHECK(pageHasNext(out));
    }

    // Last page: nextPageId 0.
    {
        PageTextResponse in; in.pageId = 43; in.text = "The End"; in.nextPageId = 0;
        std::vector<uint8_t> b = encodePageTextResponse(in);
        ByteReader r(b.data(), b.size());
        PageTextResponse out = decodePageTextResponse(r);
        CHECK(out.nextPageId == 0 && !pageHasNext(out) && out.text == "The End");
    }

    // Empty page text still frames (just the NUL).
    {
        PageTextResponse in; in.pageId = 1; in.text = ""; in.nextPageId = 0;
        std::vector<uint8_t> b = encodePageTextResponse(in);
        ByteReader r(b.data(), b.size());
        PageTextResponse out = decodePageTextResponse(r);
        CHECK(out.text.empty() && out.pageId == 1 && r.remaining() == 0);
    }
}
