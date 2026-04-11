#include "button_profile.h"

#include <assert.h>

static void test_record_button_is_momentary() {
    assert(!button_is_latching(BUTTON_RECORD));
}

static void test_sector_button_is_momentary() {
    assert(!button_is_latching(BUTTON_SECTOR));
}

static void test_only_record_button_is_populated() {
    assert(button_is_connected(BUTTON_RECORD));
    assert(!button_is_connected(BUTTON_SECTOR));
}

int main() {
    test_record_button_is_momentary();
    test_sector_button_is_momentary();
    test_only_record_button_is_populated();
    return 0;
}
