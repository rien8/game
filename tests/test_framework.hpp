#pragma once

#include <print>
#include <string_view>
#include <vector>

namespace test {

struct TestCase
{
    std::string_view name;
    void (*fn)();
};

inline int& failures()
{
    static int n = 0;
    return n;
}

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar
{
    Registrar(std::string_view name, void (*fn)())
    {
        registry().push_back({name, fn});
    }
};

int run_all();

}  // namespace test

#define TEST(name)                                            \
    static void name();                                       \
    static const ::test::Registrar reg_##name{#name, &name};  \
    static void name()

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::println("  FAIL {}:{}  {}", __FILE__, __LINE__, #cond);   \
            ::test::failures()++;                                        \
        }                                                                \
    } while (false)

#define CHECK_EQ(a, b)                                   \
    do {                                                 \
        if (!((a) == (b))) {                             \
            std::println("  FAIL {}:{}  {} == {}",        \
                        __FILE__, __LINE__, #a, #b);     \
            ::test::failures()++;                        \
        }                                                \
    } while (false)
