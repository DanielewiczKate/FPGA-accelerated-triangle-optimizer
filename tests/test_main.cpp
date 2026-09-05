// The single translation unit that pulls in doctest's implementation and
// provides main(). Nothing else belongs here - put actual tests in test_*.cpp
// files, which only #include "doctest.h" (no macro) and register TEST_CASEs.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
