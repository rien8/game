#include "gamev2/ecs/diagnostic.hpp"

#include <cstdio>

namespace gamev2::ecs {

DiagnosticHandler default_diagnostic_handler()
{
    return [](const Diagnostic& diag)
    {
        const char* level =
            diag.level == DiagnosticLevel::error ? "error" : "warn";
        /// std::print 只能写 stdout，std::print_to 在本工具链的 MSVC STL
        /// 中尚未提供，因此诊断通道只能退回 fprintf。
        std::fprintf(stderr, "[%s] %.*s: %s\n", level,
                     static_cast<int>(diag.code.size()), diag.code.data(),
                     diag.message.c_str());
    };
}

void emit_diagnostic(const Diagnostic& diag,
                     const DiagnosticHandler& handler)
{
    if (!handler)
    {
        default_diagnostic_handler()(diag);
        return;
    }
    handler(diag);
}

}  // namespace gamev2::ecs
