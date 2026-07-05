#pragma once
// ---------------------------------------------------------------------------
// Selectors: the three reusable field-editor widgets the DBC-backed tools
// share (enum combo, flags popup, search picker) -- the Keira3-style widget
// kinds, implemented from the behavior description. Each widget is a thin
// ImGui wrapper over a pure core so the logic is unit-tested headless:
//   - EnumCombo:   dropdown for enum-like fields; pure core enumLabel().
//   - FlagsPopup:  a summary button ("Flag1|Flag3") opening a checkbox list
//                  that composes the bitmask; pure core flagsSummary().
//   - SearchPicker: a modal searching an abstract id->name index (count/at
//                  SearchSource); pure core rankMatches() ranks an exact-id
//                  hit first, then case-insensitive name substrings, in
//                  stable source order.
// Ready-made SearchSource adapters wrap the dbc_index AreaIndex/MapIndex
// (constructed from an id vector -- dbc_index itself is untouched).
// Intended consumers: the Area-ID chunk tool (AreaTable picker) and the
// EntityInspectorPanel spawnEntry field.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace wf {
class AreaIndex;
class MapIndex;
} // namespace wf

namespace wf::editor {

// ---- enum combo -------------------------------------------------------------

struct Option {
    int32_t     value;
    const char* label;
};

// Label for `v` in the option set, or nullptr when the value is not listed
// (the combo then shows the raw number).
const char* enumLabel(int32_t v, const Option* opts, size_t count);

// Dropdown over the options; true when *v changed this frame.
bool EnumCombo(const char* id, int32_t* v, const Option* opts, size_t count);
template <size_t N>
bool EnumCombo(const char* id, int32_t* v, const Option (&opts)[N]) {
    return EnumCombo(id, v, opts, N);
}

// ---- flags popup ------------------------------------------------------------

struct FlagBit {
    uint32_t    bit;
    const char* label;
};

// Collapsed-button label for a mask: set flags joined '|' ("Indoor|Sanctuary"),
// residual bits no FlagBit names appended as one trailing hex term ("0x10"),
// and "None" for a zero mask.
std::string flagsSummary(uint32_t mask, const FlagBit* bits, size_t count);
template <size_t N>
std::string flagsSummary(uint32_t mask, const FlagBit (&bits)[N]) {
    return flagsSummary(mask, bits, N);
}

// A button labelled flagsSummary(*mask) opening a checkbox-per-bit popup that
// composes the bitmask; true when *mask changed this frame.
bool FlagsPopup(const char* id, uint32_t* mask, const FlagBit* bits, size_t count);
template <size_t N>
bool FlagsPopup(const char* id, uint32_t* mask, const FlagBit (&bits)[N]) {
    return FlagsPopup(id, mask, bits, N);
}

// ---- search picker ----------------------------------------------------------

// An abstract searchable id->name index: `count` entries, `at(i)` yields
// (id, display name). Both callbacks must be set for the picker to work.
struct SearchSource {
    std::function<size_t()>                                 count;
    std::function<std::pair<uint32_t, std::string>(size_t)> at;
};

// Rank the entries against `needle`: an entry whose id equals the needle
// parsed as a decimal integer comes first, then entries whose name contains
// the needle case-insensitively -- each group in stable source order, no
// duplicates, capped at maxResults. An empty needle lists the first
// maxResults entries.
std::vector<std::pair<uint32_t, std::string>>
rankMatches(const SearchSource& src, const std::string& needle, size_t maxResults);

// SearchSource over an explicit (id, name) list -- the general adapter (the
// vector is moved into the source, which owns it).
SearchSource makeSearchSource(std::vector<std::pair<uint32_t, std::string>> items);

// Adapters over the dbc_index lookups: names resolve lazily through the index
// (AreaIndex::fullName / MapEntry::name), ids come from the supplied vector.
// The index must outlive the source; the id vector is owned by the source.
SearchSource areaSearchSource(const AreaIndex& areas, std::vector<uint32_t> ids);
SearchSource mapSearchSource(const MapIndex& maps, std::vector<uint32_t> ids);

// Modal search picker. open() arms the popup; draw() renders it (query box +
// ranked, clipped result list) and returns true ONCE when an entry was picked
// -- the picked id is then in picked() / drained via takePicked(). pick() is
// the programmatic form (tests, host shortcuts).
class SearchPicker {
public:
    void open() { openReq_ = true; }
    bool draw(const char* id, const SearchSource& src);

    void pick(uint32_t id);
    bool takePicked(uint32_t& outId);              // edge-triggered drain
    uint32_t picked() const { return picked_; }

    // UI state (public for tests):
    char   query_[128]  = {0};
    size_t maxResults_  = 64;

private:
    bool     openReq_       = false;
    bool     pickedPending_ = false;
    uint32_t picked_        = 0;
};

} // namespace wf::editor
