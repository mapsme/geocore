#include "platform/target_os.hpp"

#include "testing/testing.hpp"

#ifndef GEOCORE_UNIT_TEST_DISABLE_PLATFORM_INIT
#include "testing/path.hpp"
#include "platform/platform.hpp"
#endif

int main(int argc, char * argv[])
{
#ifndef GEOCORE_UNIT_TEST_DISABLE_PLATFORM_INIT
  // Setting stored paths from testingmain.cpp
  Platform & pl = GetPlatform();
  pl.SetWritableDirForTests(TestindDataPath::kDataPath);
  pl.SetResourceDir(TestindDataPath::kDataPath);
#endif

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
