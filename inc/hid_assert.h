#ifndef HID_ASSERT_H
#define HID_ASSERT_H

#include "pico.h"

/* The default Release build defines NDEBUG, which removes the standard assert. */
#define HID_ASSERT(cond)                                  \
    do {                                                  \
        if (!(cond))                                      \
            panic("assert %s:%d", __FILE__, __LINE__);    \
    } while (0)

#endif
