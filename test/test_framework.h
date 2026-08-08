// Minimal zero-dependency test framework: self-registering cases, string diffs.
#pragma once

#include <string>
#include <vector>

struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& allTests();

struct TestRegistrar {
    TestRegistrar(const char* suite, const char* name, void (*fn)());
};

void reportFailure(const char* file, int line, const std::string& what,
                   const std::string& actual, const std::string& expected);
void countCheck();

#define TEST(suite, name)                                                        \
    static void suite##_##name##_body();                                         \
    static TestRegistrar suite##_##name##_reg(#suite, #name, &suite##_##name##_body); \
    static void suite##_##name##_body()

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        countCheck();                                                            \
        auto _a = (actual);                                                      \
        auto _e = (expected);                                                    \
        if (!(_a == _e)) {                                                       \
            reportFailure(__FILE__, __LINE__, #actual, testToString(_a), testToString(_e)); \
        }                                                                        \
    } while (0)

#define CHECK_TRUE(expr)                                                         \
    do {                                                                         \
        countCheck();                                                            \
        if (!(expr)) {                                                           \
            reportFailure(__FILE__, __LINE__, #expr, "false", "true");           \
        }                                                                        \
    } while (0)

std::string testToString(const std::string& s);
std::string testToString(const char* s);
std::string testToString(const std::wstring& s);
std::string testToString(int v);
std::string testToString(size_t v);
std::string testToString(bool v);
std::string testToString(double v);
