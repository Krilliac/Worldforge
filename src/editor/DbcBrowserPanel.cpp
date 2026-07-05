#include "editor/DbcBrowserPanel.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <regex>

#include "imgui.h"

namespace wf::editor {

namespace {

constexpr uint64_t kQueryDebounceMs = 150;   // like the asset browser filter

std::string lowerAscii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool icaseEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Strict integer parse (base 10, or 16 with an 0x/0X prefix); the whole token
// must consume. A numeric column term that fails this matches nothing.
bool parseI64(const std::string& s, long long& out) {
    if (s.empty()) return false;
    const char* p = s.c_str();
    size_t digits = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    const int base =
        (s.size() >= digits + 2 && s[digits] == '0' &&
         (s[digits + 1] == 'x' || s[digits + 1] == 'X')) ? 16 : 10;
    char* end = nullptr;
    out = std::strtoll(p, &end, base);
    return end && *end == '\0' && end != p;
}

// Strict float parse (whole token must consume).
bool parseF32(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0' && end != s.c_str();
}

float bitsToF32(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// First 4-byte field index of each schema column (LocStr spans 9 per element).
std::vector<uint32_t> columnOffsets(const TableSchema& schema) {
    std::vector<uint32_t> offs(schema.colCount);
    uint32_t off = 0;
    for (size_t c = 0; c < schema.colCount; ++c) {
        offs[c] = off;
        off += (uint32_t)columnFieldSpan(schema.cols[c]);
    }
    return offs;
}

// One QueryTerm compiled for the row loop: resolved column, pre-parsed numeric
// value, lowered needle / compiled regex. `impossible` marks a term that can
// never match (unknown column, invalid regex, non-numeric value on a numeric
// column) -- since terms AND together the whole result is then empty.
struct CompiledTerm {
    int         col = -1;             // -1 = bare (any Str/LocStr column)
    bool        numOk = false; long long i64 = 0;
    bool        f32Ok = false; float     f32 = 0.0f;
    std::string needleLower;
    std::regex  re;
    bool        useRegex   = false;
    bool        impossible = false;
};

CompiledTerm compileTerm(const TableSchema& schema, const QueryTerm& t) {
    CompiledTerm c;
    if (!t.column.empty()) {
        c.col = findColumn(schema, t.column);
        if (c.col < 0) { c.impossible = true; return c; }   // unknown column
    }
    c.numOk = parseI64(t.value, c.i64);
    c.f32Ok = parseF32(t.value, c.f32);
    c.needleLower = lowerAscii(t.value);
    if (t.isRegex) {
        try {
            c.re = std::regex(t.value, std::regex::icase);
            c.useRegex = true;
        } catch (const std::regex_error&) {
            c.impossible = true;   // invalid pattern matches NOTHING (no throw)
            return c;
        }
    }
    if (c.col >= 0) {              // bound numeric column needs a parsed number
        switch (schema.cols[(size_t)c.col].type) {
        case ColType::U32:
            if (!c.numOk || c.i64 < 0 || c.i64 > 0xFFFFFFFFll) c.impossible = true;
            break;
        case ColType::I32:
            if (!c.numOk) c.impossible = true;
            break;
        case ColType::F32:
            if (!c.f32Ok) c.impossible = true;
            break;
        default: break;            // string columns take any value
        }
    }
    return c;
}

bool matchString(const std::string& s, const CompiledTerm& t) {
    if (t.useRegex) return std::regex_search(s, t.re);
    return lowerAscii(s).find(t.needleLower) != std::string::npos;
}

// Does record `rec`'s column `col` match the term? Array columns match if ANY
// element matches; LocStr compares the enUS slot (first field of each element).
bool matchColumn(const Dbc& dbc, const TableSchema& schema,
                 const std::vector<uint32_t>& offs,
                 uint32_t rec, size_t col, const CompiledTerm& t) {
    const ColumnDef& cd = schema.cols[col];
    const uint32_t base = offs[col];
    for (uint8_t e = 0; e < cd.arrayLen; ++e) {
        switch (cd.type) {
        case ColType::U32:
            if (dbc.getU32(rec, base + e) == (uint32_t)t.i64) return true;
            break;
        case ColType::I32:
            if ((long long)(int32_t)dbc.getU32(rec, base + e) == t.i64) return true;
            break;
        case ColType::F32:
            if (bitsToF32(dbc.getU32(rec, base + e)) == t.f32) return true;
            break;
        case ColType::Str:
            if (matchString(dbc.getString(rec, base + e), t)) return true;
            break;
        case ColType::LocStr:
            if (matchString(dbc.getString(rec, base + e * 9u), t)) return true;
            break;
        }
    }
    return false;
}

} // namespace

// --- pure query layer --------------------------------------------------------

std::vector<QueryTerm> parseQuery(std::string_view text, bool regex) {
    std::vector<QueryTerm> terms;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && text[i] == ' ') ++i;
        if (i >= n) break;
        std::string column, value;
        bool inQuote = false, seenColon = false;
        for (; i < n && (inQuote || text[i] != ' '); ++i) {
            const char c = text[i];
            if (c == '"') { inQuote = !inQuote; continue; }   // quotes keep spaces
            if (c == ':' && !inQuote && !seenColon) {
                seenColon = true;                             // 'col:value' binding
                column = std::move(value);
                value.clear();
                continue;
            }
            value += c;
        }
        if (value.empty()) continue;   // 'name:' / bare "" -> term is ignored
        QueryTerm t;
        t.column  = seenColon ? std::move(column) : std::string();
        t.value   = std::move(value);
        t.isRegex = regex;
        terms.push_back(std::move(t));
    }
    return terms;
}

