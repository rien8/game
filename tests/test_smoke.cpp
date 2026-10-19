#include "test_framework.hpp"

TEST(suite_runs)
{
    CHECK(true);
}

TEST(failure_counter_starts_at_zero)
{
    CHECK_EQ(::test::failures(), 0);
}
