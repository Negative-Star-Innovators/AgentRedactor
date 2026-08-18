#include "proxy_engine.h"
#include "utils.h"
#include "logging.h"
#include "constants.h"
#include "localization.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <chrono>

using json = nlohmann::json;

#ifdef _WIN32
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_GZIP
#define WINHTTP_DECOMPRESSION_FLAG_GZIP 0x00000001
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_DEFLATE
#define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 0x00000002
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL (WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE)
#endif
#else
#include <curl/curl.h>
#endif

namespace AgentRedactor {

ProxyEngine::ProxyEngine(PIIDetector* detector, LogManager* logManager, std::function<void()> onUpdate)
    : detector_(detector), logManager_(logManager), onUpdate_(std::move(onUpdate)) {
}

size_t ProxyEngine::ComputeConfigHash(const ApiKeyProfile& profile) {
    size_t hash = 0;
    hash = Utils::HashCombine(hash, static_cast<size_t>(profile.useOpenAIModel));
    hash = Utils::HashCombine(hash, static_cast<size_t>(profile.protocolMode));
    hash = Utils::HashCombine(hash, static_cast<size_t>(profile.piiConfidenceThreshold * 1000.0f));
    for (const auto& t : profile.enabledPIITypes) {
        hash = Utils::HashCombine(hash, Utils::HashWString(t));
    }
    for (const auto& r : profile.regexPatterns) {
        hash = Utils::HashCombine(hash, Utils::HashWString(r.pattern));
        hash = Utils::HashCombine(hash, static_cast<size_t>(r.enabled));
    }
    for (const auto& k : profile.keywords) {
        hash = Utils::HashCombine(hash, Utils::HashWString(k.text));
        hash = Utils::HashCombine(hash, static_cast<size_t>(k.caseSensitive));
        hash = Utils::HashCombine(hash, static_cast<size_t>(k.enabled));
    }
    return hash;
}

SessionState& ProxyEngine::GetSessionState(const std::wstring& profileId) {
    return profileSessions_[profileId];
}

std::wstring ProxyEngine::ApplyForwardPropagation(const std::wstring& text, SessionState& session,
    std::map<std::wstring, std::wstring>& fragPii,
    std::map<std::wstring, std::wstring>& fragRegex,
    std::map<std::wstring, std::wstring>& fragKeyword) {
    std::wstring result = text;
    struct MappingInfo {
        std::wstring original;
        std::wstring label;
        int type; // 0=pii, 1=regex, 2=keyword
    };
    std::vector<MappingInfo> mappings;
    for (const auto& [original, label] : session.piiLabelMap) {
        mappings.push_back({original, label, 0});
    }
    for (const auto& [original, label] : session.regexLabelMap) {
        mappings.push_back({original, label, 1});
    }
    for (const auto& [original, label] : session.keywordLabelMap) {
        mappings.push_back({original, label, 2});
    }
    std::sort(mappings.begin(), mappings.end(), [](const MappingInfo& a, const MappingInfo& b) {
        return a.original.length() > b.original.length();
    });
    for (const auto& mi : mappings) {
        size_t pos = 0;
        bool found = false;
        while ((pos = result.find(mi.original, pos)) != std::wstring::npos) {
            result.replace(pos, mi.original.size(), mi.label);
            pos += mi.label.size();
            found = true;
        }
        if (found) {
            if (mi.type == 0) fragPii[mi.label] = mi.original;
            else if (mi.type == 1) fragRegex[mi.label] = mi.original;
            else fragKeyword[mi.label] = mi.original;
        }
    }
    return result;
}

std::wstring ProxyEngine::RedactWithPIIModel(const std::wstring& text,
    const std::vector<std::wstring>& enabledTypes,
    SessionState& session,
    std::map<std::wstring, std::wstring>& fragmentMap) {

    if (!detector_ || !detector_->IsInitialized() || enabledTypes.empty()) {
        return text;
    }

    auto entities = detector_->DetectPII(text, enabledTypes, 0);
    if (entities.empty()) return text;

    if (logManager_->IsShowSensitive()) {
        std::wstring entityDebug = L"[RedactWithPIIModel] Detected " + std::to_wstring(entities.size()) + L" entities:\n";
        for (const auto& entity : entities) {
            entityDebug += L"  type=" + entity.type + L" start=" + std::to_wstring(entity.start)
                + L" end=" + std::to_wstring(entity.end) + L" text=[" + entity.text + L"]"
                + L" confidence=" + std::to_wstring(entity.confidence) + L"\n";
        }
        LOG(entityDebug);
    }

    std::sort(entities.begin(), entities.end(),
        [](const PIIEntity& a, const PIIEntity& b) { return a.start > b.start; });

    std::wstring result = text;
    for (const auto& entity : entities) {
        std::wstring label;
        auto it = session.piiLabelMap.find(entity.text);
        if (it != session.piiLabelMap.end()) {
            label = it->second;
        } else {
            label = L"<<REDACTED_PII_" + std::to_wstring(session.piiCounter++) + L">>";
            session.piiLabelMap[entity.text] = label;
            session.piiTypeMap[entity.text] = entity.type;
        }
        fragmentMap[label] = entity.text;
        if (entity.start < result.size() && entity.end <= result.size()) {
            result.replace(entity.start, entity.end - entity.start, label);
        }
    }
    return result;
}

std::wstring ProxyEngine::UnredactAll(const std::wstring& text, const RedactionState& state) {
    std::wstring result = text;
    for (const auto& [label, original] : state.piiMap) {
        result = Utils::ReplaceAll(result, label, original);
    }
    for (const auto& [label, original] : state.regexMap) {
        result = Utils::ReplaceAll(result, label, original);
    }
    for (const auto& [label, original] : state.keywordMap) {
        result = Utils::ReplaceAll(result, label, original);
    }
    return result;
}

bool ProxyEngine::IsSSE(const std::vector<std::pair<std::wstring, std::wstring>>& headers) {
    for (const auto& [name, value] : headers) {
        if (Utils::ToLower(name) == L"content-type") {
            return Utils::ToLower(value).find(L"text/event-stream") != std::wstring::npos;
        }
    }
    return false;
}

std::string ProxyEngine::RebuildSSE(const std::string& sseBody, const RedactionState& state) {
    struct Event {
        std::string raw;
        json jsonObj;
        bool isJson = false;
        bool isDone = false;
    };

    // Pass 1: parse all SSE events into a vector
    std::vector<Event> events;
    std::istringstream stream(sseBody);
    std::string line;
    std::string currentEvent;

    auto flushCurrent = [&]() {
        if (!currentEvent.empty()) {
            Event ev;
            if (currentEvent == "[DONE]") {
                ev.isDone = true;
            } else {
                try {
                    ev.jsonObj = json::parse(currentEvent);
                    ev.isJson = true;
                } catch (...) {
                    ev.raw = currentEvent;
                }
            }
            events.push_back(std::move(ev));
            currentEvent.clear();
        }
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            flushCurrent();
        } else if (line.substr(0, 5) == "data:") {
            std::string data = line.substr(5);
            size_t start = data.find_first_not_of(" \t");
            if (start != std::string::npos) data = data.substr(start);
            if (data == "[DONE]") {
                flushCurrent();
                Event ev;
                ev.isDone = true;
                events.push_back(std::move(ev));
            } else {
                currentEvent += data;
            }
        }
    }
    flushCurrent();

    // Determine max number of choices across all events
    size_t maxChoices = 0;
    for (const auto& ev : events) {
        if (ev.isJson && ev.jsonObj.contains("choices") && ev.jsonObj["choices"].is_array()) {
            maxChoices = std::max(maxChoices, ev.jsonObj["choices"].size());
        }
    }