int findColumn(const TableSchema& schema, std::string_view name) {
    for (size_t c = 0; c < schema.colCount; ++c)
        if (icaseEquals(schema.cols[c].name, name)) return (int)c;
    return -1;
}

int schemaIdColumn(const TableSchema& schema) {
    for (size_t c = 0; c < schema.colCount; ++c)
        if (schema.cols[c].isId) return (int)c;
    return -1;
}

uint32_t nextFreeId(const Dbc& dbc, const TableSchema& schema) {
    const int idCol = schemaIdColumn(schema);
    if (idCol < 0) return 0;
    uint32_t off = 0;
    for (int c = 0; c < idCol; ++c) off += (uint32_t)columnFieldSpan(schema.cols[c]);
    uint32_t maxId = 0;
    for (uint32_t rec = 0; rec < dbc.recordCount(); ++rec)
        maxId = std::max(maxId, dbc.getU32(rec, off));
    return maxId + 1;
}

std::vector<uint32_t> filterRows(const Dbc& dbc, const TableSchema& schema,
                                 const std::vector<QueryTerm>& terms) {
    std::vector<uint32_t> out;
    const std::vector<uint32_t> offs = columnOffsets(schema);

    std::vector<CompiledTerm> compiled;
    compiled.reserve(terms.size());
    for (const QueryTerm& t : terms) {
        CompiledTerm c = compileTerm(schema, t);
        if (c.impossible) return out;   // AND semantics: one dead term kills all
        compiled.push_back(std::move(c));
    }

    for (uint32_t rec = 0; rec < dbc.recordCount(); ++rec) {
        bool all = true;
        for (const CompiledTerm& t : compiled) {
            bool hit = false;
            if (t.col >= 0) {
                hit = matchColumn(dbc, schema, offs, rec, (size_t)t.col, t);
            } else {
                // Bare term: any Str/LocStr column. A schema with no string
                // columns can never satisfy it.
                for (size_t c = 0; c < schema.colCount && !hit; ++c) {
                    const ColType ty = schema.cols[c].type;
                    if (ty == ColType::Str || ty == ColType::LocStr)
                        hit = matchColumn(dbc, schema, offs, rec, c, t);
                }
            }
            if (!hit) { all = false; break; }
        }
        if (all) out.push_back(rec);
    }
    return out;
}

