#pragma once
// ---------------------------------------------------------------------------
// DbcBrowserPanel: a generic grid browser over any DBC table with a registered
// TableSchema (dbc_defs.hpp) -- the "open any table" data view. A table combo
// picks from the schema registry (edge-triggered load request served by the
// host, which owns the MPQ IO), then an ImGui::Table shows the records with
// sortable headers, a per-column filter popover, one query box ('col:value'
// terms AND together), a hex toggle for numeric columns and a hide-empty-
// columns toggle. Foreign-key cells (ColumnDef.fkTable) render as
// "id (resolved name)" via a host-registered resolver, and clicking one emits
// an FkNav event the host drains for click-through navigation. Only visible
// rows materialize (ImGuiListClipper). A row "duplicate" action allocates the
// next free id (max id + 1) and DEEP-COPIES the record's fields into a
// panel-side buffer -- the copy never aliases the source record.
//
// The query/filter/sort layer is pure free functions so the logic is
// unit-tested headless; draw() is the thin widget layer. Query recompute is
// debounced (150 ms) against an injectable millisecond clock so tests drive
// the debounce deterministically.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "dbc_defs.hpp"    // TableSchema / ColumnDef / findSchema
#include "wow_files.hpp"   // Dbc

namespace wf::editor {

// ---- pure query layer (free functions; no ImGui) ---------------------------

// One parsed search term. An empty `column` is a bare term that matches any
// Str/LocStr column; otherwise it binds to the ColumnDef with that name
// (case-insensitive). Terms AND together.
struct QueryTerm {
    std::string column;          // empty = any string column
    std::string value;
    bool        isRegex = false; // string matching uses an icase regex
};

// Parse the query-box syntax: space-separated terms; 'col:value' binds a term
// to a column; quoted values keep spaces (name:"test name" or a bare "two
// words"). Empty-value terms ('name:') are dropped. `regex` stamps isRegex on
// every produced term (the panel's regex-mode toggle).
std::vector<QueryTerm> parseQuery(std::string_view text, bool regex = false);

// Record indices (0-based, ascending) whose row matches EVERY term. Numeric
// columns compare against the term value parsed as a number (exact match; a
// non-numeric value matches nothing). String columns match case-insensitive
// substring, or an icase regex when isRegex is set (an invalid regex matches
// nothing). An unknown column name matches nothing. Array columns match if any
// element matches; LocStr compares the enUS slot.
std::vector<uint32_t> filterRows(const Dbc& dbc, const TableSchema& schema,
                                 const std::vector<QueryTerm>& terms);

// Stable-sort `rows` (record indices) by schema column `col`: numeric columns
// compare as numbers per ColType (U32 unsigned, I32 signed, F32 float);
// Str/LocStr compare case-insensitive lexicographic. Out-of-range col: no-op.
void sortRows(const Dbc& dbc, const TableSchema& schema,
              std::vector<uint32_t>& rows, size_t col, bool ascending);

// True when every row's value for schema column `col` is zero/empty (numeric
// fields all 0; string slots all resolve to ""). Zero-row tables are empty.
// Drives the 'hide empty columns' toggle.
bool columnIsEmpty(const Dbc& dbc, const TableSchema& schema, size_t col);

// Schema column index whose name matches (case-insensitive), or -1.
int findColumn(const TableSchema& schema, std::string_view name);

// Index of the isId column, or -1 if the schema has none.
int schemaIdColumn(const TableSchema& schema);

// Next free primary key: max value in the id column + 1 (1 for an empty
// table). Requires an isId column -- returns 0 if the schema has none.
uint32_t nextFreeId(const Dbc& dbc, const TableSchema& schema);

// A clicked foreign-key cell: navigate to `table`, row with key `id`.
struct FkNav {
    std::string table;
    uint32_t    id = 0;
};

// ---- the panel --------------------------------------------------------------

class DbcBrowserPanel {
public:
    // Millisecond clock (injectable for tests); default is steady_clock.
    using ClockFn = std::function<uint64_t()>;
    // Resolves (fkTable, id) -> display name; "" leaves the cell numeric.
    using FkResolver = std::function<std::string(std::string_view table, uint32_t id)>;

    // A duplicated row: a deep field-value copy, plus the source record it was
    // copied from (used only to resolve string-offset fields for display).
    struct AddedRow {
        uint32_t              srcRec = 0;
        std::vector<uint32_t> fields;   // full record, id field rewritten
    };

    DbcBrowserPanel();

