/* Owner-thread HID input for the controllable bitmap display. */
#ifndef CONTROLLER_FIXTURE_H_
#define CONTROLLER_FIXTURE_H_
#include "st_controller.h"
struct st_controllers *st_controller_fixture_create(const char *settings);
int                    st_controller_fixture_request(void *context, const uint8_t *request, size_t size);
#endif
