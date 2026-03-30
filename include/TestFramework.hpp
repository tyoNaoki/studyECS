#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <sstream>
#include <cstdarg>
#include <cstdio>
#include <fstream>

/// <summary>
/*
///Priority だけを走らせたいテスト
TEST_CASE_PRIORITY(test_critical) {
    ASSERT(1 + 1 == 2, "1+1==2 critical check");
}

//Priority + OrderPriority を走らせたいテスト
TEST_CASE_ORDER(test_seq1) {
    MESSAGE("sequence 1");
}
TEST_CASE_ORDER(test_seq2) {
    MESSAGE("sequence 2");
}

//それ以外の通常テスト
TEST_CASE(test_normal) {
    ASSERT(true, "normal test");
}

// 4) スキップテスト
DISABLED_TEST_CASE(test_skip) {
    ASSERT(false, "won't run");
}

int main() {
    // 全カテゴリ実行
    // return RUN_ALL_TESTS();

    // === あるいは ===
    // Priorityのみ
    // return RUN_PRIORITY_TESTS();

    // Priority+Order
    // return RUN_ORDER_TESTS();
}
*/
// </summary>

namespace ECS::test{

    static std::mutex         g_runCountMutex;
    static std::unordered_map<std::string, int> g_runCounts;
    static constexpr auto LOG_FILE = "logs/runTest.log";
    static std::vector<std::string> g_logBuffer;
    static std::mutex  g_logMutex;

    inline std::string currentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    inline void saveLog(const char* fmt, ...) {
        std::lock_guard<std::mutex> lk(g_logMutex);
        char tmp[256];  // 必要に応じて拡張
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);

        // 可変長の文字列を std::string にコピー
        if (n > 0) {
            g_logBuffer.emplace_back(tmp, (size_t)n);
        }
    }

    inline void printLogBufferOnLogFile() {

        // ファイルに追記
        std::ofstream ofs(LOG_FILE, std::ios::app);

        if (!ofs.is_open()) {
            // open に失敗したら標準エラーへ出力
            std::fprintf(stderr, "Failed to open log file: %s\n", LOG_FILE);
            return;
        }

        for (auto& line : g_logBuffer) {
            ofs << currentTimestamp() << " " << line << "\n";
        }
    }

    inline void clearLogBuffer() {
        g_logBuffer.clear();
    }

enum class Category : int {
    Priority = 0,  //最優先
    OrderPriority = 1,  //実行順序保証
    Standard = 2,  //通常
};

//テスト情報構造体&レジストリ
struct TestInfo {
    std::string        name;
    std::function<void()> fn;
    bool               enabled;
    Category           cat;
};

inline auto& get_registry() {
    static std::vector<TestInfo> r;
    return r;
}

inline auto& get_category_counts() {
    static std::unordered_map<Category, int> counts = {
        { Category::Priority,      0 },
        { Category::OrderPriority, 0 },
        { Category::Standard,      0 }
    };
    return counts;
}
//登録ヘルパー
struct TestRegistrar {
    TestRegistrar(const char* name,
        std::function<void()> fn,
        bool enabled,
        Category cat)
    {
        get_registry().push_back({ name, fn, enabled, cat });
        ++get_category_counts()[cat];
        std::lock_guard lk(g_runCountMutex);
        g_runCounts[name] = 0;
    }
};

//TEST_CASEマクロ群

//通常
#define TEST_CASE(name)                                           \
    static void name();                                           \
    static ECS::test::TestRegistrar _reg_##name{                 \
        #name, &name, true, ECS::test::Category::Standard        \
    };                                                            \
    static void name()

//優先度高(Priorityのみ)
#define TEST_CASE_PRIORITY(name)                                  \
    static void name();                                           \
    static ECS::test::TestRegistrar _reg_##name{                 \
        #name, &name, true, ECS::test::Category::Priority        \
    };                                                            \
    static void name()

//実行順保証(Priority+OrderPriority)
#define TEST_CASE_ORDER(name)                                     \
    static void name();                                           \
    static ECS::test::TestRegistrar _reg_##name{                 \
        #name, &name, true, ECS::test::Category::OrderPriority   \
    };                                                            \
    static void name()

//スキップ
#define TEST_CASE_DISABLED(name)                                  \
    static void name();                                           \
    static ECS::test::TestRegistrar _reg_##name{                 \
        #name, &name, false, ECS::test::Category::Standard       \
    };                                                            \
    static void name()

