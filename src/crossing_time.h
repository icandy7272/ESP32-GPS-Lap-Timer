#pragma once

#include "types.h"

#include <stdint.h>

int64_t crossing_time_linear_us(const GpsPoint* prev,
                                const GpsPoint* curr,
                                const DetectionLine* line);

int64_t crossing_time_centered_us(const GpsPoint* p0,
                                  const GpsPoint* p1,
                                  const GpsPoint* p2,
                                  const GpsPoint* p3,
                                  const DetectionLine* line);
