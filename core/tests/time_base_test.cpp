// Host checks for the picture clock's wrap (core/viz/clock/TimeBase.hpp): below the period it is the plain sum, past it
// the clock keeps running until a seam, a seam takes away whole periods only, and no seam lets it past the float
// precision ceiling.
#include <cmath>
#include <cstdio>
#include <string>

#include "viz/clock/TimeBase.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void belowThePeriodItIsThePlainSum() {
    double t = 100.0;
    timebase::step({0.25, false}, t);
    check(t == 100.25, "a step below the period is the plain sum");
    timebase::step({0.25, true}, t);
    check(t == 100.5, "a seam below the period changes nothing");
}

void pastThePeriodItRunsOnUntilASeam() {
    double t = timebase::kPeriodSeconds - 0.5;
    timebase::step({1.0, false}, t);
    check(std::fabs(t - (timebase::kPeriodSeconds + 0.5)) < 1e-9, "no seam: the clock goes past the period without a jump");
    timebase::step({1.0, true}, t);
    check(std::fabs(t - 1.5) < 1e-9, "a seam takes away exactly the period (" + std::to_string(t) + ")");
}

void aSeamKeepsEveryPeriodicTermWhereItWas() {
    const double before = timebase::kPeriodSeconds + 123.456;
    double t = before;
    timebase::step({0.0, true}, t);
    const double turns = (before - t) / timebase::kPeriodSeconds;
    check(std::fabs(turns - std::round(turns)) < 1e-12 && std::round(turns) >= 1.0, "the seam removes whole periods only");
}

void withoutASeamThePrecisionCeilingWraps() {
    double t = timebase::kPrecisionCeilingSeconds - 0.5;
    timebase::step({1.0, false}, t);
    check(t < timebase::kPeriodSeconds, "past the ceiling the clock wraps with no seam (" + std::to_string(t) + ")");
    check(timebase::kPrecisionCeilingSeconds == 8192.0, "the ceiling is the power of two above the period");
}

void aReverseStopsAtZero() {
    double t = 0.1;
    timebase::step({-1.0, false}, t);
    check(t == 0.0, "a reverse at the start of the clock stops at 0");
}

}  // namespace

int main() {
    belowThePeriodItIsThePlainSum();
    pastThePeriodItRunsOnUntilASeam();
    aSeamKeepsEveryPeriodicTermWhereItWas();
    withoutASeamThePrecisionCeilingWraps();
    aReverseStopsAtZero();
    if (failures) {
        std::printf("time_base_test: %d failed\n", failures);
        return 1;
    }
    std::printf("time_base_test: all passed\n");
    return 0;
}
