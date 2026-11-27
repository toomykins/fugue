#include <QtCore/QByteArray>
#include <QtCore/QMetaMethod>
#include <QtCore/QObject>
#include <QtCore/qtenvironmentvariables.h>
#include <QtTest/qtestcase.h>
#include "shell/gpubackend.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define FUGUE_TEST_SUITE(name, entry) int entry(int argc, char** argv);
#include "fugue_test_suites.inc"
#undef FUGUE_TEST_SUITE

namespace {
struct Suite {
    const char* name;
    int (*run)(int, char**);
};
constexpr Suite suites[] = {
#define FUGUE_TEST_SUITE(name, entry) {name, entry},
#include "fugue_test_suites.inc"
#undef FUGUE_TEST_SUITE
};

void listSuites() {
    std::cout << "Usage: fugue_tests <suite> [--shard=K/N] [QtTest arguments]\n\nSuites:\n";
    for (const auto& suite : suites) std::cout << "  " << suite.name << '\n';
}

// QtTest's own rule for which slots are test functions.
bool isTestFunction(const QMetaMethod& method) {
    if (method.access() != QMetaMethod::Private || method.methodType() != QMetaMethod::Slot ||
        method.parameterCount() != 0 || method.returnType() != QMetaType::Void) return false;
    const QByteArray name = method.name();
    return !name.isEmpty() && !name.endsWith("_data") && name != "initTestCase" && name != "cleanupTestCase" &&
           name != "init" && name != "cleanup";
}
} // namespace

// --shard=K/N runs the Kth of N parts of a suite: its test functions in declaration order, dealt out in turn.
// They are read from the test object, so each function is in exactly one part and a new one needs no
// registration. initTestCase and cleanupTestCase run in every part.
int fugue_test_exec(QObject* test, int argc, char** argv) {
    const char* shard = nullptr;
    std::vector<char*> options;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--shard=", 8) == 0) shard = argv[i] + 8;
        else options.push_back(argv[i]);
    }
    if (!shard) return QTest::qExec(test, argc, argv);
    char* end = nullptr;
    const long part = std::strtol(shard, &end, 10);
    const long parts = *end == '/' ? std::strtol(end + 1, &end, 10) : 0;
    if (*end != 0 || part < 1 || part > parts) {
        std::cerr << "Invalid --shard=" << shard << "; expected K/N with 1 <= K <= N\n";
        return 1;
    }
    const QMetaObject* meta = test->metaObject();
    std::vector<QByteArray> names;
    long index = 0;
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (isTestFunction(method) && index++ % parts == part - 1) names.push_back(method.name());
    }
    if (names.empty()) {
        // Without a function argument QtTest would run the whole suite.
        std::cout << meta->className() << ": no test function in shard " << shard << '\n';
        return 0;
    }
    std::vector<char*> arguments{argv[0]};
    for (auto& name : names) arguments.push_back(name.data());
    arguments.insert(arguments.end(), options.begin(), options.end());
    arguments.push_back(nullptr);
    return QTest::qExec(test, int(arguments.size()) - 1, arguments.data());
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Without a console window, Qt otherwise sends plain test output to the
    // debugger instead of CTest's capture pipe (including failures and skips).
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
    fugue::shell::settleGpuBackend(); // an entry names its backend; a run by hand gets the platform's
    // FUGUE_GPU_SOFTWARE=1 asks for a software adapter in every suite (the off-screen GPU comparisons read it too). The
    // application names its software device in FUGUE_GPU_BACKEND instead (d3d11-warp, shell/gpubackend.h).
    if (qEnvironmentVariableIntValue("FUGUE_GPU_SOFTWARE") == 1) qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    if (argc < 2 || std::strcmp(argv[1], "--list") == 0 || std::strcmp(argv[1], "--help") == 0) {
        listSuites();
        return 0;
    }
    for (const auto& suite : suites) {
        if (std::strcmp(argv[1], suite.name) != 0) continue;
        // Keep argv[0] as the executable path and remove only our selector.
        // The suite creates its own Qt application, if it needs one.
        for (int i = 1; i < argc; ++i) argv[i] = argv[i + 1];
        return suite.run(argc - 1, argv);
    }
    std::cerr << "Unknown test suite: " << argv[1] << '\n';
    listSuites();
    return 1;
}
