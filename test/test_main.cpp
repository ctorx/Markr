#include "test_framework.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

std::vector<TestCase>& allTests() {
    static std::vector<TestCase> tests;
    return tests;
}

TestRegistrar::TestRegistrar(const char* suite, const char* name, void (*fn)()) {
    allTests().push_back(TestCase{suite, name, fn});
}

namespace {
int g_checks = 0;
int g_failuresInCase = 0;
int g_totalFailures = 0;
} // namespace

void countCheck() { ++g_checks; }

void reportFailure(const char* file, int line, const std::string& what,
                   const std::string& actual, const std::string& expected) {
    ++g_failuresInCase;
    ++g_totalFailures;
    std::printf("    FAIL %s:%d\n      expr    : %s\n      actual  : %s\n      expected: %s\n",
                file, line, what.c_str(), actual.c_str(), expected.c_str());
}

std::string testToString(const std::string& s) { return "\"" + s + "\""; }
std::string testToString(const char* s) { return std::string("\"") + s + "\""; }
std::string testToString(int v) { return std::to_string(v); }
std::string testToString(size_t v) { return std::to_string(v); }
std::string testToString(bool v) { return v ? "true" : "false"; }
std::string testToString(double v) { return std::to_string(v); }

std::string testToString(const std::wstring& s) {
    // Tests only use ASCII-ish expectations; escape anything wider.
    std::string out = "\"";
    for (wchar_t c : s) {
        if (c < 128 && c >= 32) {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
            out += buf;
        }
    }
    out.push_back('"');
    return out;
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;

    int run = 0;
    int failedCases = 0;
    std::string currentSuite;

    for (const TestCase& tc : allTests()) {
        std::string full = std::string(tc.suite) + "." + tc.name;
        if (filter && full.find(filter) == std::string::npos) continue;

        if (currentSuite != tc.suite) {
            currentSuite = tc.suite;
            std::printf("[%s]\n", currentSuite.c_str());
        }

        g_failuresInCase = 0;
        ++run;
        // Unbuffered trace so a crashing case is identifiable: set MDTEST_TRACE=1.
        if (std::getenv("MDTEST_TRACE")) std::fprintf(stderr, "-> %s\n", full.c_str());
        tc.fn();
        if (g_failuresInCase == 0) {
            std::printf("  ok   %s\n", tc.name);
        } else {
            std::printf("  FAIL %s (%d)\n", tc.name, g_failuresInCase);
            ++failedCases;
        }
    }

    std::printf("\n%d tests, %d checks, %d failures\n", run, g_checks, g_totalFailures);
    return failedCases == 0 ? 0 : 1;
}
