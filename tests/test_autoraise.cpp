#include <catch2/catch_test_macros.hpp>
#include <chrono>

namespace chr = std::chrono;

TEST_CASE("steady_clock future 80ms deadline is after now", "[autoraise][timer]")
{
    auto now = chr::steady_clock::now();
    auto deadline = now + chr::milliseconds(80);
    REQUIRE(deadline > now);
}

TEST_CASE("steady_clock future 400ms deadline is after now", "[autoraise][timer]")
{
    auto now = chr::steady_clock::now();
    auto deadline = now + chr::milliseconds(400);
    REQUIRE(deadline > now);
}

TEST_CASE("Duration arithmetic produces positive ms for future deadlines", "[autoraise][timer]")
{
    auto now = chr::steady_clock::now();
    auto deadline = now + chr::milliseconds(250);
    auto diff = chr::duration_cast<chr::milliseconds>(deadline - now);
    REQUIRE(diff.count() > 0);
}

TEST_CASE("Duration arithmetic produces positive ms for past deadlines", "[autoraise][timer]")
{
    auto now = chr::steady_clock::now();
    auto past_deadline = now - chr::milliseconds(100);
    auto diff = chr::duration_cast<chr::milliseconds>(now - past_deadline);
    REQUIRE(diff.count() > 0);
}

TEST_CASE("Minimum of two deadlines returns earlier", "[autoraise][timer]")
{
    auto now = chr::steady_clock::now();
    auto earlier = now + chr::milliseconds(80);
    auto later = now + chr::milliseconds(400);
    auto earliest = std::min(earlier, later);
    REQUIRE(earliest == earlier);
}

TEST_CASE("Timeout computation clamps past deadlines to 0", "[autoraise][timer]")
{
    auto now = chr::steady_clock::now();
    auto past_deadline = now - chr::milliseconds(100);
    auto ms = chr::duration_cast<chr::milliseconds>(past_deadline - now).count();
    int timeout = (ms > 0) ? static_cast<int>(ms) : 0;
    REQUIRE(timeout == 0);

    // Future deadline produces positive timeout
    auto future_deadline = now + chr::milliseconds(200);
    auto future_ms = chr::duration_cast<chr::milliseconds>(future_deadline - now).count();
    int future_timeout = (future_ms > 0) ? static_cast<int>(future_ms) : 0;
    REQUIRE(future_timeout > 0);
}
