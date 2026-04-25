#pragma once

#include "gps_filter.h"
#include "types.h"

GpsFixBundle gps_fix_bundle_from_filter_result(
    const GpsFilterProcessResult& result);
