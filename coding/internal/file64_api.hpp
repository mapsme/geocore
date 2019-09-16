#pragma once

#include "base/base.hpp"

// POSIX standart.
#include <sys/types.h>
static_assert(sizeof(off_t) == 8, "");
#define fseek64 fseeko
#define ftell64 ftello

#include <cstdio>
