#pragma once
// Minimális tesztkeret (külső függőség nélkül).
#include <cmath>
#include <cstdio>
static int g_fail = 0, g_run = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; std::printf("  HIBA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))
#define RUN(f) do { std::printf("%s\n", #f); f(); } while (0)
#define TEST_MAIN_END() do { std::printf("%d ellenőrzés, %d hiba\n", g_run, g_fail); return g_fail ? 1 : 0; } while (0)