void sortRows(const Dbc& dbc, const TableSchema& schema,
              std::vector<uint32_t>& rows, size_t col, bool ascending) {
    if (col >= schema.colCount) return;   // out-of-range: no-op
    const ColumnDef& cd = schema.cols[col];
    uint32_t base = 0;
    for (size_t c = 0; c < col; ++c) base += (uint32_t)columnFieldSpan(schema.cols[c]);

    std::vector<size_t> idx(rows.size());
    std::iota(idx.begin(), idx.end(), size_t{0});

    if (cd.type == ColType::Str || cd.type == ColType::LocStr) {
        // Case-insensitive lexicographic on the (enUS) string; keys built once.
        std::vector<std::string> keys;
        keys.reserve(rows.size());
        for (uint32_t r : rows) keys.push_back(lowerAscii(dbc.getString(r, base)));
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return ascending ? keys[a] < keys[b] : keys[b] < keys[a];
        });
    } else {
        // Numeric per ColType: U32 unsigned, I32 signed, F32 float. A double
        // key represents all three exactly (u32/i32 fit in 53 mantissa bits).
        std::vector<double> keys;
        keys.reserve(rows.size());
        for (uint32_t r : rows) {
            const uint32_t v = dbc.getU32(r, base);
            double k;
            switch (cd.type) {
            case ColType::I32: k = (double)(int32_t)v; break;
            case ColType::F32: k = (double)bitsToF32(v); break;
            default:           k = (double)v; break;
            }
            keys.push_back(k);
        }
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return ascending ? keys[a] < keys[b] : keys[b] < keys[a];
        });
    }

    std::vector<uint32_t> sorted;
    sorted.reserve(rows.size());
    for (size_t i : idx) sorted.push_back(rows[i]);
    rows = std::move(sorted);
}

bool columnIsEmpty(const Dbc& dbc, const TableSchema& schema, size_t col) {
    if (col >= schema.colCount) return true;
    const ColumnDef& cd = schema.cols[col];
    uint32_t base = 0;
    for (size_t c = 0; c < col; ++c) base += (uint32_t)columnFieldSpan(schema.cols[c]);

    for (uint32_t rec = 0; rec < dbc.recordCount(); ++rec) {
        for (uint8_t e = 0; e < cd.arrayLen; ++e) {
            switch (cd.type) {
            case ColType::Str:
                if (!dbc.getString(rec, base + e).empty()) return false;
                break;
            case ColType::LocStr:
                for (uint32_t slot = 0; slot < 8; ++slot)   // 8 locale strings
                    if (!dbc.getString(rec, base + e * 9u + slot).empty()) return false;
                break;
            default:
                if (dbc.getU32(rec, base + e) != 0) return false;
                break;
            }
        }
    }
    return true;
}

// --- panel ---------------------------------------------------------------

DbcBrowserPanel::DbcBrowserPanel() {
    clock_ = [] {
        using namespace std::chrono;
        return (uint64_t)duration_cast<milliseconds>(
                   steady_clock::now().time_since_epoch()).count();
    };
}

void DbcBrowserPanel::setClock(ClockFn clock) {
    if (clock) clock_ = std::move(clock);
}

void DbcBrowserPanel::setFkResolver(FkResolver resolver) {
    fkResolver_ = std::move(resolver);
}

