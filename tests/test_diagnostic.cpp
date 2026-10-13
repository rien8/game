/// UCRT 以不安全临时路径为由弃用 tmpfile；测试进程不受该威胁模型影响，
/// 且构建要求零警告，故在包含 CRT 头之前先行关闭该弃用检查。
#define _CRT_SECURE_NO_WARNINGS

#include "test_framework.hpp"

#include "gamev2/ecs/diagnostic.hpp"

#include <cstdio>
#include <io.h>
#include <string>

using gamev2::ecs::Diagnostic;
using gamev2::ecs::DiagnosticLevel;
using gamev2::ecs::emit_diagnostic;

TEST(emit_diagnostic_forwards_every_field_to_handler)
{
    int calls = 0;
    Diagnostic seen{};

    emit_diagnostic(Diagnostic{DiagnosticLevel::error, "ECS_TEST_CODE",
                                "test message"},
                    [&](const Diagnostic& d)
                    {
                        ++calls;
                        seen = d;
                    });

    CHECK_EQ(calls, 1);
    CHECK(seen.level == DiagnosticLevel::error);
    CHECK(seen.code == "ECS_TEST_CODE");
    CHECK(seen.message == "test message");
}

TEST(emit_diagnostic_falls_back_to_default_when_handler_is_empty)
{
    // 空 handler 必须回退到默认处理器而非静默丢弃，stderr 会因此被写入。
    // 把 stderr 接到临时文件上，既让回退可被断言，又不让噪声淹没真实失败。
    FILE* sink = std::tmpfile();
    CHECK(sink != nullptr);
    if (sink == nullptr)
    {
        return;
    }

    const int saved = _dup(_fileno(stderr));
    CHECK(saved != -1);
    if (saved == -1)
    {
        std::fclose(sink);
        return;
    }
    CHECK_EQ(_dup2(_fileno(sink), _fileno(stderr)), 0);

    emit_diagnostic(Diagnostic{DiagnosticLevel::error, "ECS_EMPTY_HANDLER",
                                "empty handler falls back to default"},
                    {});

    // 必须还原，否则后续测试的 stderr 会全部丢失。
    _dup2(saved, _fileno(stderr));
    _close(saved);

    std::rewind(sink);
    char buf[256]{};
    const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, sink);
    std::fclose(sink);

    const std::string captured(buf, n);
    CHECK(captured.find("ECS_EMPTY_HANDLER") != std::string::npos);
    CHECK(captured.find("empty handler falls back to default")
          != std::string::npos);
}
