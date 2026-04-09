#pragma once

#include <stddef.h>

void storage_naming_format_final_path(const char* session_start_ts,
                                      const char* track_name,
                                      int seq,
                                      char* path,
                                      size_t path_len);