    // Helper to redistribute a wide string across chunks proportionally
    auto redistribute = [](const std::wstring& unredacted,
                           const std::vector<std::string>& originals,
                           std::vector<std::wstring>& outChunks) {
        outChunks.clear();
        if (originals.empty()) return;
        size_t totalOriginal = 0;
        std::vector<size_t> origWLen;
        for (const auto& s : originals) {
            size_t wl = Utils::Utf8ToWide(s).size();
            origWLen.push_back(wl);
            totalOriginal += wl;
        }
        if (totalOriginal == 0) {
            for (size_t i = 0; i < originals.size(); ++i) {
                outChunks.push_back(L"");
            }
            return;
        }
        size_t pos = 0;
        const size_t unredactedLen = unredacted.size();
        for (size_t i = 0; i < originals.size(); ++i) {
            bool isLast = (i == originals.size() - 1);
            size_t take = isLast ? (unredactedLen - pos)
                                 : (origWLen[i] * unredactedLen) / totalOriginal;
            if (!isLast && take == 0 && pos < unredactedLen) take = 1;
            if (take > unredactedLen - pos) take = unredactedLen - pos;
            outChunks.push_back(unredacted.substr(pos, take));
            pos += take;
        }
    };

    // Pass 2: for each choice, collect field text, unredact, and redistribute
    for (size_t choiceIdx = 0; choiceIdx < maxChoices; ++choiceIdx) {
        // ---- Content ----
        std::vector<size_t> contentEventIdx;
        std::vector<std::string> contentOriginals;
        std::wstring fullContent;
        for (size_t e = 0; e < events.size(); ++e) {
            if (!events[e].isJson) continue;
            auto& ev = events[e].jsonObj;
            if (!ev.contains("choices") || !ev["choices"].is_array()) continue;
            if (choiceIdx >= ev["choices"].size()) continue;
            auto& choice = ev["choices"][choiceIdx];
            if (!choice.contains("delta") || !choice["delta"].is_object()) continue;
            auto& delta = choice["delta"];
            if (delta.contains("content") && delta["content"].is_string()) {
                std::string orig = delta["content"].get<std::string>();
                contentEventIdx.push_back(e);
                contentOriginals.push_back(orig);
                fullContent += Utils::Utf8ToWide(orig);
            }
        }
        if (!contentEventIdx.empty()) {
            std::wstring unredacted = UnredactAll(fullContent, state);
            std::vector<std::wstring> chunks;
            redistribute(unredacted, contentOriginals, chunks);
            for (size_t i = 0; i < contentEventIdx.size(); ++i) {
                events[contentEventIdx[i]].jsonObj["choices"][choiceIdx]["delta"]["content"] = Utils::WideToUtf8(chunks[i]);
            }
        }

        // ---- Reasoning ----
        std::vector<size_t> reasoningEventIdx;
        std::vector<std::string> reasoningOriginals;
        std::wstring fullReasoning;
        for (size_t e = 0; e < events.size(); ++e) {
            if (!events[e].isJson) continue;
            auto& ev = events[e].jsonObj;
            if (!ev.contains("choices") || !ev["choices"].is_array()) continue;
            if (choiceIdx >= ev["choices"].size()) continue;
            auto& choice = ev["choices"][choiceIdx];
            if (!choice.contains("delta") || !choice["delta"].is_object()) continue;
            auto& delta = choice["delta"];
            if (delta.contains("reasoning") && delta["reasoning"].is_string()) {
                std::string orig = delta["reasoning"].get<std::string>();
                reasoningEventIdx.push_back(e);
                reasoningOriginals.push_back(orig);
                fullReasoning += Utils::Utf8ToWide(orig);
            }
        }
        if (!reasoningEventIdx.empty()) {
            LOGF(L"[RebuildSSE] reasoning events=%zu, fullReasoning=[%s]", reasoningEventIdx.size(), fullReasoning.c_str());
            std::wstring unredacted = UnredactAll(fullReasoning, state);
            LOGF(L"[RebuildSSE] unredacted reasoning=[%s]", unredacted.c_str());
            std::vector<std::wstring> chunks;
            redistribute(unredacted, reasoningOriginals, chunks);
            for (size_t i = 0; i < reasoningEventIdx.size(); ++i) {
                events[reasoningEventIdx[i]].jsonObj["choices"][choiceIdx]["delta"]["reasoning"] = Utils::WideToUtf8(chunks[i]);
            }
        }

        // ---- Tool call arguments (per tool index) ----
        std::map<int, std::vector<size_t>> toolEventIdx;
        std::map<int, std::vector<std::string>> toolOriginals;
        std::map<int, std::wstring> toolFullText;
        for (size_t e = 0; e < events.size(); ++e) {
            if (!events[e].isJson) continue;
            auto& ev = events[e].jsonObj;
            if (!ev.contains("choices") || !ev["choices"].is_array()) continue;
            if (choiceIdx >= ev["choices"].size()) continue;
            auto& choice = ev["choices"][choiceIdx];
            if (!choice.contains("delta") || !choice["delta"].is_object()) continue;
            auto& delta = choice["delta"];
            if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                for (size_t tcIdx = 0; tcIdx < delta["tool_calls"].size(); ++tcIdx) {
                    auto& tc = delta["tool_calls"][tcIdx];
                    int toolIndex = tc.contains("index") && tc["index"].is_number_integer()
                        ? tc["index"].get<int>() : static_cast<int>(tcIdx);
                    if (tc.contains("function") && tc["function"].is_object()) {
                        auto& func = tc["function"];
                        if (func.contains("arguments") && func["arguments"].is_string()) {
                            std::string orig = func["arguments"].get<std::string>();
                            toolEventIdx[toolIndex].push_back(e);
                            toolOriginals[toolIndex].push_back(orig);
                            toolFullText[toolIndex] += Utils::Utf8ToWide(orig);
                        }
                    }
                }
            }
        }
        for (auto& [toolIndex, idxs] : toolEventIdx) {
            std::wstring unredacted = UnredactAll(toolFullText[toolIndex], state);
            std::vector<std::wstring> chunks;
            redistribute(unredacted, toolOriginals[toolIndex], chunks);
            for (size_t i = 0; i < idxs.size(); ++i) {
                auto& delta = events[idxs[i]].jsonObj["choices"][choiceIdx]["delta"];
                if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                    for (size_t tcIdx = 0; tcIdx < delta["tool_calls"].size(); ++tcIdx) {
                        auto& tc = delta["tool_calls"][tcIdx];
                        int tcIndex = tc.contains("index") && tc["index"].is_number_integer()
                            ? tc["index"].get<int>() : static_cast<int>(tcIdx);
                        if (tcIndex == toolIndex && tc.contains("function") && tc["function"].is_object()) {
                            tc["function"]["arguments"] = Utils::WideToUtf8(chunks[i]);
                            break;
                        }
                    }
                }
            }
        }
    }

    // Anthropic Messages SSE pass: unredact text/partial_json values that appear
    // in content_block_delta and content_block_start events.
    {
        struct AnthropicField {
            size_t eventIdx;
            std::string parent;
            std::string key;
            std::string original;
        };
        std::vector<AnthropicField> anthropicFields;
        std::wstring anthropicFull;
        for (size_t e = 0; e < events.size(); ++e) {
            if (!events[e].isJson) continue;
            auto& ev = events[e].jsonObj;
            if (!ev.contains("type") || !ev["type"].is_string()) continue;
            std::string evType = ev["type"].get<std::string>();
            if (evType == "content_block_delta" && ev.contains("delta") && ev["delta"].is_object()) {
                auto& delta = ev["delta"];
                if (delta.contains("type") && delta["type"].is_string()) {
                    std::string deltaType = delta["type"].get<std::string>();
                    if (deltaType == "text_delta" && delta.contains("text") && delta["text"].is_string()) {
                        std::string orig = delta["text"].get<std::string>();
                        anthropicFields.push_back({e, "delta", "text", orig});
                        anthropicFull += Utils::Utf8ToWide(orig);
                    } else if (deltaType == "input_json_delta" && delta.contains("partial_json") && delta["partial_json"].is_string()) {
                        std::string orig = delta["partial_json"].get<std::string>();
                        anthropicFields.push_back({e, "delta", "partial_json", orig});
                        anthropicFull += Utils::Utf8ToWide(orig);
                    }
                }
            } else if (evType == "content_block_start" && ev.contains("content_block") && ev["content_block"].is_object()) {
                auto& cb = ev["content_block"];
                if (cb.contains("type") && cb["type"].is_string()) {
                    std::string cbType = cb["type"].get<std::string>();
                    if (cbType == "text" && cb.contains("text") && cb["text"].is_string()) {
                        std::string orig = cb["text"].get<std::string>();
                        anthropicFields.push_back({e, "content_block", "text", orig});
                        anthropicFull += Utils::Utf8ToWide(orig);
                    } else if (cbType == "tool_use" && cb.contains("partial_json") && cb["partial_json"].is_string()) {
                        std::string orig = cb["partial_json"].get<std::string>();
                        anthropicFields.push_back({e, "content_block", "partial_json", orig});
                        anthropicFull += Utils::Utf8ToWide(orig);
                    }
                }
            }
        }
        if (!anthropicFields.empty()) {
            LOGF(L"[RebuildSSE] anthropic fields=%zu, fullText=[%s]", anthropicFields.size(), anthropicFull.c_str());
            std::wstring unredacted = UnredactAll(anthropicFull, state);
            LOGF(L"[RebuildSSE] unredacted anthropic text=[%s]", unredacted.c_str());
            std::vector<std::string> originals;
            originals.reserve(anthropicFields.size());
            for (const auto& f : anthropicFields) originals.push_back(f.original);
            std::vector<std::wstring> chunks;
            redistribute(unredacted, originals, chunks);
            for (size_t i = 0; i < anthropicFields.size(); ++i) {
                events[anthropicFields[i].eventIdx].jsonObj[anthropicFields[i].parent][anthropicFields[i].key] = Utils::WideToUtf8(chunks[i]);
            }
        }
    }

    // OpenAI Responses API SSE pass (Codex, /responses). Streaming events
    // ("response.output_text.delta", "response.reasoning_summary_text.delta",
    // "response.function_call_arguments.delta", ...) carry text in a top-level
    // string "delta" field — unlike chat completions, where "delta" is an
    // object. Group deltas by (type, item_id, output_index) channel so labels
    // split across chunks are rejoined without mixing text from different
    // channels. Terminal/snapshot events ("response.output_text.done",
    // "response.output_item.done", "response.completed", ...) repeat complete
    // text in nested "text" fields; those hold full values, so unredact each
    // in place.
    {
        struct ResponsesDelta {
            json* node;
            std::string original;
        };
        std::map<std::string, std::vector<ResponsesDelta>> deltaChannels;
        for (auto& ev : events) {
            if (!ev.isJson) continue;
            auto& j = ev.jsonObj;
            if (!j.contains("type") || !j["type"].is_string()) continue;
            std::string evType = j["type"].get<std::string>();
            if (evType.rfind("response.", 0) != 0) continue;
            if (j.contains("delta") && j["delta"].is_string()) {
                std::string channel = evType;
                if (j.contains("item_id") && j["item_id"].is_string()) {
                    channel += "|" + j["item_id"].get<std::string>();
                }
                if (j.contains("output_index") && j["output_index"].is_number_integer()) {
                    channel += "|" + std::to_string(j["output_index"].get<int>());
                }
                deltaChannels[channel].push_back({&j["delta"], j["delta"].get<std::string>()});
            }
        }
        for (auto& [channel, deltas] : deltaChannels) {
            std::wstring full;
            std::vector<std::string> originals;
            for (const auto& d : deltas) {
                full += Utils::Utf8ToWide(d.original);
                originals.push_back(d.original);
            }
            std::wstring unredacted = UnredactAll(full, state);
            std::vector<std::wstring> chunks;
            redistribute(unredacted, originals, chunks);
            for (size_t i = 0; i < deltas.size(); ++i) {
                *deltas[i].node = Utils::WideToUtf8(chunks[i]);
            }
        }

        std::function<void(json&)> unredactTextFields = [&](json& node) {
            if (node.is_object()) {
                for (auto& item : node.items()) {
                    json& value = item.value();
                    // "text" covers output_text/refusal/summary content; some
                    // providers also add an "output_text" convenience field.
                    if ((item.key() == "text" || item.key() == "output_text") && value.is_string()) {
                        value = Utils::WideToUtf8(UnredactAll(Utils::Utf8ToWide(value.get<std::string>()), state));
                    } else {
                        unredactTextFields(value);
                    }
                }
            } else if (node.is_array()) {
                for (auto& item : node) {
                    unredactTextFields(item);
                }
            }
        };
        for (auto& ev : events) {
            if (!ev.isJson) continue;
            auto& j = ev.jsonObj;
            if (!j.contains("type") || !j["type"].is_string()) continue;
            if (j["type"].get<std::string>().rfind("response.", 0) != 0) continue;
            unredactTextFields(j);
        }
    }

    // Pass 3: serialize events back
    std::ostringstream output;
    for (const auto& ev : events) {
        if (ev.isDone) {
            output << "data: [DONE]\n\n";
        } else if (ev.isJson) {
            output << "data: " << ev.jsonObj.dump() << "\n\n";
        } else if (!ev.raw.empty() && ev.raw.find_first_not_of(" \t\r\n") != std::string::npos) {
            output << "data: " << ev.raw << "\n\n";
        }
        // Whitespace-only raw events are dropped; they would become empty
        // ``data:`` lines that strict SSE clients reject.
    }
    return output.str();
}

