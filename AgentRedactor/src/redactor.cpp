#include "redactor.h"
#include "utils.h"
#include "localization.h"
#include <algorithm>
#include <sstream>

namespace AgentRedactor {

Redactor::Redactor(const std::wstring& templateString)
    : template_(templateString) {
}

std::wstring Redactor::RedactText(const std::wstring& text, const std::vector<PIIEntity>& entities) {
    if (entities.empty()) return text;
    std::vector<PIIEntity> sortedEntities = entities;
    std::sort(sortedEntities.begin(), sortedEntities.end(),
              [](const PIIEntity& a, const PIIEntity& b) { return a.start > b.start; });

    std::wstring result = text;
    for (const auto& entity : sortedEntities) {
        if (entity.start < result.size() && entity.end <= result.size()) {
            std::wstring placeholder = L"[REDACTED ";
            std::wstring upperType = entity.type;
            std::transform(upperType.begin(), upperType.end(), upperType.begin(), ::towupper);
            size_t pos = 0;
            while ((pos = upperType.find(L'_', pos)) != std::wstring::npos) {
                upperType.replace(pos, 1, L" ");
                pos += 1;
            }
            placeholder += upperType + L"]";
            result.replace(entity.start, entity.end - entity.start, placeholder);
        }
    }
    return result;
}

std::wstring Redactor::FormatEntity(const PIIEntity& entity) const {
    std::wostringstream oss;
    oss << entity.type << L": " << entity.text;
    return oss.str();
}

std::wstring Redactor::GetRedactionSummary(size_t entityCount) const {
    if (entityCount == 0) return LocString(L"Redactor_NoPIIDetected");
    if (entityCount == 1) return LocString(L"Redactor_OnePIIItem");
    return LocFormat(L"Redactor_ManyPIIItems", { std::to_wstring(entityCount) });
}

} // namespace AgentRedactor
