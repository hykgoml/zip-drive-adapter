#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

extern int test_failures; /* defined once in each test_*.c */

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
    test_failures++; } } while (0)

#define CHECK_CONTAINS(hay, needle) do { if (!strstr((hay), (needle))) { \
    fprintf(stderr, "%s:%d: output lacks \"%s\"\n--- output ---\n%s\n--------------\n", \
            __FILE__, __LINE__, (needle), (hay)); \
    test_failures++; } } while (0)

#define CHECK_LACKS(hay, needle) do { if (strstr((hay), (needle))) { \
    fprintf(stderr, "%s:%d: output has \"%s\"\n--- output ---\n%s\n--------------\n", \
            __FILE__, __LINE__, (needle), (hay)); \
    test_failures++; } } while (0)

#define RUN(t) do { fprintf(stderr, "  %s\n", #t); t(); } while (0)

static inline int count_of(const char *hay, const char *needle) {
    int n = 0;
    size_t len = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + len, needle)) n++;
    return n;
}

static inline int test_report(void) {
    if (test_failures) fprintf(stderr, "%d check(s) failed\n", test_failures);
    return test_failures != 0;
}

#endif
