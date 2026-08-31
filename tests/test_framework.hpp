// test_framework.hpp -- Minimal self-registering test harness.
//
// Small enough to read in one sitting and with no external dependency, which
// matters for a project that must build with nothing but a C++ compiler.
#ifndef FLIGHTNET_TEST_FRAMEWORK_HPP
#define FLIGHTNET_TEST_FRAMEWORK_HPP

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    void (*fn)();
};

// Single registry shared by every translation unit. Function-local static so
// there is no static initialization order problem between test files.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        TestCase tc;
        tc.name = name;
        tc.fn   = fn;
        registry().push_back(tc);
    }
};

// Failures inside the current test, reported by the CHECK macros.
inline std::vector<std::string>& failures() {
    static std::vector<std::string> f;
    return f;
}

inline void fail(const char* file, int line, const std::string& message) {
    std::ostringstream os;
    os << file << ":" << line << ": " << message;
    failures().push_back(os.str());
}

// Runs everything and returns a process exit code. `filter`, when non-empty,
// selects tests whose name contains it.
inline int runAll(const std::string& filter = std::string()) {
    int passed = 0, failed = 0, skipped = 0;
    std::vector<std::string> failedNames;

    for (std::size_t i = 0; i < registry().size(); ++i) {
        const TestCase& tc = registry()[i];
        if (!filter.empty() && tc.name.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }
        failures().clear();
        try {
            tc.fn();
        } catch (const std::exception& e) {
            failures().push_back(std::string("unexpected exception: ") + e.what());
        } catch (...) {
            failures().push_back("unexpected non-standard exception");
        }

        if (failures().empty()) {
            std::cout << "  [ ok ] " << tc.name << "\n";
            ++passed;
        } else {
            std::cout << "  [FAIL] " << tc.name << "\n";
            for (std::size_t f = 0; f < failures().size(); ++f)
                std::cout << "         " << failures()[f] << "\n";
            ++failed;
            failedNames.push_back(tc.name);
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed";
    if (skipped) std::cout << ", " << skipped << " skipped";
    std::cout << "\n";
    if (failed) {
        std::cout << "failing tests:\n";
        for (std::size_t i = 0; i < failedNames.size(); ++i)
            std::cout << "  " << failedNames[i] << "\n";
    }
    return failed == 0 ? 0 : 1;
}

} // namespace testing

// --- registration ----------------------------------------------------------

#define TEST(name)                                                        \
    static void name();                                                   \
    static ::testing::Registrar registrar_##name(#name, &name);           \
    static void name()

// --- assertions ------------------------------------------------------------
// CHECK_* record a failure and keep going, so one test can report several
// problems at once. REQUIRE stops the test, for when continuing would crash.

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) ::testing::fail(__FILE__, __LINE__,                  \
                                     "CHECK failed: " #cond);             \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        auto _va = (a);                                                   \
        auto _vb = (b);                                                   \
        if (!(_va == _vb)) {                                              \
            std::ostringstream _os;                                       \
            _os << "CHECK_EQ failed: " #a " == " #b                       \
                << "  (" << _va << " vs " << _vb << ")";                  \
            ::testing::fail(__FILE__, __LINE__, _os.str());               \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                             \
    do {                                                                  \
        const double _va = (a), _vb = (b);                                \
        if (std::fabs(_va - _vb) > (tol)) {                               \
            std::ostringstream _os;                                       \
            _os << "CHECK_NEAR failed: " #a " ~= " #b                     \
                << "  (" << _va << " vs " << _vb << ")";                  \
            ::testing::fail(__FILE__, __LINE__, _os.str());               \
        }                                                                 \
    } while (0)

// Asserts that `expr` throws `exceptionType`.
#define CHECK_THROWS(expr, exceptionType)                                 \
    do {                                                                  \
        bool _threw = false;                                              \
        try { (void)(expr); }                                             \
        catch (const exceptionType&) { _threw = true; }                   \
        catch (...) {                                                     \
            ::testing::fail(__FILE__, __LINE__,                           \
                            "CHECK_THROWS: wrong exception from " #expr); \
            _threw = true;                                                \
        }                                                                 \
        if (!_threw) ::testing::fail(__FILE__, __LINE__,                  \
                                     "CHECK_THROWS: no throw from " #expr); \
    } while (0)

#define REQUIRE(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ::testing::fail(__FILE__, __LINE__,                           \
                            "REQUIRE failed: " #cond);                    \
            return;                                                       \
        }                                                                 \
    } while (0)

#endif // FLIGHTNET_TEST_FRAMEWORK_HPP
