#pragma once
// ---------------------------------------------------------------------------
// Page-text query codec for vanilla 1.12.1 (build 5875) -- the text on in-world
// readable objects (books, signs, letters). Pages form a linked list: each
// response carries one page's text plus the id of the NEXT page (0 = last), so
// a multi-page book is walked by re-querying nextPageId until it is 0.
//
//   client -> CMSG_PAGE_TEXT_QUERY          (0x05A)  { u32 pageId, u64 guid }
//   server -> SMSG_PAGE_TEXT_QUERY_RESPONSE (0x05B)  { u32 pageId, cstring text,
//                                                      u32 nextPageId }
//
// The request guid is the object being read; the server ignores it (the page id
// is the key). Verified vs mangos-zero HandlePageTextQueryOpcode; our own code.
// Pure over byte_reader/byte_writer (tests/test_page_text.cpp).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"   // writeCString / readCString

namespace wf {

// ---- CMSG_PAGE_TEXT_QUERY ---------------------------------------------------
inline std::vector<uint8_t> encodePageTextQuery(uint32_t pageId, uint64_t guid) {
    ByteWriter w;
    w.u32(pageId);
    w.u64(guid);
    return w.data();
}
struct PageTextRequest { uint32_t pageId = 0; uint64_t guid = 0; };
inline PageTextRequest decodePageTextQuery(ByteReader& r) {
    PageTextRequest q;
    q.pageId = r.u32();
    q.guid   = r.u64();
    return q;
}

// ---- SMSG_PAGE_TEXT_QUERY_RESPONSE ------------------------------------------
struct PageTextResponse {
    uint32_t    pageId     = 0;
    std::string text;
    uint32_t    nextPageId = 0;   // 0 = last page; else the client queries it next
};

inline std::vector<uint8_t> encodePageTextResponse(const PageTextResponse& p) {
    ByteWriter w;
    w.u32(p.pageId);
    writeCString(w, p.text);
    w.u32(p.nextPageId);
    return w.data();
}

inline PageTextResponse decodePageTextResponse(ByteReader& r) {
    PageTextResponse p;
    p.pageId     = r.u32();
    p.text       = readCString(r);
    p.nextPageId = r.u32();
    return p;
}

// True while there is another page to fetch (a convenience for the read loop:
//   while (resp.hasNext()) resp = decode(query(resp.nextPageId))).
inline bool pageHasNext(const PageTextResponse& p) { return p.nextPageId != 0; }

}  // namespace wf
