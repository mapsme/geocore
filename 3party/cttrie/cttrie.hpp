#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "3party/cttrie/cttrie.h"
#pragma clang diagnostic pop
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "3party/cttrie/cttrie.h"
#pragma GCC diagnostic pop
#endif



