#include "pii_detector.h"
#include "bpe_tokenizer.h"
#include "utils.h"
#include "constants.h"
#include "logging.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <cwctype>
#include <future>
#include <cfloat>
#include <cmath>
#include <chrono>
#include <unordered_set>

using json = nlohmann::json;

namespace AgentRedactor {

static Ort::Env* g_onnx_env = nullptr;

PIIDetector::PIIDetector(const std::filesystem::path& modelPath)
    : modelPath_(modelPath) {
    supportedTypes_ = DEFAULT_PII_TYPES;
}

PIIDetector::~PIIDetector() = default;

void PIIDetector::SetProvider(const std::wstring& provider) {
    preferredProvider_ = provider;
    LOGF(L"[PIIDetector] Provider preference set to: %s", provider.c_str());
}

bool PIIDetector::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;
    LOG_LIFECYCLE(L"[PIIDetector] Initializing...");

    tokenizer_ = std::make_unique<BPETokenizer>(modelPath_);
    if (!tokenizer_->Initialize()) {
        LOG_LIFECYCLE(L"[PIIDetector] ERROR: Failed to initialize tokenizer");
        return false;
    }

    auto modelFile = modelPath_ / L"onnx" / MODEL_FILENAME;
    if (!Utils::FileExists(modelFile)) {
        modelFile = modelPath_ / L"onnx" / L"model.onnx";
        if (!Utils::FileExists(modelFile)) {
            LOG_LIFECYCLE(L"[PIIDetector] ERROR: Model file not found");
            return false;
        }
    }

    if (!LoadModel() || !LoadConfig()) {
        LOG_LIFECYCLE(L"[PIIDetector] ERROR: Failed to load model or config");
        return false;
    }

    useONNX = true;
    initialized_ = true;
    LOG_LIFECYCLE(L"[PIIDetector] Initialized successfully");
    return true;
}

bool PIIDetector::LoadModel() {
    try {
        auto modelFile = modelPath_ / L"onnx" / MODEL_FILENAME;
        if (!Utils::FileExists(modelFile)) modelFile = modelPath_ / L"onnx" / L"model.onnx";

        if (!g_onnx_env) {
            g_onnx_env = new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "AgentRedactor");
        }

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        sessionOptions.SetIntraOpNumThreads(4);
        currentProvider_ = L"CPU";

        if (preferredProvider_ != L"cpu") {
            std::vector<std::string> availableProviders = Ort::GetAvailableProviders();
            bool gpuEnabled = false;
            if (preferredProvider_ == L"auto" || preferredProvider_ == L"gpu") {
                auto it = std::find(availableProviders.begin(), availableProviders.end(), "DmlExecutionProvider");
                if (it != availableProviders.end()) {
                    try {
                        // The dedicated DML header isn't distributed with this vcpkg
                        // package, so try the generic provider API. If the installed
                        // ONNX Runtime build doesn't expose DML through this path it
                        // will throw and we fall back to CUDA / CPU.
                        sessionOptions.AppendExecutionProvider("DmlExecutionProvider");
                        currentProvider_ = L"DML";
                        gpuEnabled = true;
                        LOG(L"[PIIDetector] DirectML execution provider enabled");
                    } catch (const std::exception& e) {
                        LOGF(L"[PIIDetector] DirectML append failed: %s", Utils::Utf8ToWide(e.what()).c_str());
                    }
                }
            }
            if (!gpuEnabled && (preferredProvider_ == L"auto" || preferredProvider_ == L"gpu")) {
                auto it = std::find(availableProviders.begin(), availableProviders.end(), "CUDAExecutionProvider");
                if (it != availableProviders.end()) {
                    try {
                        OrtCUDAProviderOptions cudaOptions;
                        sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
                        currentProvider_ = L"CUDA";
                        gpuEnabled = true;
                        LOG(L"[PIIDetector] CUDA execution provider enabled");
                    } catch (const std::exception& e) {
                        LOGF(L"[PIIDetector] CUDA append failed: %s", Utils::Utf8ToWide(e.what()).c_str());
                    }
                }
            }
            if (!gpuEnabled) {
                LOG(L"[PIIDetector] No GPU provider available, using CPU");
            }
        }

        session_ = std::make_unique<Ort::Session>(*g_onnx_env, modelFile.c_str(), sessionOptions);
        LOGF_LIFECYCLE(L"[PIIDetector] Model loaded with provider: %s", currentProvider_.c_str());
        return true;
    } catch (const std::exception& e) {
        LOGF_LIFECYCLE(L"[PIIDetector] LoadModel error: %s", Utils::Utf8ToWide(e.what()).c_str());
        return false;
    }
}

