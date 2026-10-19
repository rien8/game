/// @file diagnostic.hpp
/// @brief 诊断通道：World 在结构操作失败时产出的结构化消息，经可替换的
///        DiagnosticHandler 交回调用方，而不是直接终止或静默吞掉。
///
/// 每条诊断由稳定的 code（如 ecs.entity_invalid）与人读的 message 两部分
/// 组成：工具按 code 匹配，日志按 message 展示，两者分离使文案调整不会打断
/// 按 code 的检索。默认处理器写 stderr，World 构造后可整体替换。
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace gamev2::ecs {

/// 诊断的严重程度。error 表示调用方拿到的结果与预期不符，warning 表示已按
/// 容错语义处理掉。
enum class DiagnosticLevel
{
    warning,
    error,
};

/// 一条诊断。code 供工具匹配，message 面向人。
struct Diagnostic
{
    DiagnosticLevel level = DiagnosticLevel::warning;
    /// 稳定标识，供工具按 code 匹配，不随文案变化。
    std::string_view code;
    std::string message;
};

/// 诊断处理器的签名。可换成任意可调用对象，因此测试可以就地收集诊断而不必
/// 去读 stderr。
using DiagnosticHandler = std::function<void(const Diagnostic&)>;

/// 默认处理器把诊断写到 stderr。World 构造时可替换。
[[nodiscard]] DiagnosticHandler default_diagnostic_handler();

/// 把一条诊断交给处理器。World::report 是它对内的唯一调用入口。
void emit_diagnostic(const Diagnostic& diag,
                     const DiagnosticHandler& handler);

}  // namespace gamev2::ecs
