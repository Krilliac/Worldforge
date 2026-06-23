#include "client_data.hpp"

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

} // namespace wf
