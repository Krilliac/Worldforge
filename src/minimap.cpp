#include "minimap.hpp"

#include <cctype>

namespace wf {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

// Trim ASCII whitespace (incl. CR from CRLF files) from both ends.
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string pad2(int v) {
    if (v < 0) v = 0;
    std::string s = std::to_string(v);
    return s.size() >= 2 ? s : std::string(2 - s.size(), '0') + s;
}

}  // namespace

MinimapIndex MinimapIndex::parse(const std::string& text) {
    MinimapIndex idx;
    std::string dir;                       // current `dir:` section, if any
    size_t i = 0;
    while (i < text.size()) {
        size_t nl = text.find('\n', i);
        std::string line = trim(text.substr(i, nl == std::string::npos ? std::string::npos : nl - i));
        i = (nl == std::string::npos) ? text.size() : nl + 1;
        if (line.empty()) continue;

        if (line.size() >= 4 && lower(line.substr(0, 4)) == "dir:") {
            dir = trim(line.substr(4));
            continue;
        }
        // Data lines are tab-delimited ("<logical>\t<stored>"); a line with no
        // tab is not an entry (e.g. stray prose) and is skipped. Map names and
        // md5 stored names never contain spaces, so tab is the reliable split.
        size_t sep = line.find('\t');
        if (sep == std::string::npos) continue;
        std::string logical = trim(line.substr(0, sep));
        std::string stored  = trim(line.substr(sep + 1));
        if (logical.empty() || stored.empty()) continue;

        // A bare "map<xx>_<yy>.blp" left column is prefixed by the dir section.
        if (logical.find('\\') == std::string::npos &&
            logical.find('/') == std::string::npos && !dir.empty())
            logical = dir + "\\" + logical;

        // Normalise separators to backslash, lowercase for a stable key.
        for (char& c : logical) if (c == '/') c = '\\';
        idx.byLogical_[lower(logical)] = stored;
    }
    return idx;
}

std::string MinimapIndex::tileKey(const std::string& map, int x, int y) {
    return lower(map + "\\map" + pad2(x) + "_" + pad2(y) + ".blp");
}

std::string MinimapIndex::storedPath(const std::string& storedName) {
    return "textures\\Minimap\\" + storedName;
}

std::string MinimapIndex::resolve(const std::string& logicalKey) const {
    auto it = byLogical_.find(lower(logicalKey));
    return it == byLogical_.end() ? std::string() : it->second;
}

std::string MinimapIndex::tile(const std::string& map, int x, int y) const {
    return resolve(tileKey(map, x, y));
}

}  // namespace wf