bool PIIDetector::LoadConfig() {
    try {
        auto configFile = modelPath_ / L"config.json";
        if (!Utils::FileExists(configFile)) return false;
        auto content = Utils::ReadFileAsString(configFile);
        if (!content) return false;
        auto jsonConfig = json::parse(Utils::WideToUtf8(*content));
        if (jsonConfig.contains("id2label")) {
            for (auto& [key, value] : jsonConfig["id2label"].items()) {
                int id = std::stoi(key);
                std::wstring labelWide = Utils::Utf8ToWide(value.get<std::string>());
                id2label_[id] = labelWide;
                label2id_[labelWide] = id;
            }
        }
        LOG_LIFECYCLE(L"[PIIDetector] Config loaded, " + std::to_wstring(id2label_.size()) + L" labels");
        return true;
    } catch (const std::exception& e) {
        LOGF_LIFECYCLE(L"[PIIDetector] LoadConfig error: %s", Utils::Utf8ToWide(e.what()).c_str());
        return false;
    }
}

bool PIIDetector::LoadViterbiCalibration() {
    try {
        auto calibFile = modelPath_ / L"viterbi_calibration.json";
        if (!Utils::FileExists(calibFile)) return true;
        auto content = Utils::ReadFileAsString(calibFile);
        if (!content) return true;
        auto jsonCalib = json::parse(Utils::WideToUtf8(*content));
        if (jsonCalib.contains("operating_points") && jsonCalib["operating_points"].contains("default")
            && jsonCalib["operating_points"]["default"].contains("biases")) {
            for (auto& [key, value] : jsonCalib["operating_points"]["default"]["biases"].items()) {
                viterbiBiases_[key] = value.get<float>();
            }
        }
        return true;
    } catch (...) { return true; }
}

static std::vector<float> ComputeAssignedProbs(const std::vector<float>& logits, int seqLen, int numClasses, const std::vector<int>& labels) {
    std::vector<float> probs(seqLen);
    for (int t = 0; t < seqLen; ++t) {
        float maxLogit = -1e9f;
        int offset = t * numClasses;
        for (int c = 0; c < numClasses; ++c) {
            float val = logits[offset + c];
            if (val > maxLogit) maxLogit = val;
        }
        float sum = 0.0f;
        for (int c = 0; c < numClasses; ++c) {
            sum += std::exp(logits[offset + c] - maxLogit);
        }
        float assignedLogit = logits[offset + labels[t]];
        probs[t] = std::exp(assignedLogit - maxLogit) / sum;
    }
    return probs;
}

