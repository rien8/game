#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace gamev2::ecs {

enum class DiagnosticLevel
{
    warning,
    error,
};

struct Diagnostic
{
    DiagnosticLevel level = DiagnosticLevel::warning;
    /// 稳定标识，供工具按 code 匹配，不随文案变化。
    std::string_view code;
    std::string message;
};

using DiagnosticHandler = std::function<void(const Diagnostic&)>;

/// 默认处理器把诊断写到 stderr。World 构造时可替换。
[[nodiscard]] DiagnosticHandler default_diagnostic_handler();

void emit_diagnostic(const Diagnostic& diag,
                     const DiagnosticHandler& handler);

}  // namespace gamev2::ecs
