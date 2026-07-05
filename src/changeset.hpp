#pragma once
// ---------------------------------------------------------------------------
// Changeset: an ordered batch of world-DB edits the editor wants to make live,
// plus the Keira3-style dual SQL generators that produce its statements at the
// string level:
//   * diffUpdate   -- an UPDATE containing ONLY the changed columns, so the
//                     emitted script documents exactly what the edit touched;
//   * deleteInsert -- an atomic DELETE-by-key + one bulk INSERT, so multi-row
//                     entities (loot tables, vendor lists) can never half-apply.
// Everything here is pure string work with no DB dependency, unit-tested.
// BridgeClient::applyChangeset ships each entry over the bridge as an
// EDITOR_SQL_APPLY, followed by the deduped ".reload" commands
// (reloadCommandFor in editor_bridge) that make the edits live.
// ---------------------------------------------------------------------------
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wf {

// (column, value) pairs. Values are SQL fragments rendered by the caller:
// numbers directly ("5"), strings through sqlQuote().
using Row = std::vector<std::pair<std::string, std::string>>;

// Quote a string value for SQL: wraps it in single quotes, doubling embedded
// quotes. Throws std::invalid_argument on an embedded NUL -- never valid in
// the world DB's text columns, and silently dropping it would splice the
// statement.
std::string sqlQuote(std::string_view value);

// "UPDATE table SET <only the changed columns> WHERE idCol = idVal;" comparing
// `current` against `original` column-by-column. A column present only in
// `current` counts as changed. Returns "" when nothing changed (no statement
// to run). Values must be pre-rendered (see Row).
std::string diffUpdate(const std::string& table, const std::string& idCol,
                       const std::string& idVal, const Row& original, const Row& current);

// "DELETE FROM table WHERE keyCol = keyVal;" followed by one multi-row INSERT
// of `rows` -- delete + reinsert as a unit so a multi-row entity is replaced
// atomically. Column order comes from the first row. With no rows the result
// is just the DELETE (remove the whole entity).
std::string deleteInsert(const std::string& table, const std::string& keyCol,
                         const std::string& keyVal, const std::vector<Row>& rows);

// One world-DB edit inside a changeset.
struct ChangeEntry {
    std::string sql;          // a complete statement (or deleteInsert's pair)
    std::string description;  // human note, serialised as a leading comment
    std::string worldTable;   // the table touched (drives the reload suggestion)
};

class Changeset {
public:
    Changeset() = default;
    explicit Changeset(std::string name) : name_(std::move(name)) {}

    void add(std::string sql, std::string description, std::string worldTable);

    const std::string&              name()    const { return name_; }
    const std::vector<ChangeEntry>& entries() const { return entries_; }
    bool                            empty()   const { return entries_.empty(); }

    // Every distinct worldTable across the entries (sorted).
    std::set<std::string> touchedTables() const;

    // The deduped ".reload" commands for the touched tables, in table order
    // (via reloadCommandFor); tables needing no reload contribute nothing.
    std::vector<std::string> reloadCommands() const;

    // The whole changeset as a runnable .sql script: a header comment with the
    // name + timestamp, each entry's description as a comment above its SQL,
    // and the reload suggestions as trailing comments. Pass a fixed `timestamp`
    // for deterministic output; empty = current UTC time.
    std::string serialize(std::string_view timestamp = {}) const;

private:
    std::string              name_;
    std::vector<ChangeEntry> entries_;
};

} // namespace wf