std::vector<int> PIIDetector::ViterbiDecode(const std::vector<float>& logits, int seqLen, int numClasses) {
    const float NEG_INF = -1e9f;

    struct LabelInfo {
        std::wstring boundary;
        std::wstring entity;
    };
    std::vector<LabelInfo> labelInfos(numClasses);
    for (int i = 0; i < numClasses; ++i) {
        auto it = id2label_.find(i);
        if (it != id2label_.end()) {
            const std::wstring& label = it->second;
            if (label == L"O") labelInfos[i] = {L"O", L""};
            else if (label.size() > 2 && label[1] == L'-') {
                labelInfos[i] = {label.substr(0, 1), label.substr(2)};
            }
        }
    }

    auto isValidTransition = [&](int prev, int curr) -> bool {
        const auto& p = labelInfos[prev];
        const auto& c = labelInfos[curr];
        if (p.boundary == L"O") return c.boundary == L"O" || c.boundary == L"B" || c.boundary == L"S";
        if (p.boundary == L"B" || p.boundary == L"I") {
            if (c.boundary == L"I" || c.boundary == L"E") return p.entity == c.entity;
            return false;
        }
        if (p.boundary == L"E" || p.boundary == L"S") return c.boundary == L"O" || c.boundary == L"B" || c.boundary == L"S";
        return false;
    };

    auto getTransitionScore = [&](int prev, int curr) -> float {
        if (!isValidTransition(prev, curr)) return NEG_INF;
        float score = 0.0f;
        const auto& p = labelInfos[prev];
        const auto& c = labelInfos[curr];
        if (p.boundary == L"O" && c.boundary == L"O") {
            auto it = viterbiBiases_.find("transition_bias_background_stay");
            if (it != viterbiBiases_.end()) score += it->second;
        } else if (p.boundary == L"O" && (c.boundary == L"B" || c.boundary == L"S")) {
            auto it = viterbiBiases_.find("transition_bias_background_to_start");
            if (it != viterbiBiases_.end()) score += it->second;
        } else if ((p.boundary == L"B" || p.boundary == L"I") && c.boundary == L"I") {
            auto it = viterbiBiases_.find("transition_bias_inside_to_continue");
            if (it != viterbiBiases_.end()) score += it->second;
        } else if ((p.boundary == L"B" || p.boundary == L"I") && c.boundary == L"E") {
            auto it = viterbiBiases_.find("transition_bias_inside_to_end");
            if (it != viterbiBiases_.end()) score += it->second;
        } else if ((p.boundary == L"E" || p.boundary == L"S") && c.boundary == L"O") {
            auto it = viterbiBiases_.find("transition_bias_end_to_background");
            if (it != viterbiBiases_.end()) score += it->second;
        } else if ((p.boundary == L"E" || p.boundary == L"S") && (c.boundary == L"B" || c.boundary == L"S")) {
            auto it = viterbiBiases_.find("transition_bias_end_to_start");
            if (it != viterbiBiases_.end()) score += it->second;
        }
        return score;
    };

    // dp[t][c] = best score to reach class c at time t
    std::vector<std::vector<float>> dp(seqLen, std::vector<float>(numClasses, NEG_INF));
    // backtrace[t][c] = previous class that led to best score at time t
    std::vector<std::vector<int>> backtrace(seqLen, std::vector<int>(numClasses, 0));

    // Initialize first timestep: valid start labels are O, B, S
    for (int c = 0; c < numClasses; ++c) {
        if (labelInfos[c].boundary != L"O" && labelInfos[c].boundary != L"B" && labelInfos[c].boundary != L"S") continue;
        dp[0][c] = logits[c];
    }

    // Forward pass
    for (int t = 1; t < seqLen; ++t) {
        int offset = t * numClasses;
        for (int c = 0; c < numClasses; ++c) {
            float bestScore = NEG_INF;
            int bestPrev = 0;
            for (int p = 0; p < numClasses; ++p) {
                if (dp[t - 1][p] <= NEG_INF / 2) continue;
                float trans = getTransitionScore(p, c);
                if (trans <= NEG_INF / 2) continue;
                float score = dp[t - 1][p] + trans + logits[offset + c];
                if (score > bestScore) {
                    bestScore = score;
                    bestPrev = p;
                }
            }
            dp[t][c] = bestScore;
            backtrace[t][c] = bestPrev;
        }
    }

    // Find best final state
    float bestFinalScore = NEG_INF;
    int bestFinalLabel = 0;
    for (int c = 0; c < numClasses; ++c) {
        if (dp[seqLen - 1][c] > bestFinalScore) {
            bestFinalScore = dp[seqLen - 1][c];
            bestFinalLabel = c;
        }
    }

    // Backtrace
    std::vector<int> path(seqLen);
    path[seqLen - 1] = bestFinalLabel;
    for (int t = seqLen - 2; t >= 0; --t) {
        path[t] = backtrace[t + 1][path[t + 1]];
    }
    return path;
}

