#pragma once

#if defined(__APPLE__)
  #include <TargetConditionals.h>
  #define GEOCORE_OS_MAC
  #define GEOCORE_OS_NAME "mac"
#else
  #define GEOCORE_OS_LINUX
  #define GEOCORE_OS_NAME "linux"
#endif
