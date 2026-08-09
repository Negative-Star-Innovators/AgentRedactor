#pragma once

#include <string>
#include <vector>
#include "pii_detector.h"

namespace AgentRedactor {

class Redactor {
public:
    explicit Redactor(const std::wstring& templateString = L"[REDACTED]");
    const std::wstring& GetTemplate() const { return template_; }
    std::wstring RedactText(const std::wstring& text, const std::vector<PIIEntity>& entities);
    std::wstring FormatEntity(const PIIEntity& entity) const;
    std::wstring GetRedactionSummary(size_t entityCount) const;

private:
    std::wstring template_;
};

} // namespace AgentRedactor