std::vector<PIIEntity> PIIDetector::LabelsToEntities(
    const std::vector<int>& labels,
    const std::wstring& text,
    const std::vector<size_t>& tokenCharStarts,
    const std::vector<size_t>& tokenCharEnds,
    const std::vector<std::wstring>& enabledTypes,
    const std::vector<float>& tokenProbs,
    size_t globalTokenStart,
    size_t maxValidGlobalTokenEnd) {

    std::vector<PIIEntity> entities;
    std::unordered_set<std::wstring> enabledSet(enabledTypes.begin(), enabledTypes.end());
    std::optional<PIIEntity> currentEntity;
    size_t currentEntityTokenCount = 0;
    std::optional<size_t> currentEntityGlobalStartIdx;

    auto finalizeEntity = [&](bool pushIfEnabled) {
        if (!currentEntity.has_value()) return;
        if (pushIfEnabled && enabledSet.count(currentEntity->type) > 0 && currentEntityGlobalStartIdx.has_value()) {
            if (currentEntityGlobalStartIdx.value() < maxValidGlobalTokenEnd) {
                currentEntity->confidence /= static_cast<float>(currentEntityTokenCount);
                if (currentEntity->start < currentEntity->end && currentEntity->end <= text.size()) {
                    currentEntity->text = text.substr(currentEntity->start, currentEntity->end - currentEntity->start);
                    entities.push_back(std::move(currentEntity.value()));
                }
            }
        }
        currentEntity.reset();
        currentEntityTokenCount = 0;
        currentEntityGlobalStartIdx.reset();
    };

    for (size_t i = 0; i < labels.size(); ++i) {
        size_t globalTokenIdx = globalTokenStart + i;
        auto it = id2label_.find(labels[i]);
        if (it == id2label_.end()) continue;
        const std::wstring& label = it->second;
        if (label == L"O") {
            finalizeEntity(true);
            continue;
        }
        if (label.size() < 3 || label[1] != L'-') continue;
        std::wstring boundary = label.substr(0, 1);
        std::wstring entityType = label.substr(2);

        if (boundary == L"S") {
            finalizeEntity(true);
            if (enabledSet.count(entityType) > 0 && i < tokenCharStarts.size() && i < tokenCharEnds.size()) {
                size_t cs = tokenCharStarts[i];
                size_t ce = tokenCharEnds[i];
                if (cs < ce && ce <= text.size() && globalTokenIdx < maxValidGlobalTokenEnd) {
                    PIIEntity ent;
                    ent.type = entityType;
                    ent.text = text.substr(cs, ce - cs);
                    ent.start = cs;
                    ent.end = ce;
                    ent.confidence = tokenProbs[i];
                    entities.push_back(ent);
                }
            }
        } else if (boundary == L"B") {
            finalizeEntity(true);
            if (enabledSet.count(entityType) > 0 && i < tokenCharStarts.size() && i < tokenCharEnds.size()) {
                currentEntity = PIIEntity();
                currentEntity->type = entityType;
                currentEntity->start = tokenCharStarts[i];
                currentEntity->end = tokenCharEnds[i];
                currentEntity->confidence = tokenProbs[i];
                currentEntityTokenCount = 1;
                currentEntityGlobalStartIdx = globalTokenIdx;
            }
        } else if (boundary == L"I" || boundary == L"E") {
            if (currentEntity.has_value() && currentEntity->type == entityType) {
                if (i < tokenCharEnds.size()) currentEntity->end = tokenCharEnds[i];
                currentEntity->confidence += tokenProbs[i];
                currentEntityTokenCount++;
                if (boundary == L"E") {
                    finalizeEntity(true);
                }
            } else {
                finalizeEntity(true);
                if (enabledSet.count(entityType) > 0 && i < tokenCharStarts.size() && i < tokenCharEnds.size()) {
                    currentEntity = PIIEntity();
                    currentEntity->type = entityType;
                    currentEntity->start = tokenCharStarts[i];
                    currentEntity->end = tokenCharEnds[i];
                    currentEntity->confidence = tokenProbs[i];
                    currentEntityTokenCount = 1;
                    currentEntityGlobalStartIdx = globalTokenIdx;
                    if (boundary == L"E") {
                        finalizeEntity(true);
                    }
                }
            }
        }
    }
    finalizeEntity(true);
    {
        std::wstring labelDebug = L"[LabelsToEntities] Labels (count=" + std::to_wstring(labels.size()) + L"):\n";
        for (size_t i = 0; i < labels.size() && i < 40; ++i) {
            auto it = id2label_.find(labels[i]);
            std::wstring labelStr = (it != id2label_.end()) ? it->second : L"?";
            labelDebug += L"  [" + std::to_wstring(i) + L"] label=" + labelStr + L" (id=" + std::to_wstring(labels[i]) + L")\n";
        }
        labelDebug += L"[LabelsToEntities] Raw entities: " + std::to_wstring(entities.size());
        LOG(labelDebug);
    }
    return entities;
}