inline int& testPassCount() {
    static int pass = 0;
    return pass;
}

//失敗カウンタ＆アサーション
inline int& testFailures() {
    static int fails = 0;
    return fails;
}

inline bool& testFailFlag() {
    static bool fail = false;
    return fail;
}

inline void assertTrue(bool cond, const char* msg) {
    if (cond) {
        saveLog("[  PASSED ] %s", msg);
        std::printf("[  PASSED ] %s\n", msg);
        ++testPassCount();
    }
    else {
        saveLog("[  FAILED ] %s", msg);
        std::printf("[FAILED] %s\n", msg);
        ++testFailures();
        testFailFlag() = true;
    }
}

inline int run_test(const char* testName,int maxRuns){
    {
        std::lock_guard lk(g_runCountMutex);
        int& cnt = g_runCounts[testName];
        ++cnt;
        if (cnt > maxRuns) {
            return 1;
        }
    }

    for (auto& t : get_registry()) {
        if (t.name != testName) {
            continue;
        }
        
        std::string log = "[ RUN     ] " + t.name;
        saveLog("%s", log.c_str());
        std::printf("%s\n",log.c_str());
        t.fn();
    }

    int pass = testPassCount();
    int fails = testFailures();

    std::string finishLog =  
        "[ SUMMARY ] " 
        + std::to_string(pass) 
        + " passed, " 
        + std::to_string(fails) 
        + " faild in "
        + std::to_string(pass + fails)
        + " total test";

    std::printf("%s\n", finishLog.c_str());
    saveLog("%s\n", finishLog.c_str());

    if(testFailFlag()){
        printLogBufferOnLogFile();
        testFailFlag() = false;
    }
    
    clearLogBuffer();

    return fails == 0 ? 0 : 1;
}

///CategoryFilterまでを実行してまとめて返す
inline int run_tests(Category filter = Category::Standard,bool isLoop = false) {
    
    static bool ran = false;

    if (ran && !isLoop) {
        return 1; 
    }

    ran = true;

    // 定義した順序通りに回す
    constexpr Category order[] = {
        Category::Priority,
        Category::OrderPriority,
        Category::Standard
    };

    int total = 0;
    for (auto cat : order) {
        if (static_cast<int>(cat) > static_cast<int>(filter)) {
            break;
        }

        for (auto& t : get_registry()) {
            if (!t.enabled || t.cat != cat) {
                continue;
            }
            std::cout << "[ RUN     ] " << t.name << "\n";
            t.fn();
            total++;
        }
    }

    int fails = testFailures();

    std::cout << "\n" <<"[ SUMMARY ] "
        << (total - fails) << " passed, "
        << fails <<" faild " << " in "
        << total << " total\n";

    return fails == 0 ? 0 : 1;
}

//マクロ
#define RUN_ALL_TESTS(isLoop)           return ECS::test::run_tests(ECS::test::Category::Standard,isLoop)
#define RUN_PRIORITY_TESTS(isLoop)      return ECS::test::run_tests(ECS::test::Category::Priority,isLoop)
#define RUN_ORDER_TESTS(isLoop)         return ECS::test::run_tests(ECS::test::Category::OrderPriority,isLoop)
#define RUN_TEST(name,maxCount) ECS::test::run_test(name,maxCount)
}// namespace ECS::test

#ifdef NDEBUG
    #define ASSERT(cond, ...)   ((void)0)
#else
    #define ASSERT(cond, ...)                                     \
        do {                                                       \
            if(!(cond)) {                                          \
                std::cerr << "[ASSERT_ERROR]: ";                  \
                std::cerr << __VA_ARGS__;                         \
                std::cerr << std::endl;                           \
                std::abort();                                      \
            }                                                      \
        } while(0)
#endif

#if !defined(NDEBUG) && defined(CUSTOM_INFO_ENABLED)
#define CUSTOM_INFO(msg)    std::cout << "[ INFO    ] " << msg << "\n"
#else
#define CUSTOM_INFO(msg)    ((void)0)
#endif

#ifdef NDEBUG
#undef MESSAGE
#define MESSAGE(msg)    ((void)0)
#else
#define MESSAGE(msg)    std::cout << "[ MESSAGE ] " << msg << "\n"
#endif



