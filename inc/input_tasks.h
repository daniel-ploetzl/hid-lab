/*
 * Public interface for HID input behaviour engine.
 */

#ifndef INPUT_TASKS_H
#define INPUT_TASKS_H

#include <stdint.h>
#include <stdbool.h>

void input_tasks_init(void);
void input_tasks_run(bool active);
void input_tasks_stop(void);

#endif