std::vector<PIIEntity> PIIDetector::DetectPII(
    const std::wstring& text,
    const std::vector<std::wstring>& enabledTypes,
    uint64_t redactionSequence) {

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || text.empty() || enabledTypes.empty() || !session_) {
        return {};
    }

    auto tokenPieces = tokenizer_->Encode(text);
    if (tokenPieces.empty()) return {};

    std::vector<int64_t> allTokenIds;
    std::vector<size_t> tokenCharStarts;
    std::vector<size_t> tokenCharEnds;
    for (const auto& piece : tokenPieces) allTokenIds.push_back(piece.id);

    {
        std::wstring tokenDebug = L"[DetectPII] Tokens: " + std::to_wstring(tokenPieces.size()) + L"\n";
        for (size_t i = 0; i < tokenPieces.size() && i < 30; ++i) {
            auto decoded = tokenizer_->Decode({tokenPieces[i].id});
            tokenDebug += L"  [" + std::to_wstring(i) + L"] id=" + std::to_wstring(tokenPieces[i].id)
                + L" byteStart=" + std::to_wstring(tokenPieces[i].byteStart)
                + L" byteEnd=" + std::to_wstring(tokenPieces[i].byteEnd)
                + L" decoded=[" + decoded + L"]\n";
        }
        LOG(tokenDebug);
    }

    {
        std::vector<size_t> byteToChar;
        for (size_t charIdx = 0; charIdx < text.size(); ++charIdx) {
            std::string charUtf8 = Utils::WideToUtf8(text.substr(charIdx, 1));
            for (size_t b = 0; b < charUtf8.size(); ++b) {
                byteToChar.push_back(charIdx);
            }
        }
        for (const auto& piece : tokenPieces) {
            size_t cs = (piece.byteStart < byteToChar.size()) ? byteToChar[piece.byteStart] : text.size();
            size_t ce = (piece.byteEnd > 0 && piece.byteEnd - 1 < byteToChar.size()) ? byteToChar[piece.byteEnd - 1] + 1 : text.size();
            // Exclude leading Ġ (space prefix) from character positions
            if (piece.text.size() >= 2 && static_cast<unsigned char>(piece.text[0]) == 0xC4 && static_cast<unsigned char>(piece.text[1]) == 0xA0) {
                if (cs < ce) cs++;
            }
            tokenCharStarts.push_back(cs);
            tokenCharEnds.push_back(ce);
        }
    }

    std::vector<std::vector<int64_t>> chunks;
    std::vector<std::pair<size_t, size_t>> chunkTokenRanges;
    const size_t maxTokens = MAX_TOKENS_PER_CHUNK;
    const size_t overlap = TOKEN_OVERLAP;
    for (size_t i = 0; i < allTokenIds.size(); i += (maxTokens - overlap)) {
        size_t end = std::min(i + maxTokens, allTokenIds.size());
        chunks.push_back(std::vector<int64_t>(allTokenIds.begin() + i, allTokenIds.begin() + end));
        chunkTokenRanges.push_back({i, end});
        if (end >= allTokenIds.size()) break;
    }

    std::vector<std::vector<int>> allChunkLabels;
    std::vector<std::vector<float>> allChunkProbs;
    for (size_t chunkIdx = 0; chunkIdx < chunks.size(); ++chunkIdx) {
        const auto& chunk = chunks[chunkIdx];
        size_t seqLen = chunk.size();
        try {
            Ort::AllocatorWithDefaultOptions allocator;
            Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            std::vector<int64_t> inputShape = {1, static_cast<int64_t>(seqLen)};
            std::vector<int64_t> inputIds = chunk;
            Ort::Value inputTensor = Ort::Value::CreateTensor<int64_t>(memoryInfo, inputIds.data(), inputIds.size(), inputShape.data(), inputShape.size());

            std::vector<int64_t> attentionMask(seqLen, 1);
            Ort::Value maskTensor = Ort::Value::CreateTensor<int64_t>(memoryInfo, attentionMask.data(), attentionMask.size(), inputShape.data(), inputShape.size());

            char* inputName0 = session_->GetInputNameAllocated(0, allocator).release();
            char* inputName1 = session_->GetInputNameAllocated(1, allocator).release();
            char* outputName = session_->GetOutputNameAllocated(0, allocator).release();
            const char* inputNames[] = {inputName0, inputName1};
            const char* outputNamesArr[] = {outputName};
            Ort::Value inputTensors[] = {std::move(inputTensor), std::move(maskTensor)};

            Ort::RunOptions runOptions;
            activeInferenceCount_.fetch_add(1);
            {
                std::lock_guard<std::mutex> roLock(runOptionsMutex_);
                if (redactionSequence > cancelledSequence_.load()) {
                    activeRunOptions_.push_back({redactionSequence, &runOptions});
                }
            }

            auto outputTensors = session_->Run(runOptions, inputNames, inputTensors, 2, outputNamesArr, 1);

            {
                std::lock_guard<std::mutex> roLock(runOptionsMutex_);
                activeRunOptions_.erase(std::remove_if(activeRunOptions_.begin(), activeRunOptions_.end(),
                    [&runOptions](const auto& pair) { return pair.second == &runOptions; }), activeRunOptions_.end());
            }
            activeInferenceCount_.fetch_sub(1);
            {
                std::lock_guard<std::mutex> termLock(terminationMutex_);
                terminationCondition_.notify_all();
            }

            float* logits = outputTensors[0].GetTensorMutableData<float>();
            auto outputShape = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();
            int64_t outSeqLen = outputShape[1];
            int64_t numClasses = outputShape[2];

            std::vector<float> logitsVec(outSeqLen * numClasses);
            std::copy(logits, logits + outSeqLen * numClasses, logitsVec.begin());
            auto labels = ViterbiDecode(logitsVec, static_cast<int>(outSeqLen), static_cast<int>(numClasses));
            std::vector<float> tokenProbs = ComputeAssignedProbs(logitsVec, static_cast<int>(outSeqLen), static_cast<int>(numClasses), labels);
            allChunkLabels.push_back(std::move(labels));
            allChunkProbs.push_back(std::move(tokenProbs));

        } catch (const Ort::Exception&) {
            activeInferenceCount_.fetch_sub(1);
            std::lock_guard<std::mutex> termLock(terminationMutex_);
            terminationCondition_.notify_all();
        } catch (const std::exception& e) {
            activeInferenceCount_.fetch_sub(1);
            std::lock_guard<std::mutex> termLock(terminationMutex_);
            terminationCondition_.notify_all();
            LOGF(L"[PIIDetector] Chunk %zu error: %s", chunkIdx, Utils::Utf8ToWide(e.what()).c_str());
        }
    }

    std::vector<PIIEntity> allEntities;
    for (size_t chunkIdx = 0; chunkIdx < allChunkLabels.size(); ++chunkIdx) {
        auto& labels = allChunkLabels[chunkIdx];
        auto& range = chunkTokenRanges[chunkIdx];
        std::vector<size_t> chunkCharStarts;
        std::vector<size_t> chunkCharEnds;
        for (size_t i = range.first; i < range.second && i < tokenCharStarts.size(); ++i) {
            chunkCharStarts.push_back(tokenCharStarts[i]);
            chunkCharEnds.push_back(tokenCharEnds[i]);
        }
        size_t maxValidGlobalTokenEnd = (chunkIdx == allChunkLabels.size() - 1)
            ? range.second
            : range.first + (MAX_TOKENS_PER_CHUNK - TOKEN_OVERLAP);
        auto chunkEntities = LabelsToEntities(labels, text, chunkCharStarts, chunkCharEnds, enabledTypes, allChunkProbs[chunkIdx], range.first, maxValidGlobalTokenEnd);
        allEntities.insert(allEntities.end(), chunkEntities.begin(), chunkEntities.end());
    }

    std::sort(allEntities.begin(), allEntities.end(),
        [](const PIIEntity& a, const PIIEntity& b) {
            if (a.start != b.start) return a.start < b.start;
            return a.end > b.end;
        });

    std::vector<PIIEntity> merged;
    for (const auto& ent : allEntities) {
        bool overlap = false;
        for (auto& existing : merged) {
            if (ent.start < existing.end && ent.end > existing.start) {
                overlap = true;
                if (ent.end - ent.start > existing.end - existing.start) {
                    existing = ent;
                }
                break;
            }
        }
        if (!overlap) merged.push_back(ent);
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(),
        [this](const PIIEntity& ent) { return ent.confidence < confidenceThreshold_; }), merged.end());
    for (const auto& ent : merged) {
        std::wstring logMsg = L"[DetectPII] PII detected: type=" + ent.type
            + L" start=" + std::to_wstring(ent.start)
            + L" end=" + std::to_wstring(ent.end)
            + L" text=[" + ent.text + L"]"
            + L" confidence=" + std::to_wstring(ent.confidence);
        LOG(logMsg);
    }
    return merged;
}