    void setClock(ClockFn clock);
    void setFkResolver(FkResolver resolver);

    // Attach the table to browse. The panel does not own `dbc`; the host keeps
    // it alive while attached. Resets filters, sort and duplicated rows.
    // Either pointer nullptr detaches.
    void setTable(const TableSchema* schema, const Dbc* dbc);
    const TableSchema* schema() const { return schema_; }

    // Render the panel; window title "DBC Browser".
    void draw();

    // --- query / filter state (debounced) -----------------------------------
    // Commit new query-box text; recompute happens in refresh() once the text
    // has been stable for 150 ms. Identical text is a no-op.
    void setQueryText(const std::string& text);
    // Per-column filter (the header popover); "" clears. Same debounce.
    void setColumnFilter(size_t col, const std::string& value);
    // Recompute visibleRows() if a query/filter edit is due; true if it ran.
    // draw() calls this every frame; tests call it against the fake clock.
    bool refresh();

    // Filtered (and sorted) record indices -- what the grid shows.
    const std::vector<uint32_t>& visibleRows() const { return visible_; }

    // Sort by schema column `col` (applies immediately, no debounce).
    void sortBy(int col, bool ascending);

    // --- events (edge-triggered, like MapBrowserPanel) -----------------------
    // The user picked a different table in the combo (also requestTable());
    // host loads that DBC and calls setTable().
    bool takeTableRequest(std::string& outName);
    void requestTable(std::string dbcName);
    // A foreign-key cell was clicked (also clickFk()); host navigates.
    bool takeFkNav(FkNav& out);
    // Programmatic FK click (also called by draw on a cell click) -- no-op
    // unless column `col` has fkTable set; uses the first array element.
    void clickFk(uint32_t rec, size_t col);

    // --- duplicate-row action ------------------------------------------------
    // Enabled only when the schema has an isId column.
    bool canDuplicate() const;
    // Deep-copy record `rec` with a freshly-allocated id (max id over the dbc
    // AND already-duplicated rows, + 1). Returns the new id, or -1 when
    // disabled / rec out of range. The copy is independent of the source.
    int64_t duplicateRow(uint32_t rec);
    const std::vector<AddedRow>& addedRows() const { return added_; }
    std::vector<AddedRow>&       addedRows()       { return added_; }

    // --- cell formatting (pure, public for tests) ----------------------------
    // Record cell text: numerics per the hex toggle, arrays joined ", ",
    // LocStr = enUS slot, FK values as "id (name)" when the resolver knows it.
    std::string cellText(uint32_t rec, size_t col) const;
    // Same for a duplicated row (strings resolve through its source record).
    std::string addedCellText(size_t addedIdx, size_t col) const;

    // Every-row-zero flag per column, cached at setTable (hide-empty toggle).
    bool columnEmpty(size_t col) const;

    // --- UI state (public for tests) ----------------------------------------
    bool hexDisplay_ = false;   // numeric cells as 0x…
    bool hideEmpty_  = false;   // hide all-zero/empty columns
    bool regexMode_  = false;   // string terms are icase regexes
    int  sortCol_    = -1;      // schema column; -1 = record order
    bool sortAsc_    = true;
    char queryBuf_[256] = {0};

private:
    void recompute();
    uint32_t fieldOffset(size_t col) const { return colOffsets_[col]; }
    // Shared cell formatter: fields == nullptr reads record `strRec` from the
    // dbc; otherwise numerics come from `fields` (a duplicated row's deep copy)
    // while string offsets still resolve through record `strRec`.
    std::string cellTextImpl(uint32_t strRec, size_t col,
                             const std::vector<uint32_t>* fields) const;

    const TableSchema* schema_ = nullptr;
    const Dbc*         dbc_    = nullptr;

    std::vector<uint32_t>    colOffsets_;   // first field index per column
    std::vector<uint8_t>     emptyCols_;    // columnIsEmpty cache
    std::vector<std::string> colFilters_;   // per-column filter values

    std::vector<uint32_t> visible_;         // filtered+sorted record indices
    std::vector<AddedRow> added_;           // duplicated rows

    std::string pendingQuery_;              // last committed query text
    bool     dirty_      = false;
    uint64_t lastEditMs_ = 0;
    ClockFn  clock_;
    FkResolver fkResolver_;

    bool  tableReq_ = false; std::string tableReqName_;
    bool  fkPending_ = false; FkNav fkNav_;
};

} // namespace wf::editor
