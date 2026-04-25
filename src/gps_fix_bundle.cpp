#include "gps_fix_bundle.h"

GpsFixBundle gps_fix_bundle_from_filter_result(
    const GpsFilterProcessResult& result) {
    GpsFixBundle bundle = {};
    bundle.raw_fix = result.raw_fix;
    bundle.match_valid = result.match_valid;
    bundle.match_fix = result.match_valid ? result.match_fix : result.raw_fix;
    return bundle;
}