void DbcBrowserPanel::setTable(const TableSchema* schema, const Dbc* dbc) {
    const bool attached = schema != nullptr && dbc != nullptr;
    schema_ = attached ? schema : nullptr;
    dbc_    = attached ? dbc : nullptr;

    colOffsets_.clear();
    emptyCols_.clear();
    colFilters_.clear();
    visible_.clear();
    added_.clear();
    pendingQuery_.clear();
    queryBuf_[0] = 0;
    dirty_   = false;
    sortCol_ = -1;
    sortAsc_ = true;
    if (!schema_) return;

    colOffsets_ = columnOffsets(*schema_);
    colFilters_.assign(schema_->colCount, std::string());
    emptyCols_.resize(schema_->colCount);
    for (size_t c = 0; c < schema_->colCount; ++c)
        emptyCols_[c] = columnIsEmpty(*dbc_, *schema_, c) ? 1 : 0;
    recompute();                     // visible_ = every record, record order
}

void DbcBrowserPanel::setQueryText(const std::string& text) {
    if (text == pendingQuery_) return;             // identical text is a no-op
    pendingQuery_ = text;
    std::snprintf(queryBuf_, sizeof queryBuf_, "%s", text.c_str());
    dirty_ = true;
    lastEditMs_ = clock_();
}

void DbcBrowserPanel::setColumnFilter(size_t col, const std::string& value) {
    if (col >= colFilters_.size() || colFilters_[col] == value) return;
    colFilters_[col] = value;
    dirty_ = true;
    lastEditMs_ = clock_();
}

bool DbcBrowserPanel::refresh() {
    if (!dirty_) return false;
    if (clock_() - lastEditMs_ < kQueryDebounceMs) return false;   // debounce
    recompute();
    return true;
}

void DbcBrowserPanel::recompute() {
    dirty_ = false;
    visible_.clear();
    if (!schema_ || !dbc_) return;

    std::vector<QueryTerm> terms = parseQuery(pendingQuery_, regexMode_);
    for (size_t c = 0; c < colFilters_.size(); ++c) {
        if (colFilters_[c].empty()) continue;      // "" = no filter on the column
        QueryTerm t;
        t.column  = schema_->cols[c].name;
        t.value   = colFilters_[c];
        t.isRegex = regexMode_;
        terms.push_back(std::move(t));
    }
    visible_ = filterRows(*dbc_, *schema_, terms);
    if (sortCol_ >= 0)
        sortRows(*dbc_, *schema_, visible_, (size_t)sortCol_, sortAsc_);
}

void DbcBrowserPanel::sortBy(int col, bool ascending) {
    sortCol_ = col;
    sortAsc_ = ascending;
    if (schema_ && dbc_ && col >= 0)
        sortRows(*dbc_, *schema_, visible_, (size_t)col, ascending);
}

bool DbcBrowserPanel::takeTableRequest(std::string& outName) {
    if (!tableReq_) return false;
    outName = tableReqName_;
    tableReq_ = false;
    return true;
}

void DbcBrowserPanel::requestTable(std::string dbcName) {
    tableReq_ = true;
    tableReqName_ = std::move(dbcName);
}

bool DbcBrowserPanel::takeFkNav(FkNav& out) {
    if (!fkPending_) return false;
    out = fkNav_;
    fkPending_ = false;
    return true;
}

void DbcBrowserPanel::clickFk(uint32_t rec, size_t col) {
    if (!schema_ || !dbc_ || col >= schema_->colCount) return;
    const ColumnDef& cd = schema_->cols[col];
    if (!cd.fkTable) return;                       // not a foreign-key column
    if (rec >= dbc_->recordCount()) return;
    fkPending_ = true;
    fkNav_.table = cd.fkTable;
    fkNav_.id    = dbc_->getU32(rec, fieldOffset(col));   // first array element
}

bool DbcBrowserPanel::canDuplicate() const {
    return schema_ != nullptr && dbc_ != nullptr && schemaIdColumn(*schema_) >= 0;
}

