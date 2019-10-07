#include "testing/testing.hpp"

#include "base/logging.hpp"

#include <vector>


namespace
{
  void TestLogMessage(base::LogLevel, base::SrcPoint const &, std::string const &)
  {
  }

  bool g_SomeFunctionCalled;
  int SomeFunction()
  {
    g_SomeFunctionCalled = true;
    return 3;
  }

  bool BoolFunction(bool result, bool & called)
  {
     called = true;
     return result;
  }
}

UNIT_TEST(Logging_Level)
{
  base::LogLevel const logLevelSaved = base::g_LogLevel;
  base::g_LogLevel = LWARNING;

  g_SomeFunctionCalled = false;
  base::LogMessageFn logMessageSaved = base::SetLogMessageFn(&TestLogMessage);

  LOG(LINFO, ("This should not pass", SomeFunction()));
  TEST(!g_SomeFunctionCalled, ());

  LOG(LWARNING, ("This should pass", SomeFunction()));
  TEST(g_SomeFunctionCalled, ());

  base::SetLogMessageFn(logMessageSaved);
  base::g_LogLevel = logLevelSaved;
}

UNIT_TEST(NullMessage)
{
  char const * ptr = 0;
  LOG(LINFO, ("Null message test", ptr));
}

UNIT_TEST(Logging_ConditionalLog)
{
  bool isCalled = false;
  CLOG(LINFO, BoolFunction(true, isCalled), ("This should not be displayed"));
  TEST(isCalled, ());

  isCalled = false;
  CLOG(LWARNING, BoolFunction(false, isCalled), ("This should be displayed"));
  TEST(isCalled, ());
}

UNIT_TEST(Logging_Levels)
{
  base::LogLevel level = NUM_LOG_LEVELS;
  bool good = base::FromString("DEBUG", level);
  EXPECT_TRUE(good);
  EXPECT_EQ(level, base::LDEBUG);

  good = base::FromString("YAVASYA", level);
  EXPECT_FALSE(good);

  EXPECT_EQ(base::ToString(base::LDEBUG), "DEBUG");
}