std::string ProxyEngine::ProcessRequest(const ApiKeyProfile& profile, const std::string& method,
    const std::wstring& path, const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    const std::string& body, RedactionState& state) {

    size_t configHash = ComputeConfigHash(profile);
    SessionState& session = GetSessionState(profile.id);
    if (session.configHash != configHash) {
        session.configHash = configHash;
        session.fragmentCache.clear();
        session.piiLabelMap.clear();
        session.piiTypeMap.clear();
        session.regexLabelMap.clear();
        session.keywordLabelMap.clear();
        session.piiCounter = 0;
        session.regexCounter = 0;
        session.keywordCounter = 0;
    }

    state.originalText = Utils::Utf8ToWide(body);
    state.redactedText = state.originalText;

    size_t piiCount = 0, regexCount = 0, keywordCount = 0;

    // Helper to redact a single text fragment with session caching and forward propagation
    auto redactTextFragment = [&](const std::wstring& text) -> std::wstring {
        std::wstring result = text;
        auto fragmentStart = std::chrono::high_resolution_clock::now();

        auto logPipelineStage = [&](const std::wstring& stage, const std::wstring& stageText) {
            if (logManager_->IsShowSensitive()) {
                LOG(L"[RedactionPipeline] " + stage + L": " + stageText);
            } else {
                LOG(L"[RedactionPipeline] " + stage);
            }
        };

        // Check fragment cache
        auto tCacheStart = std::chrono::high_resolution_clock::now();
        size_t textHash = Utils::HashWString(text);
        auto cacheIt = session.fragmentCache.find(textHash);
        auto tCacheEnd = std::chrono::high_resolution_clock::now();
        auto cacheLookupUs = std::chrono::duration_cast<std::chrono::microseconds>(tCacheEnd - tCacheStart).count();
        if (cacheIt != session.fragmentCache.end()) {
            const auto& cached = cacheIt->second;
            for (const auto& pair : cached.piiMap) state.piiMap[pair.first] = pair.second;
            for (const auto& pair : cached.regexMap) state.regexMap[pair.first] = pair.second;
            for (const auto& pair : cached.keywordMap) state.keywordMap[pair.first] = pair.second;
            if (logManager_->IsShowSensitive()) {
                LOG(L"[RedactionPipeline] CACHE_HIT original: " + text);
                LOG(L"[RedactionPipeline] CACHE_HIT redacted: " + cached.redactedText);
                std::wstring cachedEntities;
                for (const auto& pair : cached.piiMap) {
                    cachedEntities += L"  label=" + pair.first + L" text=[" + pair.second + L"]\n";
                }
                if (!cachedEntities.empty()) {
                    LOG(L"[RedactionPipeline] CACHE_HIT cached entities:\n" + cachedEntities);
                }
            }
            logPipelineStage(L"CACHE_HIT (skipping redaction engines)", cached.redactedText);
            auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - fragmentStart).count();
            LOG(L"[RedactionPipeline] TIMING total=" + std::to_wstring(totalUs) + L"us cache_lookup=" + std::to_wstring(cacheLookupUs) + L"us");
            return cached.redactedText;
        }

        if (logManager_->IsShowSensitive()) {
            logPipelineStage(L"INPUT (text seen by AI/PII model)", result);
        }

        std::map<std::wstring, std::wstring> fragPii, fragRegex, fragKeyword;
        long long piiModelUs = 0, regexUs = 0, keywordsUs = 0, forwardPropUs = 0;

        if (profile.useOpenAIModel && detector_ && detector_->IsInitialized() && !profile.enabledPIITypes.empty()) {
            detector_->SetConfidenceThreshold(profile.piiConfidenceThreshold);
            auto t0 = std::chrono::high_resolution_clock::now();
            result = RedactWithPIIModel(result, profile.enabledPIITypes, session, fragPii);
            auto t1 = std::chrono::high_resolution_clock::now();
            piiModelUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            for (const auto& pair : fragPii) state.piiMap[pair.first] = pair.second;
            logPipelineStage(L"AFTER_PII_MODEL (text seen by regex engine)", result);
        }
        if (!profile.regexPatterns.empty()) {
            auto t0 = std::chrono::high_resolution_clock::now();
            regexEngine_.SetPatterns(profile.regexPatterns);
            auto [redacted, newLabels] = regexEngine_.Redact(result, session.regexLabelMap, session.regexCounter, fragRegex);
            result = redacted;
            auto t1 = std::chrono::high_resolution_clock::now();
            regexUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            for (const auto& pair : fragRegex) state.regexMap[pair.first] = pair.second;
            logPipelineStage(L"AFTER_REGEX (text seen by keyword engine)", result);
        }
        if (!profile.keywords.empty()) {
            auto t0 = std::chrono::high_resolution_clock::now();
            keywordEngine_.SetKeywords(profile.keywords);
            auto [redacted, newLabels] = keywordEngine_.Redact(result, session.keywordLabelMap, session.keywordCounter, fragKeyword);
            result = redacted;
            auto t1 = std::chrono::high_resolution_clock::now();
            keywordsUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            for (const auto& pair : fragKeyword) state.keywordMap[pair.first] = pair.second;
            logPipelineStage(L"AFTER_KEYWORDS (before forward propagation)", result);
        }

        // Forward propagation: apply all previously known session mappings
        auto tFwdStart = std::chrono::high_resolution_clock::now();
        result = ApplyForwardPropagation(result, session, fragPii, fragRegex, fragKeyword);
        auto tFwdEnd = std::chrono::high_resolution_clock::now();
        forwardPropUs = std::chrono::duration_cast<std::chrono::microseconds>(tFwdEnd - tFwdStart).count();
        logPipelineStage(L"AFTER_FORWARD_PROPAGATION (final redacted text)", result);

        if (profile.regexPatterns.empty() && profile.keywords.empty()) {
            logPipelineStage(L"FINAL (text after AI/PII model - no regex/keywords configured)", result);
        }

        // Store in fragment cache
        CachedFragment cached;
        cached.redactedText = result;
        cached.piiMap = std::move(fragPii);
        cached.regexMap = std::move(fragRegex);
        cached.keywordMap = std::move(fragKeyword);
        session.fragmentCache[textHash] = std::move(cached);

        auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - fragmentStart).count();
        LOG(L"[RedactionPipeline] TIMING total=" + std::to_wstring(totalUs) + L"us cache_lookup=" + std::to_wstring(cacheLookupUs) + L"us pii_model=" + std::to_wstring(piiModelUs) + L"us regex=" + std::to_wstring(regexUs) + L"us keywords=" + std::to_wstring(keywordsUs) + L"us forward_prop=" + std::to_wstring(forwardPropUs) + L"us");

        return result;
    };

    // Try to parse as OpenAI chat completions JSON and redact message contents individually
    bool parsedJson = false;
    try {
        auto jsonBody = json::parse(body);
        if (jsonBody.contains("messages") && jsonBody["messages"].is_array()) {
            parsedJson = true;
            for (auto& message : jsonBody["messages"]) {
                // Redact message content (string or array format)
                if (message.contains("content")) {
                    if (message["content"].is_string()) {
                        std::wstring content = Utils::Utf8ToWide(message["content"].get<std::string>());
                        message["content"] = Utils::WideToUtf8(redactTextFragment(content));
                    } else if (message["content"].is_array()) {
                        for (auto& item : message["content"]) {
                            if (item.is_object() && item.contains("type") && item["type"] == "text" && item.contains("text") && item["text"].is_string()) {
                                std::wstring text = Utils::Utf8ToWide(item["text"].get<std::string>());
                                item["text"] = Utils::WideToUtf8(redactTextFragment(text));
                            }
                        }
                    }
                }
                // Redact tool call arguments (assistant messages with tool_calls)
                if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                    for (auto& toolCall : message["tool_calls"]) {
                        if (toolCall.contains("function") && toolCall["function"].is_object()) {
                            auto& func = toolCall["function"];
                            if (func.contains("arguments") && func["arguments"].is_string()) {
                                std::wstring args = Utils::Utf8ToWide(func["arguments"].get<std::string>());
                                func["arguments"] = Utils::WideToUtf8(redactTextFragment(args));
                            }
                            // Also redact function name if it looks like it contains PII
                            if (func.contains("name") && func["name"].is_string()) {
                                std::wstring name = Utils::Utf8ToWide(func["name"].get<std::string>());
                                func["name"] = Utils::WideToUtf8(redactTextFragment(name));
                            }
                        }
                    }
                }
            }
            state.redactedText = Utils::Utf8ToWide(jsonBody.dump());
        }
    } catch (const json::exception&) {
        // Not valid JSON or not an OpenAI format — fall through to full-body redaction
    }

    // Fallback: redact the entire body as plain text
    if (!parsedJson) {
        auto logFallbackStage = [&](const std::wstring& stage, const std::wstring& stageText) {
            if (logManager_->IsShowSensitive()) {
                LOG(L"[RedactionPipeline] " + stage + L": " + stageText);
            } else {
                LOG(L"[RedactionPipeline] " + stage);
            }
        };

        auto fragmentStart = std::chrono::high_resolution_clock::now();

        // Check fragment cache for the full body
        auto tCacheStart = std::chrono::high_resolution_clock::now();
        size_t bodyHash = Utils::HashWString(state.redactedText);
        auto cacheIt = session.fragmentCache.find(bodyHash);
        auto tCacheEnd = std::chrono::high_resolution_clock::now();
        auto cacheLookupUs = std::chrono::duration_cast<std::chrono::microseconds>(tCacheEnd - tCacheStart).count();
        if (cacheIt != session.fragmentCache.end()) {
            const auto& cached = cacheIt->second;
            for (const auto& pair : cached.piiMap) state.piiMap[pair.first] = pair.second;
            for (const auto& pair : cached.regexMap) state.regexMap[pair.first] = pair.second;
            for (const auto& pair : cached.keywordMap) state.keywordMap[pair.first] = pair.second;
            state.redactedText = cached.redactedText;
            logFallbackStage(L"CACHE_HIT (skipping redaction engines)", state.redactedText);
            auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - fragmentStart).count();
            LOG(L"[RedactionPipeline] TIMING total=" + std::to_wstring(totalUs) + L"us cache_lookup=" + std::to_wstring(cacheLookupUs) + L"us");
        } else {
            if (logManager_->IsShowSensitive()) {
                logFallbackStage(L"INPUT (text seen by AI/PII model)", state.redactedText);
            }

            std::map<std::wstring, std::wstring> fragPii, fragRegex, fragKeyword;
            long long piiModelUs = 0, regexUs = 0, keywordsUs = 0, forwardPropUs = 0;

            // OpenAI Model redaction
            if (profile.useOpenAIModel && detector_ && detector_->IsInitialized() && !profile.enabledPIITypes.empty()) {
                auto t0 = std::chrono::high_resolution_clock::now();
                state.redactedText = RedactWithPIIModel(state.redactedText, profile.enabledPIITypes, session, fragPii);
                auto t1 = std::chrono::high_resolution_clock::now();
                piiModelUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                for (const auto& pair : fragPii) state.piiMap[pair.first] = pair.second;
                logFallbackStage(L"AFTER_PII_MODEL (text seen by regex engine)", state.redactedText);
            }

            // Regex redaction
            if (!profile.regexPatterns.empty()) {
                auto t0 = std::chrono::high_resolution_clock::now();
                regexEngine_.SetPatterns(profile.regexPatterns);
                auto [redacted, newLabels] = regexEngine_.Redact(state.redactedText, session.regexLabelMap, session.regexCounter, fragRegex);
                state.redactedText = redacted;
                auto t1 = std::chrono::high_resolution_clock::now();
                regexUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                for (const auto& pair : fragRegex) state.regexMap[pair.first] = pair.second;
                logFallbackStage(L"AFTER_REGEX (text seen by keyword engine)", state.redactedText);
            }

            // Keyword redaction
            if (!profile.keywords.empty()) {
                auto t0 = std::chrono::high_resolution_clock::now();
                keywordEngine_.SetKeywords(profile.keywords);
                auto [redacted, newLabels] = keywordEngine_.Redact(state.redactedText, session.keywordLabelMap, session.keywordCounter, fragKeyword);
                state.redactedText = redacted;
                auto t1 = std::chrono::high_resolution_clock::now();
                keywordsUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                for (const auto& pair : fragKeyword) state.keywordMap[pair.first] = pair.second;
                logFallbackStage(L"AFTER_KEYWORDS (before forward propagation)", state.redactedText);
            }

            // Forward propagation
            auto tFwdStart = std::chrono::high_resolution_clock::now();
            state.redactedText = ApplyForwardPropagation(state.redactedText, session, fragPii, fragRegex, fragKeyword);
            auto tFwdEnd = std::chrono::high_resolution_clock::now();
            forwardPropUs = std::chrono::duration_cast<std::chrono::microseconds>(tFwdEnd - tFwdStart).count();
            logFallbackStage(L"AFTER_FORWARD_PROPAGATION (final redacted text)", state.redactedText);

            if (profile.regexPatterns.empty() && profile.keywords.empty()) {
                logFallbackStage(L"FINAL (text after AI/PII model - no regex/keywords configured)", state.redactedText);
            }

            // Store in fragment cache
            CachedFragment cached;
            cached.redactedText = state.redactedText;
            cached.piiMap = std::move(fragPii);
            cached.regexMap = std::move(fragRegex);
            cached.keywordMap = std::move(fragKeyword);
            session.fragmentCache[bodyHash] = std::move(cached);

            auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - fragmentStart).count();
            LOG(L"[RedactionPipeline] TIMING total=" + std::to_wstring(totalUs) + L"us cache_lookup=" + std::to_wstring(cacheLookupUs) + L"us pii_model=" + std::to_wstring(piiModelUs) + L"us regex=" + std::to_wstring(regexUs) + L"us keywords=" + std::to_wstring(keywordsUs) + L"us forward_prop=" + std::to_wstring(forwardPropUs) + L"us");
        }
    }

    piiCount = state.piiMap.size();
    regexCount = state.regexMap.size();
    keywordCount = state.keywordMap.size();

    // Sensitive redaction logging (raw values, only when show-sensitive is on)
    if (logManager_->IsShowSensitive()) {
        for (const auto& [label, original] : state.piiMap) {
            auto itType = session.piiTypeMap.find(original);
            std::wstring type = (itType != session.piiTypeMap.end()) ? itType->second : L"unknown";
            LOG(L"[REDACTED: " + type + L"] raw=\"" + original + L"\"");
        }
        for (const auto& [label, original] : state.regexMap) {
            LOG(L"[REDACTED: regex] raw=\"" + original + L"\"");
        }
        for (const auto& [label, original] : state.keywordMap) {
            LOG(L"[REDACTED: keyword] raw=\"" + original + L"\"");
        }
    }

    // Record session matches
    {
        std::lock_guard<std::mutex> lock(matchMutex_);
        auto& tracker = matchTrackers_[profile.id];
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::wstring timeStr = Utils::FormatLocalizedTime(time);

        for (const auto& [label, original] : state.piiMap) {
            SessionMatch m;
            m.type = LocString(L"MatchType_PII");
            m.matchedText = original;
            auto itType = session.piiTypeMap.find(original);
            m.detail = (itType != session.piiTypeMap.end()) ? itType->second : L"";
            m.timestamp = timeStr;
            tracker.matches.push_back(m);
        }
        for (const auto& [label, original] : state.regexMap) {
            SessionMatch m;
            m.type = LocString(L"MatchType_Regex");
            m.matchedText = original;
            m.detail = L"";
            m.timestamp = timeStr;
            tracker.matches.push_back(m);
        }
        for (const auto& [label, original] : state.keywordMap) {
            SessionMatch m;
            m.type = LocString(L"MatchType_Keyword");
            m.matchedText = original;
            m.detail = L"";
            m.timestamp = timeStr;
            tracker.matches.push_back(m);
        }
        while (tracker.matches.size() > MatchTracker::MAX_MATCHES) {
            tracker.matches.pop_front();
        }
    }

    std::wstring summary = LocFormat(L"ProxyLog_Request", { Utils::Utf8ToWide(method), path });
    if (piiCount > 0) summary += L" | " + LocFormat(L"ProxyLog_PII", { std::to_wstring(piiCount) });
    if (regexCount > 0) summary += L" | " + LocFormat(L"ProxyLog_Regex", { std::to_wstring(regexCount) });
    if (keywordCount > 0) summary += L" | " + LocFormat(L"ProxyLog_Keywords", { std::to_wstring(keywordCount) });

    std::wstring details;
    if (logManager_->IsShowSensitive()) {
        std::wstring bodyPreview = state.originalText;
        if (bodyPreview.length() > 50000) bodyPreview = bodyPreview.substr(0, 50000) + L"...[truncated]";
        details = L"=== RAW REQUEST BODY (from client) ===\n" + bodyPreview + L"\n";
    }
    std::wstring redactedPreview = state.redactedText;
    if (redactedPreview.length() > 50000) redactedPreview = redactedPreview.substr(0, 50000) + L"...[truncated]";
    details += L"=== REDACTED REQUEST BODY (to upstream) ===\n" + redactedPreview;

    logManager_->AddLog(profile.alias, LogDirection::UserToProxy, summary, details);

    return Utils::WideToUtf8(state.redactedText);
}

