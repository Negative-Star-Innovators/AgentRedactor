#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <atomic>
#include <condition_variable>

namespace Ort {
    class Session;
    class Env;
    class Value;
    class RunOptions;
}

namespace AgentRedactor {

class BPETokenizer;

struct PIIEntity {
    std::wstring type;
    std::wstring text;
    size_t start = 0;
    size_t end = 0;
    float confidence = 0.0f;
    std::wstring redacted;
    PIIEntity() = default;
    PIIEntity(const std::wstring& t, const std::wstring& txt, size_t s, size_t e, float conf = 1.0f)
        : type(t), text(txt), start(s), end(e), confidence(conf) {}
};

struct DetectionResult {
    std::vector<PIIEntity> entities;
    std::wstring redactedText;
    bool success = false;
    std::wstring errorMessage;
};

class PIIDetector {
public:
    explicit PIIDetector(const std::filesystem::path& modelPath = L"models");
    ~PIIDetector();
    PIIDetector(const PIIDetector&) = delete;
    PIIDetector& operator=(const PIIDetector&) = delete;

    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    void SetProvider(const std::wstring& provider);
    std::wstring GetCurrentProvider() const { return currentProvider_; }
    void SetConfidenceThreshold(float threshold) { confidenceThreshold_ = threshold; }
    float GetConfidenceThreshold() const { return confidenceThreshold_; }

    std::vector<PIIEntity> DetectPII(
        const std::wstring& text,
        const std::vector<std::wstring>& enabledTypes,
        uint64_t redactionSequence = 0);

    DetectionResult RedactText(
        const std::wstring& text,
        const std::vector<std::wstring>& enabledTypes,
        const std::wstring& templateString = L"[REDACTED]");

    const std::vector<std::wstring>& GetSupportedPIITypes() const { return supportedTypes_; }

    void CancelInference(uint64_t sequence);
    bool WaitForInferenceTermination(int timeout_ms = 5000);
    void ClearMemory();

private:
    bool LoadModel();
    bool LoadConfig();
    bool LoadViterbiCalibration();
    std::vector<int> ViterbiDecode(const std::vector<float>& logits, int seqLen, int numClasses);
    std::vector<PIIEntity> LabelsToEntities(
        const std::vector<int>& labels,
        const std::wstring& text,
        const std::vector<size_t>& tokenCharStarts,
        const std::vector<size_t>& tokenCharEnds,
        const std::vector<std::wstring>& enabledTypes,
        const std::vector<float>& tokenProbs,
        size_t globalTokenStart,
        size_t maxValidGlobalTokenEnd);

    std::filesystem::path modelPath_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<BPETokenizer> tokenizer_;
    std::vector<std::wstring> supportedTypes_;
    bool initialized_ = false;
    bool useONNX = false;
    std::wstring preferredProvider_ = L"auto";
    std::wstring currentProvider_ = L"cpu";
    std::unordered_map<std::string, float> viterbiBiases_;
    float confidenceThreshold_ = 0.9f;
    std::unordered_map<int, std::wstring> id2label_;
    std::unordered_map<std::wstring, int> label2id_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> cancelledSequence_{0};
    std::mutex runOptionsMutex_;
    std::vector<std::pair<uint64_t, Ort::RunOptions*>> activeRunOptions_;
    std::atomic<int> activeInferenceCount_{0};
    std::mutex terminationMutex_;
    std::condition_variable terminationCondition_;
};

} // namespace AgentRedactor
