#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <string.h>
#include "unity.h"
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct
{
    const char *name;
    UnityTestFunction function;
    int line;
} TestCase;

#define TEST_CASE(name) {#name, name, __LINE__}

/* CTest starts each case in a separate process, including crash regressions. */
static int run_test_cases(int argc, char **argv, const char *file,
                          const TestCase *cases, size_t count)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (strcmp(argv[1], cases[i].name) == 0)
            {
                UnityBegin(file);
                UnityDefaultTestRun(cases[i].function, cases[i].name, cases[i].line);
                return UnityEnd();
            }
        }
    }
    fprintf(stderr, "Usage: %s <test_name>\nUse CTest to run all cases safely.\n", argv[0]);
    for (size_t i = 0; i < count; ++i)
        fprintf(stderr, "  %s\n", cases[i].name);
    return 2;
}

#endif
