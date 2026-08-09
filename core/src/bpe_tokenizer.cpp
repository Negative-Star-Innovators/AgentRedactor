#include "bpe_tokenizer.h"
#include "utils.h"
#include "logging.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <limits>

using json = nlohmann::json;

namespace AgentRedactor {

BPETokenizer::BPETokenizer(const std::filesystem::path& tokenizerPath)
    : tokenizerPath_(tokenizerPath) {
}

bool BPETokenizer::Initialize() {
    if (initialized_) return true;
    LOG(L"[BPETokenizer] Initializing from: " + tokenizerPath_.wstring());
    if (!LoadTokenizerJson()) {
        LOG(L"[BPETokenizer] ERROR: Failed to load tokenizer.json");
        return false;
    }
    initialized_ = true;
    LOGF(L"[BPETokenizer] Initialized. Vocab size: %zu", tokenToId_.size());
    return true;
}

bool BPETokenizer::LoadTokenizerJson() {
    auto path = tokenizerPath_ / L"tokenizer.json";
    if (!Utils::FileExists(path)) {
        LOG(L"[BPETokenizer] tokenizer.json not found at: " + path.wstring());
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG(L"[BPETokenizer] Failed to open tokenizer.json");
        return false;
    }
    try {
        json tokenizerJson;
        file >> tokenizerJson;
        file.close();
        if (!tokenizerJson.contains("model")) {
            LOG(L"[BPETokenizer] Invalid tokenizer.json: missing 'model'");
            return false;
        }
        auto& model = tokenizerJson["model"];
        if (!model.contains("vocab")) {
            LOG(L"[BPETokenizer] Invalid tokenizer.json: missing 'vocab'");
            return false;
        }
        LOG(L"[BPETokenizer] Loading vocabulary...");
        for (auto& [token, id] : model["vocab"].items()) {
            int64_t tokenId = id.get<int64_t>();
            tokenToId_[token] = tokenId;
            idToToken_[tokenId] = token;
        }
        LOGF(L"[BPETokenizer] Loaded %zu vocab entries", tokenToId_.size());
        if (!model.contains("merges")) {
            LOG(L"[BPETokenizer] Invalid tokenizer.json: missing 'merges'");
            return false;
        }
        LOG(L"[BPETokenizer] Loading merges...");
        const auto& merges = model["merges"];
        for (size_t i = 0; i < merges.size(); ++i) {
            const auto& merge = merges[i];
            if (merge.size() >= 2) {
                std::string key = MergeKey(merge[0].get<std::string>(), merge[1].get<std::string>());
                mergeTable_[key] = static_cast<int>(i);
            }
        }
        LOGF(L"[BPETokenizer] Loaded %zu merges", mergeTable_.size());
        return true;
    } catch (const std::exception& e) {
        LOGF(L"[BPETokenizer] Error parsing tokenizer.json: %s", Utils::Utf8ToWide(e.what()).c_str());
        return false;
    }
}

std::string BPETokenizer::ByteToString(unsigned char b) {
    static const std::string kByteToUnicode[256] = {
        "\xc4\x80", "\xc4\x81", "\xc4\x82", "\xc4\x83", "\xc4\x84", "\xc4\x85", "\xc4\x86", "\xc4\x87",
        "\xc4\x88", "\xc4\x89", "\xc4\x8a", "\xc4\x8b", "\xc4\x8c", "\xc4\x8d", "\xc4\x8e", "\xc4\x8f",
        "\xc4\x90", "\xc4\x91", "\xc4\x92", "\xc4\x93", "\xc4\x94", "\xc4\x95", "\xc4\x96", "\xc4\x97",
        "\xc4\x98", "\xc4\x99", "\xc4\x9a", "\xc4\x9b", "\xc4\x9c", "\xc4\x9d", "\xc4\x9e", "\xc4\x9f",
        "\xc4\xa0", "\x21", "\x22", "\x23", "\x24", "\x25", "\x26", "\x27",
        "\x28", "\x29", "\x2a", "\x2b", "\x2c", "\x2d", "\x2e", "\x2f",
        "\x30", "\x31", "\x32", "\x33", "\x34", "\x35", "\x36", "\x37",
        "\x38", "\x39", "\x3a", "\x3b", "\x3c", "\x3d", "\x3e", "\x3f",
        "\x40", "\x41", "\x42", "\x43", "\x44", "\x45", "\x46", "\x47",
        "\x48", "\x49", "\x4a", "\x4b", "\x4c", "\x4d", "\x4e", "\x4f",
        "\x50", "\x51", "\x52", "\x53", "\x54", "\x55", "\x56", "\x57",
        "\x58", "\x59", "\x5a", "\x5b", "\x5c", "\x5d", "\x5e", "\x5f",
        "\x60", "\x61", "\x62", "\x63", "\x64", "\x65", "\x66", "\x67",
        "\x68", "\x69", "\x6a", "\x6b", "\x6c", "\x6d", "\x6e", "\x6f",
        "\x70", "\x71", "\x72", "\x73", "\x74", "\x75", "\x76", "\x77",
        "\x78", "\x79", "\x7a", "\x7b", "\x7c", "\x7d", "\x7e", "\xc4\xa1",
        "\xc4\xa2", "\xc4\xa3", "\xc4\xa4", "\xc4\xa5", "\xc4\xa6", "\xc4\xa7", "\xc4\xa8", "\xc4\xa9",
        "\xc4\xaa", "\xc4\xab", "\xc4\xac", "\xc4\xad", "\xc4\xae", "\xc4\xaf", "\xc4\xb0", "\xc4\xb1",
        "\xc4\xb2", "\xc4\xb3", "\xc4\xb4", "\xc4\xb5", "\xc4\xb6", "\xc4\xb7", "\xc4\xb8", "\xc4\xb9",
        "\xc4\xba", "\xc4\xbb", "\xc4\xbc", "\xc4\xbd", "\xc4\xbe", "\xc4\xbf", "\xc5\x80", "\xc5\x81",
        "\xc5\x82", "\xc2\xa1", "\xc2\xa2", "\xc2\xa3", "\xc2\xa4", "\xc2\xa5", "\xc2\xa6", "\xc2\xa7",
        "\xc2\xa8", "\xc2\xa9", "\xc2\xaa", "\xc2\xab", "\xc2\xac", "\xc5\x83", "\xc2\xae", "\xc2\xaf",
        "\xc2\xb0", "\xc2\xb1", "\xc2\xb2", "\xc2\xb3", "\xc2\xb4", "\xc2\xb5", "\xc2\xb6", "\xc2\xb7",
        "\xc2\xb8", "\xc2\xb9", "\xc2\xba", "\xc2\xbb", "\xc2\xbc", "\xc2\xbd", "\xc2\xbe", "\xc2\xbf",
        "\xc3\x80", "\xc3\x81", "\xc3\x82", "\xc3\x83", "\xc3\x84", "\xc3\x85", "\xc3\x86", "\xc3\x87",
        "\xc3\x88", "\xc3\x89", "\xc3\x8a", "\xc3\x8b", "\xc3\x8c", "\xc3\x8d", "\xc3\x8e", "\xc3\x8f",
        "\xc3\x90", "\xc3\x91", "\xc3\x92", "\xc3\x93", "\xc3\x94", "\xc3\x95", "\xc3\x96", "\xc3\x97",
        "\xc3\x98", "\xc3\x99", "\xc3\x9a", "\xc3\x9b", "\xc3\x9c", "\xc3\x9d", "\xc3\x9e", "\xc3\x9f",
        "\xc3\xa0", "\xc3\xa1", "\xc3\xa2", "\xc3\xa3", "\xc3\xa4", "\xc3\xa5", "\xc3\xa6", "\xc3\xa7",
        "\xc3\xa8", "\xc3\xa9", "\xc3\xaa", "\xc3\xab", "\xc3\xac", "\xc3\xad", "\xc3\xae", "\xc3\xaf",
        "\xc3\xb0", "\xc3\xb1", "\xc3\xb2", "\xc3\xb3", "\xc3\xb4", "\xc3\xb5", "\xc3\xb6", "\xc3\xb7",
        "\xc3\xb8", "\xc3\xb9", "\xc3\xba", "\xc3\xbb", "\xc3\xbc", "\xc3\xbd", "\xc3\xbe", "\xc3\xbf"
    };
    return kByteToUnicode[b];
}

std::string BPETokenizer::MergeKey(const std::string& a, const std::string& b) {
    return a + "\x01" + b;
}

std::vector<TokenPiece> BPETokenizer::BpeEncodeWord(const std::string& word, size_t byteOffset, const std::vector<size_t>& byteToChar) {
    std::vector<TokenPiece> pieces;
    if (word.empty()) return pieces;
    for (size_t i = 0; i < word.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(word[i]);
        std::string byteStr = ByteToString(b);
        pieces.push_back({0, byteStr, byteOffset + i, byteOffset + i + 1});
    }
    while (pieces.size() > 1) {
        int bestRank = std::numeric_limits<int>::max();
        size_t bestIdx = 0;
        bool found = false;
        for (size_t i = 0; i < pieces.size() - 1; ++i) {
            std::string key = MergeKey(pieces[i].text, pieces[i + 1].text);
            auto it = mergeTable_.find(key);
            if (it != mergeTable_.end() && it->second < bestRank) {
                bestRank = it->second;
                bestIdx = i;
                found = true;
            }
        }
        if (!found) break;
        TokenPiece merged;
        merged.text = pieces[bestIdx].text + pieces[bestIdx + 1].text;
        merged.byteStart = pieces[bestIdx].byteStart;
        merged.byteEnd = pieces[bestIdx + 1].byteEnd;
        merged.id = 0;
        pieces[bestIdx] = merged;
        pieces.erase(pieces.begin() + bestIdx + 1);
    }
    for (auto& piece : pieces) {
        auto it = tokenToId_.find(piece.text);
        if (it != tokenToId_.end()) {
            piece.id = it->second;
        } else {
            piece.id = 0;
        }
    }
    return pieces;
}

std::vector<TokenPiece> BPETokenizer::EncodeSpacesToPieces(int k) {
    std::string spaces(k, ' ');
    return BpeEncodeWord(spaces, 0, {});
}

std::vector<TokenPiece> BPETokenizer::Encode(const std::wstring& text) {
    std::vector<TokenPiece> result;
    if (text.empty()) return result;

    std::string utf8Text;
    std::vector<size_t> byteToChar;
    std::vector<size_t> charToByte;
    for (size_t charIdx = 0; charIdx < text.size(); ++charIdx) {
        charToByte.push_back(utf8Text.size());
        std::wstring charStr = text.substr(charIdx, 1);
        std::string charUtf8 = Utils::WideToUtf8(charStr);
        for (size_t b = 0; b < charUtf8.size(); ++b) {
            byteToChar.push_back(charIdx);
        }
        utf8Text += charUtf8;
    }
    charToByte.push_back(utf8Text.size());

    struct Segment {
        std::wstring text;
        size_t charStart;
        size_t charEnd;
        bool isSpace;
        int spaceCount;
    };
    std::vector<Segment> segments;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == L' ') {
            size_t spaceStart = i;
            while (i < text.size() && text[i] == L' ') i++;
            segments.push_back({std::wstring(i - spaceStart, L' '), spaceStart, i, true, static_cast<int>(i - spaceStart)});
        } else {
            size_t wordStart = i;
            while (i < text.size() && text[i] != L' ') i++;
            segments.push_back({text.substr(wordStart, i - wordStart), wordStart, i, false, 0});
        }
    }

    for (size_t segIdx = 0; segIdx < segments.size(); ++segIdx) {
        const auto& seg = segments[segIdx];
        if (seg.isSpace) {
            bool isLeading = (segIdx == 0);
            bool isTrailing = (segIdx == segments.size() - 1);
            int spaceCount = seg.spaceCount;
            if (isTrailing) {
                auto spaceTokens = EncodeSpacesToPieces(spaceCount);
                for (auto& piece : spaceTokens) {
                    piece.byteStart = charToByte.empty() ? 0 : charToByte[seg.charStart];
                    piece.byteEnd = charToByte.empty() ? 0 : charToByte[seg.charEnd];
                    result.push_back(piece);
                }
            } else if (isLeading) {
                if (spaceCount == 1) {
                    // absorbed
                } else if (spaceCount > 1) {
                    auto spaceTokens = EncodeSpacesToPieces(spaceCount - 1);
                    for (auto& piece : spaceTokens) {
                        piece.byteStart = charToByte.empty() ? 0 : charToByte[seg.charStart];
                        piece.byteEnd = charToByte.empty() ? 0 : charToByte[seg.charStart + spaceCount - 1];
                        result.push_back(piece);
                    }
                }
            } else {
                if (spaceCount > 1) {
                    auto spaceTokens = EncodeSpacesToPieces(spaceCount - 1);
                    for (auto& piece : spaceTokens) {
                        piece.byteStart = charToByte.empty() ? 0 : charToByte[seg.charStart];
                        piece.byteEnd = charToByte.empty() ? 0 : charToByte[seg.charStart + spaceCount - 1];
                        result.push_back(piece);
                    }
                }
            }
        } else {
            bool prependSpace = false;
            if (segIdx > 0) {
                const auto& prevSeg = segments[segIdx - 1];
                if (prevSeg.isSpace && prevSeg.spaceCount >= 1) prependSpace = true;
            }
            std::wstring wordText = prependSpace ? (L" " + seg.text) : seg.text;
            std::string wordUtf8 = Utils::WideToUtf8(wordText);
            size_t wordByteStart = 0;
            if (seg.charStart < charToByte.size()) {
                wordByteStart = charToByte[seg.charStart];
                if (prependSpace && seg.charStart > 0) wordByteStart = charToByte[seg.charStart - 1];
            }
            auto pieces = BpeEncodeWord(wordUtf8, wordByteStart, byteToChar);
            for (auto& piece : pieces) result.push_back(piece);
        }
    }
    return result;
}

std::wstring BPETokenizer::Decode(const std::vector<int64_t>& tokenIds) {
    std::string result;
    for (int64_t id : tokenIds) {
        auto it = idToToken_.find(id);
        if (it != idToToken_.end()) result += it->second;
    }
    return Utils::Utf8ToWide(result);
}

} // namespace AgentRedactor