std::string ProxyEngine::ProcessResponse(const ApiKeyProfile& profile, const std::string& responseBody,
    const std::vector<std::pair<std::wstring, std::wstring>>& responseHeaders,
    const RedactionState& state) {

    bool hadRedaction = !state.piiMap.empty() || !state.regexMap.empty() || !state.keywordMap.empty();

    std::string result;
    if (IsSSE(responseHeaders) && hadRedaction) {
        // Only rebuild SSE when we actually performed redaction
        result = RebuildSSE(responseBody, state);
    } else if (IsSSE(responseHeaders) && !hadRedaction) {
        // No redaction — pass through raw SSE body exactly as received
        result = responseBody;
    } else {
        std::wstring wResponse = Utils::Utf8ToWide(responseBody);
        wResponse = UnredactAll(wResponse, state);
        result = Utils::WideToUtf8(wResponse);
    }

    return result;
}

bool ProxyEngine::ForwardToUpstream(const std::wstring& upstreamUrl, const std::wstring& apiKey,
    const std::wstring& profileAlias,
    const std::string& method, const std::wstring& path,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    const std::string& body,
    int& statusCode,
    std::vector<std::pair<std::wstring, std::wstring>>& responseHeaders,
    std::string& responseBody) {

    responseBody.clear();
    bool ok = ForwardToUpstreamStreaming(upstreamUrl, apiKey, profileAlias, method, path, headers, body,
        statusCode, responseHeaders,
        [&responseBody](const char* data, size_t len) {
            responseBody.append(data, len);
            return true;
        },
        nullptr);
    if (!ok) return false;

    std::wstring upstreamResponsePreview = Utils::Utf8ToWide(responseBody);
    if (upstreamResponsePreview.length() > 50000) upstreamResponsePreview = upstreamResponsePreview.substr(0, 50000) + L"...[truncated]";

    std::wstring headerSummary;
    for (const auto& [name, value] : responseHeaders) {
        headerSummary += name + L": " + value + L"; ";
    }

    LOG(L"[Upstream] Response: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(responseBody.size()) + L" bytes");
    LOG(L"[Upstream] Response headers: " + headerSummary);
    LOG(L"[Upstream] Response body:\n" + upstreamResponsePreview);
    LOG_TRAFFIC(L"UPSTREAM_IN",
        std::to_wstring(statusCode) + L"\r\n" +
        headerSummary + L"\r\n" +
        Utils::Utf8ToWide(responseBody));
    logManager_->AddLog(profileAlias, LogDirection::LLMToProxy,
        L"Upstream response: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(responseBody.size()) + L" bytes",
        L"=== HEADERS FROM UPSTREAM ===\n" + headerSummary +
        L"\n=== BODY FROM UPSTREAM ===\n" + upstreamResponsePreview);

    return true;
}

bool ProxyEngine::ForwardToUpstreamStreaming(const std::wstring& upstreamUrl, const std::wstring& apiKey,
    const std::wstring& profileAlias,
    const std::string& method, const std::wstring& path,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    const std::string& body,
    int& statusCode,
    std::vector<std::pair<std::wstring, std::wstring>>& responseHeaders,
    std::function<bool(const char* data, size_t len)> onBodyChunk,
    std::function<void(int statusCode, const std::vector<std::pair<std::wstring, std::wstring>>& headers)> onHeaders) {

#ifdef _WIN32
    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    WinHttpCrackUrl(upstreamUrl.c_str(), 0, 0, &urlComp);

    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    INTERNET_PORT port = urlComp.nPort;
    DWORD scheme = urlComp.nScheme;

    HINTERNET hSession = WinHttpOpen(L"AgentRedactor/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    // Smart path concatenation: deduplicate overlapping segments
    // e.g. upstream=/api/v1 + incoming=/v1/chat → /api/v1/chat
    std::wstring fullPath = urlPath;
    if (!fullPath.empty() && fullPath.back() == L'/') fullPath.pop_back();

    if (!path.empty()) {
        size_t maxCommon = std::min(fullPath.length(), path.length());
        size_t common = 0;
        for (size_t i = 1; i <= maxCommon; ++i) {
            if (fullPath.substr(fullPath.length() - i) == path.substr(0, i)) {
                common = i;
            }
        }
        if (common > 0) {
            fullPath = fullPath.substr(0, fullPath.length() - common);
        }
        if (!fullPath.empty() && !path.empty() && fullPath.back() == L'/' && path.front() == L'/') {
            fullPath.pop_back();
        }
        fullPath += path;
    }
    std::wstring wMethod = Utils::Utf8ToWide(method);

    DWORD flags = (scheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    // Set explicit upstream timeouts so a stalled upstream cannot hang the proxy
    // indefinitely. Values are generous to account for large request bodies and
    // long streaming responses.
    WinHttpSetTimeouts(hRequest, 30000, 30000, 120000, 300000);

    // Enable automatic gzip/deflate decompression. WinHTTP adds its own
    // Accept-Encoding header when this option is enabled, so we strip any
    // client-provided Accept-Encoding below to avoid duplicate/conflicting
    // encodings.
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    // Build headers string, stripping hop-by-hop and accept-encoding headers.
    // Client credential headers (Authorization, x-api-key, api-key) are
    // forwarded with the real upstream key substituted for whatever value the
    // client sent — agents commonly send placeholders (e.g. Claude Code's
    // dummy ANTHROPIC_AUTH_TOKEN) and expect the proxy to hold the real key.
    // The client's auth style is preserved (Authorization keeps its scheme,
    // x-api-key/api-key take the raw key), so both OpenAI-style Bearer and
    // Anthropic-style x-api-key upstreams work without agent sniffing. If the
    // client sent no credential header at all, default to Authorization:
    // Bearer as before.
    auto isCredentialHeader = [](const std::wstring& lowerName) {
        return lowerName == L"authorization" || lowerName == L"x-api-key" || lowerName == L"api-key";
    };
    auto substituteKey = [](const std::wstring& lowerName, const std::wstring& value, const std::wstring& key) {
        if (lowerName == L"authorization") {
            size_t sp = value.find(L' ');
            if (sp != std::wstring::npos) {
                // Preserve the client's scheme (e.g. Bearer) when present.
                return value.substr(0, sp) + L" " + key;
            }
        }
        return key;
    };

    std::wstring headerString;
    std::wstring logHeaderString; // log-safe headers (API key masked unless show-sensitive mode is on)
    bool clientSentCredential = false;
    for (const auto& [name, value] : headers) {
        std::wstring lowerName = Utils::ToLower(name);
        if (lowerName == L"host" || lowerName == L"proxy-authorization") continue;
        if (lowerName == L"connection" || lowerName == L"keep-alive" || lowerName == L"proxy-connection" || lowerName == L"content-length" || lowerName == L"accept-encoding") continue;
        if (isCredentialHeader(lowerName)) {
            clientSentCredential = true;
            headerString += name + L": " + substituteKey(lowerName, value, apiKey) + L"\r\n";
            logHeaderString += name + L": " + substituteKey(lowerName, value, L"<REDACTED>") + L"\r\n";
            continue;
        }
        headerString += name + L": " + value + L"\r\n";
        logHeaderString += name + L": " + value + L"\r\n";
    }
    if (!clientSentCredential) {
        headerString += L"Authorization: Bearer " + apiKey + L"\r\n";
        logHeaderString += L"Authorization: Bearer <REDACTED>\r\n";
    }
    const std::wstring& headersForLog = logManager_->IsShowSensitive() ? headerString : logHeaderString;

    // Log what we're about to send upstream
    std::wstring upstreamBodyPreview = Utils::Utf8ToWide(body);
    if (upstreamBodyPreview.length() > 50000) upstreamBodyPreview = upstreamBodyPreview.substr(0, 50000) + L"...[truncated]";
    LOG(L"[Upstream] Request: " + wMethod + L" " + fullPath);
    LOG(L"[Upstream] Request headers:\n" + headersForLog);
    LOG(L"[Upstream] Request body:\n" + upstreamBodyPreview);
    LOG_TRAFFIC(L"UPSTREAM_OUT",
        wMethod + L" " + fullPath + L"\r\n" +
        headersForLog + L"\r\n" +
        Utils::Utf8ToWide(body));
    logManager_->AddLog(profileAlias, LogDirection::ProxyToLLM,
        L"Upstream request: " + wMethod + L" " + fullPath,
        L"=== HEADERS SENT TO UPSTREAM ===\n" + headersForLog +
        L"\n=== BODY SENT TO UPSTREAM ===\n" + upstreamBodyPreview);

    BOOL sent = WinHttpSendRequest(hRequest, headerString.c_str(), (DWORD)-1, (void*)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!sent) {
        logManager_->AddLog(profileAlias, LogDirection::ProxyToLLM,
            L"Upstream request FAILED", L"WinHttpSendRequest failed");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL received = WinHttpReceiveResponse(hRequest, nullptr);
    if (!received) {
        logManager_->AddLog(profileAlias, LogDirection::LLMToProxy,
            L"Upstream response FAILED", L"WinHttpReceiveResponse failed");
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    // Read response headers
    DWORD headerSize = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &headerSize, WINHTTP_NO_HEADER_INDEX);
    if (headerSize > 0) {
        std::vector<wchar_t> rawHeaders(headerSize / sizeof(wchar_t));
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, rawHeaders.data(), &headerSize, WINHTTP_NO_HEADER_INDEX)) {
            std::wstring headersStr(rawHeaders.data());
            std::wistringstream headerStream(headersStr);
            std::wstring line;
            while (std::getline(headerStream, line)) {
                if (line.empty() || line == L"\r") continue;
                size_t colon = line.find(L':');
                if (colon != std::wstring::npos) {
                    std::wstring name = line.substr(0, colon);
                    std::wstring value = line.substr(colon + 1);
                    size_t start = value.find_first_not_of(L" \t\r");
                    if (start != std::wstring::npos) value = value.substr(start);
                    responseHeaders.push_back({name, value});
                }
            }
        }
    }

    std::wstring headerSummary;
    for (const auto& [name, value] : responseHeaders) {
        headerSummary += name + L": " + value + L"; ";
    }
    LOG(L"[Upstream] Response headers: " + headerSummary);

    if (onHeaders) {
        onHeaders(statusCode, responseHeaders);
    }

    // Stream body chunks as they arrive
    DWORD bytesAvailable = 0;
    size_t totalBytes = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            if (bytesRead > 0) {
                if (!onBodyChunk(buffer.data(), bytesRead)) {
                    LOG(L"[Upstream] Streaming aborted by consumer");
                    break;
                }
                totalBytes += bytesRead;
            }
        }
    }

    LOG(L"[Upstream] Streamed response: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(totalBytes) + L" bytes");
    logManager_->AddLog(profileAlias, LogDirection::LLMToProxy,
        L"Upstream streaming response: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(totalBytes) + L" bytes",
        L"=== HEADERS FROM UPSTREAM ===\n" + headerSummary);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;

#else // POSIX: libcurl upstream client
    // NOTE: the path-concatenation, credential-substitution and logging logic
    // below mirrors the WinHTTP branch above; keep the two in sync.

    // Crack upstreamUrl into scheme/host[:port]/path (WinHttpCrackUrl
    // counterpart; curl re-parses the port from the rebuilt URL).
    const std::string upstreamNarrow = Utils::WideToUtf8(upstreamUrl);
    std::string scheme = "http";
    std::string hostPort;
    std::wstring urlPath;
    {
        std::string rest = upstreamNarrow;
        const size_t schemeEnd = rest.find("://");
        if (schemeEnd != std::string::npos) {
            scheme = rest.substr(0, schemeEnd);
            rest = rest.substr(schemeEnd + 3);
        }
        const size_t slash = rest.find('/');
        hostPort = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        urlPath = Utils::Utf8ToWide((slash == std::string::npos) ? "" : rest.substr(slash));
    }

    // Smart path concatenation: deduplicate overlapping segments
    // e.g. upstream=/api/v1 + incoming=/v1/chat → /api/v1/chat
    std::wstring fullPath = urlPath;
    if (!fullPath.empty() && fullPath.back() == L'/') fullPath.pop_back();

    if (!path.empty()) {
        size_t maxCommon = std::min(fullPath.length(), path.length());
        size_t common = 0;
        for (size_t i = 1; i <= maxCommon; ++i) {
            if (fullPath.substr(fullPath.length() - i) == path.substr(0, i)) {
                common = i;
            }
        }
        if (common > 0) {
            fullPath = fullPath.substr(0, fullPath.length() - common);
        }
        if (!fullPath.empty() && !path.empty() && fullPath.back() == L'/' && path.front() == L'/') {
            fullPath.pop_back();
        }
        fullPath += path;
    }
    std::wstring wMethod = Utils::Utf8ToWide(method);

    // Build headers, stripping hop-by-hop and accept-encoding headers.
    // Client credential headers (Authorization, x-api-key, api-key) are
    // forwarded with the real upstream key substituted for whatever value the
    // client sent — agents commonly send placeholders (e.g. Claude Code's
    // dummy ANTHROPIC_AUTH_TOKEN) and expect the proxy to hold the real key.
    // The client's auth style is preserved (Authorization keeps its scheme,
    // x-api-key/api-key take the raw key), so both OpenAI-style Bearer and
    // Anthropic-style x-api-key upstreams work without agent sniffing. If the
    // client sent no credential header at all, default to Authorization:
    // Bearer as before.
    auto isCredentialHeader = [](const std::wstring& lowerName) {
        return lowerName == L"authorization" || lowerName == L"x-api-key" || lowerName == L"api-key";
    };
    auto substituteKey = [](const std::wstring& lowerName, const std::wstring& value, const std::wstring& key) {
        if (lowerName == L"authorization") {
            size_t sp = value.find(L' ');
            if (sp != std::wstring::npos) {
                // Preserve the client's scheme (e.g. Bearer) when present.
                return value.substr(0, sp) + L" " + key;
            }
        }
        return key;
    };

    std::wstring headerString;
    std::wstring logHeaderString; // log-safe headers (API key masked unless show-sensitive mode is on)
    bool clientSentCredential = false;
    for (const auto& [name, value] : headers) {
        std::wstring lowerName = Utils::ToLower(name);
        if (lowerName == L"host" || lowerName == L"proxy-authorization") continue;
        if (lowerName == L"connection" || lowerName == L"keep-alive" || lowerName == L"proxy-connection" || lowerName == L"content-length" || lowerName == L"accept-encoding") continue;
        if (isCredentialHeader(lowerName)) {
            clientSentCredential = true;
            headerString += name + L": " + substituteKey(lowerName, value, apiKey) + L"\r\n";
            logHeaderString += name + L": " + substituteKey(lowerName, value, L"<REDACTED>") + L"\r\n";
            continue;
        }
        headerString += name + L": " + value + L"\r\n";
        logHeaderString += name + L": " + value + L"\r\n";
    }
    if (!clientSentCredential) {
        headerString += L"Authorization: Bearer " + apiKey + L"\r\n";
        logHeaderString += L"Authorization: Bearer <REDACTED>\r\n";
    }
    const std::wstring& headersForLog = logManager_->IsShowSensitive() ? headerString : logHeaderString;

    // Log what we're about to send upstream
    std::wstring upstreamBodyPreview = Utils::Utf8ToWide(body);
    if (upstreamBodyPreview.length() > 50000) upstreamBodyPreview = upstreamBodyPreview.substr(0, 50000) + L"...[truncated]";
    LOG(L"[Upstream] Request: " + wMethod + L" " + fullPath);
    LOG(L"[Upstream] Request headers:\n" + headersForLog);
    LOG(L"[Upstream] Request body:\n" + upstreamBodyPreview);
    LOG_TRAFFIC(L"UPSTREAM_OUT",
        wMethod + L" " + fullPath + L"\r\n" +
        headersForLog + L"\r\n" +
        Utils::Utf8ToWide(body));
    logManager_->AddLog(profileAlias, LogDirection::ProxyToLLM,
        L"Upstream request: " + wMethod + L" " + fullPath,
        L"=== HEADERS SENT TO UPSTREAM ===\n" + headersForLog +
        L"\n=== BODY SENT TO UPSTREAM ===\n" + upstreamBodyPreview);

    struct UpstreamCurlContext {
        CURL* curl = nullptr;
        int statusCode = 0;
        std::vector<std::pair<std::wstring, std::wstring>>* responseHeaders = nullptr;
        std::function<bool(const char*, size_t)>* onBodyChunk = nullptr;
        std::function<void(int, const std::vector<std::pair<std::wstring, std::wstring>>& )>* onHeaders = nullptr;
        bool abortedByConsumer = false;
        size_t totalBytes = 0;
    };

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    UpstreamCurlContext ctx;
    ctx.curl = curl;
    ctx.responseHeaders = &responseHeaders;
    ctx.onBodyChunk = &onBodyChunk;
    ctx.onHeaders = &onHeaders;

    const std::string fullUrl = scheme + "://" + hostPort + Utils::WideToUtf8(fullPath.empty() ? L"/" : fullPath);
    curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AgentRedactor/1.0");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    // Explicit upstream timeouts so a stalled upstream cannot hang the proxy
    // indefinitely (30 s connect; abort when the transfer stalls below
    // 1 byte/s for 300 s, matching the WinHTTP receive timeout).
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 300L);
    // Automatic gzip/deflate decompression (curl adds its own Accept-Encoding;
    // any client-provided one is stripped above, like the WinHTTP branch).
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    struct curl_slist* headerList = nullptr;
    for (const auto& h : Utils::Split(headerString, L'\n')) {
        const std::string narrow = Utils::WideToUtf8(Utils::Trim(h));
        if (!narrow.empty()) headerList = curl_slist_append(headerList, narrow.c_str());
    }
    // WinHTTP never sends Expect: 100-continue; suppress curl's automatic one
    // so onHeaders fires exactly once with the final response.
    headerList = curl_slist_append(headerList, "Expect:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

    auto headerCb = +[](char* buffer, size_t size, size_t nitems, void* userdata) -> size_t {
        const size_t len = size * nitems;
        auto* c = static_cast<UpstreamCurlContext*>(userdata);
        std::string line(buffer, len);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) {
            // End of a header block: invoke onHeaders with the final status
            // (interim 1xx blocks are skipped).
            long status = 0;
            curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &status);
            if (status >= 200) {
                c->statusCode = static_cast<int>(status);
                if (c->onHeaders && *c->onHeaders) {
                    (*c->onHeaders)(c->statusCode, *c->responseHeaders);
                }
            }
            return len;
        }
        const size_t colon = line.find(':');
        if (colon != std::string::npos && colon > 0) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            const size_t start = value.find_first_not_of(" \t");
            if (start != std::string::npos) value = value.substr(start);
            c->responseHeaders->push_back({Utils::Utf8ToWide(name), Utils::Utf8ToWide(value)});
        }
        return len;
    };
    auto writeCb = +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        const size_t len = size * nmemb;
        auto* c = static_cast<UpstreamCurlContext*>(userdata);
        if (len > 0 && c->onBodyChunk && *c->onBodyChunk) {
            if (!(*c->onBodyChunk)(ptr, len)) {
                c->abortedByConsumer = true;
                return 0;
            }
            c->totalBytes += len;
        }
        return len;
    };
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    const CURLcode res = curl_easy_perform(curl);
    statusCode = ctx.statusCode;
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK && !ctx.abortedByConsumer) {
        logManager_->AddLog(profileAlias, LogDirection::LLMToProxy,
            L"Upstream response FAILED",
            L"curl_easy_perform failed: " + Utils::Utf8ToWide(curl_easy_strerror(res)));
        return false;
    }
    if (ctx.abortedByConsumer) {
        LOG(L"[Upstream] Streaming aborted by consumer");
    }

    std::wstring headerSummary;
    for (const auto& [name, value] : responseHeaders) {
        headerSummary += name + L": " + value + L"; ";
    }
    LOG(L"[Upstream] Response headers: " + headerSummary);
    LOG(L"[Upstream] Streamed response: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(ctx.totalBytes) + L" bytes");
    logManager_->AddLog(profileAlias, LogDirection::LLMToProxy,
        L"Upstream streaming response: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(ctx.totalBytes) + L" bytes",
        L"=== HEADERS FROM UPSTREAM ===\n" + headerSummary);
    return true;
#endif
}

void ProxyEngine::UpdateStats(const ApiKeyProfile& profile, size_t piiCount, size_t regexCount, size_t keywordCount) {
    // Stats are updated via the settings manager externally
    // This is a placeholder for future real-time stat updates
    if (onUpdate_) {
        onUpdate_();
    }
}

void ProxyEngine::ClearSessionMatches(const std::wstring& profileId) {
    std::lock_guard<std::mutex> lock(matchMutex_);
    auto it = matchTrackers_.find(profileId);
    if (it != matchTrackers_.end()) {
        it->second.matches.clear();
    }
}

std::vector<SessionMatch> ProxyEngine::GetSessionMatches(const std::wstring& profileId) const {
    std::lock_guard<std::mutex> lock(matchMutex_);
    std::vector<SessionMatch> result;
    auto it = matchTrackers_.find(profileId);
    if (it != matchTrackers_.end()) {
        result.assign(it->second.matches.begin(), it->second.matches.end());
    }
    return result;
}

} // namespace AgentRedactor
