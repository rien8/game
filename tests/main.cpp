#include "test_framework.hpp"

#include <exception>
#include <print>

namespace test {

int run_all()
{
    int failed = 0;
    for (const auto& tc : registry())
    {
        failures() = 0;
        std::println("[ RUN  ] {}", tc.name);
        // 一个用例抛异常不该让整轮测试跑不完：记一次失败后继续，
        // 否则异常逃出 main 触发 terminate，CI 只会看到挂死，
        // 拿不到指向具体用例的失败行。
        try
        {
            tc.fn();
        }
        catch (const std::exception& ex)
        {
            std::println("  THROW {}  {}", tc.name, ex.what());
            ++failures();
        }
        catch (...)
        {
            std::println("  THROW {}  (non-std exception)", tc.name);
            ++failures();
        }
        if (failures() == 0)
        {
            std::println("[  OK  ] {}", tc.name);
        }
        else
        {
            std::println("[ FAIL ] {}  ({})", tc.name, failures());
            ++failed;
        }
    }
    const int total = static_cast<int>(registry().size());
    std::println("{} test(s), {} failed", total, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace test

int main()
{
    return test::run_all();
}
