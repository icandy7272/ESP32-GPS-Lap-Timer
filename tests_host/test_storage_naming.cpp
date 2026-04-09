#include "storage_naming.h"

#include <assert.h>
#include <string.h>

static void test_final_path_format_matches_expected_layout() {
    char path[128] = {0};

    storage_naming_format_final_path("20260409_123456", "MyTrack",
                                     7, path, sizeof(path));

    assert(strcmp(path, "sessions/20260409_MyTrack_123456_007.vbo") == 0);
}

int main() {
    test_final_path_format_matches_expected_layout();
    return 0;
}
