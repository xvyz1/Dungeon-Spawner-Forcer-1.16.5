#include "inputs.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *readLine(char *buf, int size) {
    if (!fgets(buf, size, stdin)) return NULL;
    char *s = buf;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int isEmpty(const char *s) { return *s == '\n' || *s == '\r' || *s == 0; }

static int parseI64(const char *s, int64_t *out) {
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end == s) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end != 0 || errno == ERANGE) return 0;
    *out = (int64_t)v;
    return 1;
}

static int readI64(const char *name, int64_t *out, int hasDefault, int64_t defaultValue, int64_t min, int64_t max) {
    char buf[128];
    for (;;) {
        char *s = readLine(buf, sizeof buf);
        if (!s || isEmpty(s)) {
            if (hasDefault) { *out = defaultValue; return 1; }
            if (!s) return 0;
            printf("A value is required, enter %s:\n", name);
            continue;
        }
        int64_t v;
        if (!parseI64(s, &v)) { printf("Entered value is not valid, enter %s:\n", name); continue; }
        if (v < min || v > max) {
            printf("Entered value must be between %" PRId64 " and %" PRId64 ", enter %s:\n", min, max, name);
            continue;
        }
        *out = v;
        return 1;
    }
}

int getI64Number(const char *name, int64_t *out, int hasDefault, int64_t defaultValue, int64_t min, int64_t max) {
    if (hasDefault) printf("Enter %s (leave empty for %" PRId64 "):\n", name, defaultValue);
    else printf("Enter %s:\n", name);
    if (!readI64(name, out, hasDefault, defaultValue, min, max)) return 0;
    int shown = (int)strlen(name);
    const char *hint = strrchr(name, '(');
    if (hint && hint > name && name[shown - 1] == ')' && hint[-1] == ' ') shown = (int)(hint - name) - 1;
    printf("Using %.*s %" PRId64 "\n", shown, name, *out);
    return 1;
}

int getIntEnum(const char *name, int *out, const char *const *elements, int elementCount) {
    printf("Enter %s:\n", name);
    for (int i = 0; i < elementCount; i++) printf("%d - %s\n", i + 1, elements[i]);
    int64_t v;
    if (!readI64(name, &v, 0, 0, 1, elementCount)) return 0;
    *out = (int)v - 1;
    printf("Using %s %s\n", name, elements[v - 1]);
    return 1;
}

int getIntEnumDefault(const char *name, int *out, const char *const *elements, int elementCount, int defaultIndex) {
    printf("Enter %s (leave empty for %d - %s):\n", name, defaultIndex + 1, elements[defaultIndex]);
    for (int i = 0; i < elementCount; i++) printf("%d - %s\n", i + 1, elements[i]);
    int64_t v;
    if (!readI64(name, &v, 1, defaultIndex + 1, 1, elementCount)) return 0;
    *out = (int)v - 1;
    printf("Using %s %s\n", name, elements[v - 1]);
    return 1;
}

int getString(const char *name, char *out, int cap, int allowEmpty) {
    char buf[1024];
    for (;;) {
        if (allowEmpty) printf("Enter %s (leave empty for none):\n", name);
        else printf("Enter %s:\n", name);
        char *s = readLine(buf, sizeof buf);
        if (!s) return 0;
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
        if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { s[n - 1] = 0; s++; n -= 2; }
        if (n == 0 && !allowEmpty) { printf("A value is required.\n"); continue; }
        snprintf(out, cap, "%s", s);
        if (n) printf("Using %s %s\n", name, out);
        else printf("Using no %s\n", name);
        return 1;
    }
}
