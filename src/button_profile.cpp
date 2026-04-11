#include "button_profile.h"

bool button_is_latching(uint8_t button_id)
{
    switch (button_id) {
        case BUTTON_RECORD:
        case BUTTON_SECTOR:
            return false;
        default:
            return false;
    }
}

bool button_is_connected(uint8_t button_id)
{
    switch (button_id) {
        case BUTTON_RECORD:
            return true;
        case BUTTON_SECTOR:
            return false;
        default:
            return false;
    }
}