DetectionResult PIIDetector::RedactText(
    const std::wstring& text,
    const std::vector<std::wstring>& enabledTypes,
    const std::wstring& templateString) {

    DetectionResult result;
    result.redactedText = text;
    try {
        auto entities = DetectPII(text, enabledTypes, 0);
        result.entities = entities;
        result.success = true;
        if (entities.empty()) return result;

        std::sort(entities.begin(), entities.end(),
            [](const PIIEntity& a, const PIIEntity& b) { return a.start > b.start; });

        for (const auto& entity : entities) {
            std::wstring placeholder = templateString;
            if (placeholder == L"[REDACTED]") {
                std::wstring typeUpper = entity.type;
                std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(), ::towupper);
                std::replace(typeUpper.begin(), typeUpper.end(), L'_', L' ');
                placeholder = L"[REDACTED " + typeUpper + L"]";
            }
            if (entity.start < result.redactedText.size() && entity.end <= result.redactedText.size()) {
                result.redactedText.replace(entity.start, entity.end - entity.start, placeholder);
            }
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = Utils::Utf8ToWide(e.what());
        LOGF(L"[PIIDetector] RedactText error: %s", result.errorMessage.c_str());
    }
    return result;
}

void PIIDetector::CancelInference(uint64_t sequence) {
    LOG(L"[PIIDetector] Cancelling sequence #" + std::to_wstring(sequence));
    cancelledSequence_.store(sequence);
    std::lock_guard<std::mutex> lock(runOptionsMutex_);
    for (auto& [seq, runOptions] : activeRunOptions_) {
        if (seq <= sequence && runOptions) {
            runOptions->SetTerminate();
        }
    }
    terminationCondition_.notify_all();
}

bool PIIDetector::WaitForInferenceTermination(int timeout_ms) {
    if (activeInferenceCount_.load() == 0) return true;
    std::unique_lock<std::mutex> lock(terminationMutex_);
    return terminationCondition_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
        [this]() { return activeInferenceCount_.load() == 0; });
}

void PIIDetector::ClearMemory() {
    LOG(L"[PIIDetector] Clearing temporary memory...");
    {
        std::lock_guard<std::mutex> lock(runOptionsMutex_);
        activeRunOptions_.clear();
        activeRunOptions_.shrink_to_fit();
    }
    std::vector<std::uint8_t> tempVector;
    tempVector.reserve(1);
    tempVector.shrink_to_fit();
    #ifdef _WIN32
    HANDLE hProcess = GetCurrentProcess();
    SetProcessWorkingSetSize(hProcess, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    #endif
    {
        std::lock_guard<std::mutex> termLock(terminationMutex_);
        terminationCondition_.notify_all();
    }
    LOG(L"[PIIDetector] Memory cleanup completed");
}

} // namespace AgentRedactor
