#pragma once

#include "base/exception.hpp"
#include "base/logging.hpp"
#include "base/math.hpp"
#include "base/src_point.hpp"

#define GTEST_DONT_DEFINE_TEST 1
#include "gtest/gtest.h"

using namespace std;

#define UNIT_TEST(X) GTEST_TEST(geocore, X)
#define TEST(X, msg) EXPECT_TRUE(X)
#define TEST_EQUAL(X, Y, msg) EXPECT_EQ(X, Y)
#define TEST_NOT_EQUAL(X, Y, msg) EXPECT_NE(X, Y)
#define TEST_LESS(X, Y, msg) EXPECT_LT(X, Y)
#define TEST_LESS_OR_EQUAL(X, Y, msg) EXPECT_LE(X, Y)
#define TEST_GREATER(X, Y, msg) EXPECT_GT(X, Y)
#define TEST_GREATER_OR_EQUAL(X, Y, msg) EXPECT_GE(X, Y)
#define TEST_ALMOST_EQUAL_ULPS(X, Y, msg) EXPECT_DOUBLE_EQ(X, Y)
#define TEST_THROW(X, exception, msg) EXPECT_THROW(X, exception)
#define TEST_NO_THROW(X, msg) EXPECT_NO_THROW(X)
#define TEST_ANY_THROW(X, msg) EXPECT_ANY_THROW(X)
#define TEST_NEAR(X, Y, abs_err, msg) EXPECT_NEAR(X, Y, abs_err)
#define UNIT_CLASS_TEST(CLASS, NAME)               \
  struct UnitClass_##CLASS##_##NAME : public CLASS \
  {                                                \
  public:                                          \
    void NAME();                                   \
  };                                               \
  UNIT_TEST(CLASS##_##NAME)                        \
  {                                                \
    UnitClass_##CLASS##_##NAME instance;           \
    instance.NAME();                               \
  }                                                \
  void UnitClass_##CLASS##_##NAME::NAME()