int64_t DbcBrowserPanel::duplicateRow(uint32_t rec) {
    if (!canDuplicate() || rec >= dbc_->recordCount()) return -1;
    const int idCol = schemaIdColumn(*schema_);
    const uint32_t idField = fieldOffset((size_t)idCol);

    // Next free id over the dbc AND rows already duplicated this session.
    uint32_t newId = nextFreeId(*dbc_, *schema_);
    for (const AddedRow& a : added_)
        if (idField < a.fields.size())
            newId = std::max(newId, a.fields[idField] + 1);

    // DEEP copy: every 4-byte field is copied by value; the new row never
    // aliases the source record.
    AddedRow row;
    row.srcRec = rec;
    row.fields.resize(dbc_->fieldCount());
    for (uint32_t f = 0; f < dbc_->fieldCount(); ++f)
        row.fields[f] = dbc_->getU32(rec, f);
    row.fields[idField] = newId;
    added_.push_back(std::move(row));
    return (int64_t)newId;
}

std::string DbcBrowserPanel::cellTextImpl(uint32_t strRec, size_t col,
                                          const std::vector<uint32_t>* fields) const {
    if (!schema_ || !dbc_ || col >= schema_->colCount) return std::string();
    const ColumnDef& cd = schema_->cols[col];
    const uint32_t base = fieldOffset(col);
    auto fieldVal = [&](uint32_t f) -> uint32_t {
        if (fields) return f < fields->size() ? (*fields)[f] : 0u;
        return dbc_->getU32(strRec, f);
    };

    std::string out;
    char buf[48];
    for (uint8_t e = 0; e < cd.arrayLen; ++e) {
        if (e) out += ", ";
        switch (cd.type) {
        case ColType::Str:
            out += dbc_->getString(strRec, base + e);
            break;
        case ColType::LocStr:
            out += dbc_->getString(strRec, base + e * 9u);   // enUS slot
            break;
        case ColType::F32:
            std::snprintf(buf, sizeof buf, "%g", bitsToF32(fieldVal(base + e)));
            out += buf;
            break;
        default: {   // U32 / I32
            const uint32_t v = fieldVal(base + e);
            if (hexDisplay_)
                std::snprintf(buf, sizeof buf, "0x%X", v);
            else if (cd.type == ColType::I32)
                std::snprintf(buf, sizeof buf, "%d", (int32_t)v);
            else
                std::snprintf(buf, sizeof buf, "%u", v);
            out += buf;
            if (cd.fkTable && fkResolver_) {
                const std::string name = fkResolver_(cd.fkTable, v);
                if (!name.empty()) { out += " ("; out += name; out += ')'; }
            }
            break;
        }
        }
    }
    return out;
}

std::string DbcBrowserPanel::cellText(uint32_t rec, size_t col) const {
    if (!dbc_ || rec >= dbc_->recordCount()) return std::string();
    return cellTextImpl(rec, col, nullptr);
}

std::string DbcBrowserPanel::addedCellText(size_t addedIdx, size_t col) const {
    if (addedIdx >= added_.size()) return std::string();
    const AddedRow& a = added_[addedIdx];
    // Fields come from the deep copy; string offsets resolve through the source
    // record (the copied offset is identical, so the text matches).
    return cellTextImpl(a.srcRec, col, &a.fields);
}

bool DbcBrowserPanel::columnEmpty(size_t col) const {
    return col < emptyCols_.size() && emptyCols_[col] != 0;
}

// --- ImGui layer -----------------------------------------------------------

