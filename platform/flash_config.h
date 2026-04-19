#ifndef FLASH_CONFIG_H
#define FLASH_CONFIG_H

#include <stdint.h>
#include "hardware/flash.h"
#include "pico/platform.h"

#define CONFIG_MAGIC         0xCAFEF00DUL
#define CONFIG_DEFAULT_HOURS 14UL
#define CONFIG_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_FLASH_ADDR    (XIP_BASE + CONFIG_FLASH_OFFSET)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t hours;
} pico_config_t;

static inline uint32_t config_read_hours(void)
{
    const pico_config_t *cfg = (const pico_config_t *) CONFIG_FLASH_ADDR;
    if (cfg->magic == CONFIG_MAGIC && cfg->hours > 0 && cfg->hours <= 720)
        return cfg->hours;
    return CONFIG_DEFAULT_HOURS;
}

#endif
