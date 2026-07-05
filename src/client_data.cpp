#include "client_data.hpp"

#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace wf {

std::vector<std::string> wowBaseArchiveNames() {
    return {
        "base.MPQ", "dbc.MPQ", "interface.MPQ", "misc.MPQ", "model.MPQ",
        "sound.MPQ", "speech.MPQ", "terrain.MPQ", "texture.MPQ", "wmo.MPQ",
        "patch.MPQ", "patch-2.MPQ",
    };
}

std::vector<std::string> wowLocaleArchiveTemplates() {
    return { "locale-{loc}.MPQ", "speech-{loc}.MPQ", "patch-{loc}.MPQ", "patch-{loc}-2.MPQ" };
}

namespace {
std::string replaceLoc(std::string s, const std::string& loc) {
    const std::string token = "{loc}";
    for (size_t p; (p = s.find(token)) != std::string::npos; )
        s.replace(p, token.size(), loc);
    return s;
}
} // namespace

std::vector<std::string> wowArchiveChain(const fs::path& dataDir, const std::string& locale) {
    std::vector<std::string> chain;
    for (const std::string& a : wowBaseArchiveNames())
        chain.push_back((dataDir / a).string());
    const fs::path locDir = dataDir / locale;
    for (const std::string& a : wowLocaleArchiveTemplates())
        chain.push_back((locDir / replaceLoc(a, locale)).string());
    return chain;
}

bool looksLikeDataDir(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const std::string& a : wowBaseArchiveNames())
        if (fs::exists(dir / a, ec)) return true;
    return false;
}

fs::path findDataDir(const fs::path& start) {
    std::error_code ec;
    fs::path dir = start;
    if (dir.empty()) dir = fs::current_path(ec);

    // Walk `dir` and a few of its parents; at each level try the dir itself
    // (we're already inside Data) and dir/Data (we're next to WoW.exe).
    for (int up = 0; up < 6 && !dir.empty(); ++up) {
        if (looksLikeDataDir(dir)) return dir;
        if (looksLikeDataDir(dir / "Data")) return dir / "Data";
        fs::path parent = dir.parent_path();
        if (parent == dir) break;   // reached filesystem root
        dir = parent;
    }
    return {};
}

std::string detectLocale(const fs::path& dataDir) {
    std::error_code ec;
    if (!fs::is_directory(dataDir, ec)) return {};
    for (const fs::directory_entry& e : fs::directory_iterator(dataDir, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        const std::string loc = e.path().filename().string();
        if (fs::exists(e.path() / ("locale-" + loc + ".MPQ"), ec)) return loc;
    }
    return {};
}

size_t mountWowClient(MpqManager& mpq, const fs::path& dataDir, const std::string& locale,
                      const std::function<void(const std::string&, bool)>& onMount) {
    std::error_code ec;
    size_t before = mpq.archiveCount();
    for (const std::string& path : wowArchiveChain(dataDir, locale)) {
        if (!fs::exists(path, ec)) continue;
        bool ok = mpq.addArchive(path);
        if (onMount) onMount(path, ok);
    }
    return mpq.archiveCount() - before;
}

fs::path overlayFilePath(const fs::path& overlayDir, const std::string& archivedPath) {
    // Archived paths use backslashes ("World\\Maps\\..."); map them to
    // directory separators so the overlay mirrors the archive tree on any
    // filesystem. u8path keeps non-ASCII bytes intact on Windows.
    std::string rel = archivedPath;
    for (char& c : rel)
        if (c == '\\') c = '/';
    return overlayDir / fs::u8path(rel);
}

bool readOverlayFile(const fs::path& overlayDir, const std::string& archivedPath,
                     std::vector<uint8_t>& out) {
    if (overlayDir.empty()) return false;
    std::error_code ec;
    const fs::path p = overlayFilePath(overlayDir, archivedPath);
    if (!fs::is_regular_file(p, ec)) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeOverlayFile(const fs::path& overlayDir, const std::string& archivedPath,
                      const std::vector<uint8_t>& bytes) {
    if (overlayDir.empty()) return false;
    std::error_code ec;
    const fs::path p = overlayFilePath(overlayDir, archivedPath);
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty())
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

} // namespace wf
