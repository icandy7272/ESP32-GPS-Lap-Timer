#pragma once

// Single canonical declaration of the global SdFat instance.
// Defined in storage.cpp. Include this header instead of
// writing `extern SdFat sd;` inline.

#include <SdFat.h>

extern SdFat sd;