void DbcBrowserPanel::draw() {
    ImGui::Begin("DBC Browser");

    // --- table picker over the schema registry ------------------------------
    const char* current = schema_ ? schema_->dbcName : "<none>";
    if (ImGui::BeginCombo("Table", current)) {
        for (size_t i = 0; i < schemaCount(); ++i) {
            const TableSchema& s = schemaAt(i);
            const bool sel = (schema_ == &s);
            if (ImGui::Selectable(s.dbcName, sel) && !sel)
                requestTable(s.dbcName);       // host loads it, calls setTable
        }
        ImGui::EndCombo();
    }

    if (ImGui::InputText("Query", queryBuf_, sizeof queryBuf_))
        setQueryText(queryBuf_);               // debounced recompute
    ImGui::Checkbox("Hex", &hexDisplay_);      // display-only: no recompute
    ImGui::SameLine();
    ImGui::Checkbox("Hide empty", &hideEmpty_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Regex", &regexMode_))
        recompute();                           // structural: no keystroke wait

    refresh();

    if (!schema_ || !dbc_) {
        ImGui::TextUnformatted("No table loaded.");
        ImGui::End();
        return;
    }

    // Visible schema columns after the hide-empty toggle.
    std::vector<int> cols;
    for (size_t c = 0; c < schema_->colCount; ++c)
        if (!hideEmpty_ || !columnEmpty(c)) cols.push_back((int)c);

    ImGui::Text("%d of %u rows (%d added)", (int)visible_.size(),
                dbc_->recordCount(), (int)added_.size());

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Sortable;
    if (!cols.empty() &&
        ImGui::BeginTable("dbcgrid", (int)cols.size(), flags, ImVec2(0, 400))) {
        for (int ci : cols)   // user_id carries the SCHEMA column index
            ImGui::TableSetupColumn(schema_->cols[(size_t)ci].name, 0, 0.0f, (ImGuiID)ci);
        ImGui::TableSetupScrollFreeze(0, 1);

        // Custom header row: sort handle + '?' badge tooltip on unverified
        // columns + the per-column filter popover (right-click the header).
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int n = 0; n < (int)cols.size(); ++n) {
            ImGui::TableSetColumnIndex(n);
            const size_t ci = (size_t)cols[(size_t)n];
            const ColumnDef& cd = schema_->cols[ci];
            ImGui::PushID(n);
            ImGui::TableHeader(cd.name);
            if (!cd.verified && ImGui::IsItemHovered())
                ImGui::SetTooltip("? unverified column -- layout not yet confirmed");
            if (ImGui::BeginPopupContextItem("colfilter")) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "%s", colFilters_[ci].c_str());
                if (ImGui::InputText("Filter", buf, sizeof buf))
                    setColumnFilter(ci, buf);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        // Header clicks -> the pure sort layer (record order when cleared).
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsDirty) {
                if (ss->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& s0 = ss->Specs[0];
                    sortBy((int)s0.ColumnUserID,
                           s0.SortDirection != ImGuiSortDirection_Descending);
                } else {
                    sortCol_ = -1;
                    recompute();
                }
                ss->SpecsDirty = false;
            }
        }

        // Only visible rows materialize; duplicated rows append after the grid.
        const int liveRows = (int)visible_.size();
        ImGuiListClipper clipper;
        clipper.Begin(liveRows + (int)added_.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow();
                const bool isAdded = row >= liveRows;
                const uint32_t rec = isAdded ? 0 : visible_[(size_t)row];
                ImGui::PushID(row);
                for (int n = 0; n < (int)cols.size(); ++n) {
                    ImGui::TableSetColumnIndex(n);
                    const size_t ci = (size_t)cols[(size_t)n];
                    const std::string txt =
                        isAdded ? addedCellText((size_t)(row - liveRows), ci)
                                : cellText(rec, ci);
                    if (!isAdded && schema_->cols[ci].fkTable) {
                        ImGui::PushID((int)ci);
                        if (ImGui::Selectable(txt.c_str()))
                            clickFk(rec, ci);   // click-through navigation
                        ImGui::PopID();
                    } else {
                        ImGui::TextUnformatted(txt.c_str());
                    }
                    if (n == 0 && ImGui::BeginPopupContextItem("row")) {
                        if (ImGui::MenuItem("Duplicate", nullptr, false,
                                            canDuplicate() && !isAdded))
                            duplicateRow(rec);
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    } else if (cols.empty()) {
        ImGui::TextUnformatted("All columns are empty.");
    }

    ImGui::End();
}

} // namespace wf::editor
