#include "changeset.hpp"

#include <ctime>
#include <stdexcept>

#include "editor_bridge.hpp"   // reloadCommandFor

namespace wf {

std::string sqlQuote(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\0') throw std::invalid_argument("sqlQuote: embedded NUL");
        out.push_back(c);
        if (c == '\'') out.push_back('\'');   // '' is SQL's escaped quote
        // Backslash is MySQL's escape character (the output runs against the
        // mangos world DB): double it, or "World\Maps\..." corrupts and a
        // trailing backslash would escape the closing quote.
        if (c == '\\') out.push_back('\\');
    }
    out.push_back('\'');
    return out;
}

std::string diffUpdate(const std::string& table, const std::string& idCol,
                       const std::string& idVal, const Row& original, const Row& current) {
    std::string sets;
    for (const auto& [col, val] : current) {
        bool changed = true;                   // absent in `original` = changed
        for (const auto& [ocol, oval] : original)
            if (ocol == col) { changed = (oval != val); break; }
        if (!changed) continue;
        if (!sets.empty()) sets += ", ";
        sets += col; sets += " = "; sets += val;
    }
    if (sets.empty()) return {};               // nothing changed: no statement
    return "UPDATE " + table + " SET " + sets + " WHERE " + idCol + " = " + idVal + ";";
}

std::string deleteInsert(const std::string& table, const std::string& keyCol,
                         const std::string& keyVal, const std::vector<Row>& rows) {
    std::string out = "DELETE FROM " + table + " WHERE " + keyCol + " = " + keyVal + ";";
    if (rows.empty()) return out;              // no rows: remove the entity

    out += "\nINSERT INTO " + table + " (";
    const Row& first = rows.front();
    for (size_t i = 0; i < first.size(); ++i) {
        if (i) out += ", ";
        out += first[i].first;
    }
    out += ") VALUES";
    for (size_t r = 0; r < rows.size(); ++r) {
        out += (r ? ",\n(" : "\n(");
        const Row& row = rows[r];
        for (size_t i = 0; i < row.size(); ++i) {
            if (i) out += ", ";
            out += row[i].second;
        }
        out += ")";
    }
    out += ";";
    return out;
}

void Changeset::add(std::string sql, std::string description, std::string worldTable) {
    entries_.push_back({ std::move(sql), std::move(description), std::move(worldTable) });
}

std::set<std::string> Changeset::touchedTables() const {
    std::set<std::string> tables;
    for (const ChangeEntry& e : entries_)
        if (!e.worldTable.empty()) tables.insert(e.worldTable);
    return tables;
}

std::vector<std::string> Changeset::reloadCommands() const {
    std::vector<std::string> out;
    for (const std::string& table : touchedTables())   // sorted -> deterministic
        if (const char* cmd = reloadCommandFor(table)) out.push_back(cmd);
    return out;
}

std::string Changeset::serialize(std::string_view timestamp) const {
    std::string ts(timestamp);
    if (ts.empty()) {
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        gmtime_s(&tmv, &now);
#else
        gmtime_r(&now, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        ts = buf;
    }

    std::string out = "-- Changeset: " + name_ + "\n-- Generated: " + ts + "\n";
    for (const ChangeEntry& e : entries_) {
        if (!e.description.empty()) out += "-- " + e.description + "\n";
        out += e.sql;
        if (out.back() != '\n') out += "\n";
    }
    std::vector<std::string> reloads = reloadCommands();
    if (!reloads.empty()) {
        out += "-- Reload to apply live:\n";
        for (const std::string& r : reloads) out += "-- " + r + "\n";
    }
    return out;
}

} // namespace wf
