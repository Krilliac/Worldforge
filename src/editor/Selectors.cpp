#include "editor/Selectors.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "dbc_index.hpp"   // AreaIndex / MapIndex (adapters only)
#include "imgui.h"

namespace wf::editor {

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Strict decimal parse for the exact-id ranking pass (whole token, digits only).
bool parseId(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (!end || *end != '\0' || v > 0xFFFFFFFFull) return false;
    out = (uint32_t)v;
    return true;
}

} // namespace

// ---- enum combo -------------------------------------------------------------

const char* enumLabel(int32_t v, const Option* opts, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (opts[i].value == v) return opts[i].label;
    return nullptr;
}

bool EnumCombo(const char* id, int32_t* v, const Option* opts, size_t count) {
    const char* label = enumLabel(*v, opts, count);
    char fallback[32];
    if (!label) {   // unlisted value: show the raw number rather than lying
        std::snprintf(fallback, sizeof fallback, "%d", *v);
        label = fallback;
    }
    bool changed = false;
    if (ImGui::BeginCombo(id, label)) {
        for (size_t i = 0; i < count; ++i) {
            const bool sel = (opts[i].value == *v);
            if (ImGui::Selectable(opts[i].label, sel) && !sel) {
                *v = opts[i].value;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// ---- flags popup ------------------------------------------------------------

std::string flagsSummary(uint32_t mask, const FlagBit* bits, size_t count) {
    if (mask == 0) return "None";
    std::string out;
    uint32_t rest = mask;
    for (size_t i = 0; i < count; ++i) {
        if (bits[i].bit == 0 || (mask & bits[i].bit) != bits[i].bit) continue;
        if (!out.empty()) out += '|';
        out += bits[i].label;
        rest &= ~bits[i].bit;
    }
    if (rest != 0) {   // bits no FlagBit names: keep them visible as hex
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%X", rest);
        if (!out.empty()) out += '|';
        out += buf;
    }
    return out;
}

bool FlagsPopup(const char* id, uint32_t* mask, const FlagBit* bits, size_t count) {
    bool changed = false;
    ImGui::PushID(id);
    const std::string summary = flagsSummary(*mask, bits, count);
    if (ImGui::Button(summary.c_str())) ImGui::OpenPopup("flags");
    if (ImGui::BeginPopup("flags")) {
        for (size_t i = 0; i < count; ++i) {
            bool on = (*mask & bits[i].bit) == bits[i].bit && bits[i].bit != 0;
            if (ImGui::Checkbox(bits[i].label, &on)) {
                if (on) *mask |= bits[i].bit;
                else    *mask &= ~bits[i].bit;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// ---- search picker ----------------------------------------------------------

std::vector<std::pair<uint32_t, std::string>>
rankMatches(const SearchSource& src, const std::string& needle, size_t maxResults) {
    std::vector<std::pair<uint32_t, std::string>> out;
    if (!src.count || !src.at || maxResults == 0) return out;
    const size_t n = src.count();

    uint32_t idNeedle = 0;
    const bool hasId = parseId(needle, idNeedle);
    const std::string lowered = lowerAscii(needle);

    // Pass 1: exact-id hits, in source order.
    if (hasId) {
        for (size_t i = 0; i < n && out.size() < maxResults; ++i) {
            std::pair<uint32_t, std::string> e = src.at(i);
            if (e.first == idNeedle) out.push_back(std::move(e));
        }
    }
    // Pass 2: case-insensitive name substrings (skipping pass-1 hits), stable.
    for (size_t i = 0; i < n && out.size() < maxResults; ++i) {
        std::pair<uint32_t, std::string> e = src.at(i);
        if (hasId && e.first == idNeedle) continue;   // already ranked first
        if (lowered.empty() ||
            lowerAscii(e.second).find(lowered) != std::string::npos)
            out.push_back(std::move(e));
    }
    return out;
}

SearchSource makeSearchSource(std::vector<std::pair<uint32_t, std::string>> items) {
    auto shared = std::make_shared<std::vector<std::pair<uint32_t, std::string>>>(
        std::move(items));
    SearchSource s;
    s.count = [shared] { return shared->size(); };
    s.at    = [shared](size_t i) { return (*shared)[i]; };
    return s;
}

SearchSource areaSearchSource(const AreaIndex& areas, std::vector<uint32_t> ids) {
    auto shared = std::make_shared<std::vector<uint32_t>>(std::move(ids));
    const AreaIndex* idx = &areas;    // caller keeps the index alive
    SearchSource s;
    s.count = [shared] { return shared->size(); };
    s.at    = [shared, idx](size_t i) {
        const uint32_t id = (*shared)[i];
        return std::make_pair(id, idx->fullName(id));
    };
    return s;
}

SearchSource mapSearchSource(const MapIndex& maps, std::vector<uint32_t> ids) {
    auto shared = std::make_shared<std::vector<uint32_t>>(std::move(ids));
    const MapIndex* idx = &maps;
    SearchSource s;
    s.count = [shared] { return shared->size(); };
    s.at    = [shared, idx](size_t i) {
        const uint32_t id = (*shared)[i];
        const MapEntry* m = idx->find(id);
        return std::make_pair(id, m ? m->name : std::string());
    };
    return s;
}

void SearchPicker::pick(uint32_t id) {
    picked_ = id;
    pickedPending_ = true;
}

bool SearchPicker::takePicked(uint32_t& outId) {
    if (!pickedPending_) return false;
    outId = picked_;
    pickedPending_ = false;
    return true;
}

bool SearchPicker::draw(const char* id, const SearchSource& src) {
    if (openReq_) {
        ImGui::OpenPopup(id);
        openReq_ = false;
        query_[0] = 0;
    }
    bool pickedNow = false;
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Search", query_, sizeof query_);
        const std::vector<std::pair<uint32_t, std::string>> matches =
            rankMatches(src, query_, maxResults_);

        ImGui::BeginChild("results", ImVec2(360, 240), true);
        ImGuiListClipper clipper;   // only visible result rows materialize
        clipper.Begin((int)matches.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& [mid, name] = matches[(size_t)row];
                char label[320];
                std::snprintf(label, sizeof label, "%u  %s", mid, name.c_str());
                ImGui::PushID(row);
                if (ImGui::Selectable(label)) {
                    pick(mid);
                    pickedNow = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return pickedNow;
}

} // namespace wf::editor
