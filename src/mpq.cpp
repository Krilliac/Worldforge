#include "mpq.hpp"
#include <StormLib.h>

#include <cstdio>

namespace wf {

bool writeMpqArchive(const std::string& path,
                     const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::remove(path.c_str());   // overwrite: SFileCreateArchive won't clobber

    HANDLE hMpq = nullptr;
    // Vanilla 1.12 uses MPQ format v1. Hash table sized to comfortably hold the
    // files plus the (listfile).
    DWORD maxFiles = static_cast<DWORD>(files.size() + 2);
    if (!SFileCreateArchive(path.c_str(),
                            MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE,
                            maxFiles, &hMpq))
        return false;

    bool ok = true;
    for (const auto& f : files) {
        const std::string& name = f.first;
        const std::vector<uint8_t>& data = f.second;
        HANDLE hFile = nullptr;
        if (!SFileCreateFile(hMpq, name.c_str(), 0, static_cast<DWORD>(data.size()),
                             0, MPQ_FILE_COMPRESS, &hFile)) {
            ok = false; break;
        }
        if (!SFileWriteFile(hFile, data.data(), static_cast<DWORD>(data.size()),
                            MPQ_COMPRESSION_ZLIB)) {
            SFileFinishFile(hFile); ok = false; break;
        }
        SFileFinishFile(hFile);
    }

    SFileCloseArchive(hMpq);
    if (!ok) std::remove(path.c_str());
    return ok;
}

MpqManager::~MpqManager() {
    for (void* h : handles_) {
        if (h) SFileCloseArchive(h);
    }
}

bool MpqManager::addArchive(const std::string& path) {
    HANDLE h = nullptr;
    // MPQ_OPEN_READ_ONLY guarantees we never write back to the client's data.
    if (!SFileOpenArchive(path.c_str(), 0, MPQ_OPEN_READ_ONLY, &h)) {
        return false;
    }
    handles_.push_back(h);
    return true;
}

bool MpqManager::contains(const std::string& archivedPath) const {
    for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
        if (SFileHasFile(*it, archivedPath.c_str())) return true;
    }
    return false;
}

bool MpqManager::readFile(const std::string& archivedPath,
                          std::vector<uint8_t>& out) const {
    // Search patches first (back of vector), then base archives.
    for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
        HANDLE hFile = nullptr;
        if (!SFileOpenFileEx(*it, archivedPath.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
            continue;

        DWORD high = 0;
        DWORD low  = SFileGetFileSize(hFile, &high);
        if (low == SFILE_INVALID_SIZE) {
            SFileCloseFile(hFile);
            continue;
        }

        const uint64_t total = (static_cast<uint64_t>(high) << 32) | low;
        out.resize(static_cast<size_t>(total));

        DWORD read = 0;
        // StormLib may return FALSE at EOF even on a complete read, so trust the
        // byte count, not the boolean.
        SFileReadFile(hFile, out.data(), low, &read, nullptr);
        SFileCloseFile(hFile);

        if (read == low) return true;

        out.clear();
        return false; // found but truncated: don't silently fall back to a base copy
    }
    return false;
}

} // namespace wf
