#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <memory>

namespace AgentRedactor {

struct TokenPiece {
    int64_t id = 0;
    std::string text;
    size_t byteStart = 0;
    size_t byteEnd = 0;
};

class BPETokenizer {
public:
    explicit BPETokenizer(const std::filesystem::path& tokenizerPath);
    ~BPETokenizer() = default;
    BPETokenizer(const BPETokenizer&) = delete;
    BPETokenizer& operator=(const BPETokenizer&) = delete;

    bool Initialize();
    bool IsInitialized() const { return initialized_; }
    std::vector<TokenPiece> Encode(const std::wstring& text);
    std::wstring Decode(const std::vector<int64_t>& tokenIds);
    size_t GetVocabSize() const { return tokenToId_.size(); }

private:
    bool LoadTokenizerJson();
    static std::string ByteToString(unsigned char b);
    std::vector<TokenPiece> BpeEncodeWord(const std::string& word, size_t byteOffset, const std::vector<size_t>& byteToChar);
    std::vector<TokenPiece> EncodeSpacesToPieces(int k);
    static std::string MergeKey(const std::string& a, const std::string& b);

    std::filesystem::path tokenizerPath_;
    bool initialized_ = false;
    std::unordered_map<std::string, int64_t> tokenToId_;
    std::unordered_map<int64_t, std::string> idToToken_;
    std::unordered_map<std::string, int> mergeTable_;
};

} // namespace AgentRedactor
