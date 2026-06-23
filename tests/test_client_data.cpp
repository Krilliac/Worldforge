#include "test.hpp"
#include "client_data.hpp"
#include "mpq.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace wf;

namespace {
// A real (tiny) MPQ on disk so MpqManager::addArchive actually opens it.
bool writeEmptyMpq(const fs::path& p) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> one = {
        { "placeholder.txt", { 'x' } }
    };
    return writeMpqArchive(p.string(), one);
}
} // namespace

void test_client_data() {
    std::printf("[client_data]\n");

    // --- wowArchiveChain: pure ordering / substitution (no filesystem) -------
    auto chain = wowArchiveChain("DATA", "enUS");
    // 12 base + 4 locale archives, base first.
    CHECK(chain.size() == wowBaseArchiveNames().size() + wowLocaleArchiveTemplates().size());
    CHECK(chain.front().find("base.MPQ") != std::string::npos);
    // patch.MPQ comes after base.MPQ (patches win = mounted later).
    size_t iBase = std::string::npos, iPatch = std::string::npos;
    for (size_t i = 0; i < chain.size(); ++i) {
        if (chain[i].find("base.MPQ")  != std::string::npos) iBase  = i;
        if (chain[i].find("patch.MPQ") != std::string::npos && iPatch == std::string::npos) iPatch = i;
    }
    CHECK(iBase != std::string::npos && iPatch != std::string::npos && iBase < iPatch);
    // Locale archives are last and carry the substituted locale + locale subdir.
    CHECK(chain.back().find("enUS") != std::string::npos);
    CHECK(chain.back().find("{loc}") == std::string::npos);
    bool sawLocaleArchive = false;
    for (const std::string& c : chain)
        if (c.find("locale-enUS.MPQ") != std::string::npos) sawLocaleArchive = true;
    CHECK(sawLocaleArchive);

    // A different locale substitutes everywhere.
    auto deDE = wowArchiveChain("DATA", "deDE");
    bool sawDe = false;
    for (const std::string& c : deDE)
        if (c.find("patch-deDE-2.MPQ") != std::string::npos) sawDe = true;
    CHECK(sawDe);

    // --- filesystem: build a fake install, detect + mount it -----------------
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / "wforge_client_test";
    fs::remove_all(root, ec);
    fs::create_directories(root / "Data" / "enUS", ec);
    // A WoW.exe sitting next to Data (detection cue + realism).
    { std::FILE* f = std::fopen((root / "WoW.exe").string().c_str(), "wb"); if (f) std::fclose(f); }

    CHECK(writeEmptyMpq(root / "Data" / "base.MPQ"));
    CHECK(writeEmptyMpq(root / "Data" / "dbc.MPQ"));
    CHECK(writeEmptyMpq(root / "Data" / "enUS" / "locale-enUS.MPQ"));

    // looksLikeDataDir: Data yes, the install root no.
    CHECK(looksLikeDataDir(root / "Data"));
    CHECK(!looksLikeDataDir(root));

    // findDataDir from the install root (next to WoW.exe) and from inside Data.
    CHECK(findDataDir(root) == root / "Data");
    CHECK(findDataDir(root / "Data") == root / "Data");
    // ...and from a deeper subdir, walking up to the install.
    CHECK(findDataDir(root / "Data" / "enUS") == root / "Data");
    CHECK(findDataDir(fs::temp_directory_path(ec)).empty() ||
          findDataDir(fs::temp_directory_path(ec)) != (root / "Data"));  // unrelated dir

    // detectLocale finds enUS by its locale-*.MPQ.
    CHECK(detectLocale(root / "Data") == "enUS");

    // mountWowClient opens exactly the three archives that exist, in order.
    {
        MpqManager mpq;
        size_t opened = mountWowClient(mpq, root / "Data", "enUS");
        CHECK(opened == 3);
        CHECK(mpq.archiveCount() == 3);
        // The mounted chain can read a file from the base archive.
        std::vector<uint8_t> buf;
        CHECK(mpq.readFile("placeholder.txt", buf) && !buf.empty());
    }

    fs::remove_all(root, ec);
}
