#ifndef FLASH_CONFIG_H
#define FLASH_CONFIG_H

#include <stdint.h>

#define CONFIG_FLASH_XIP_BASE    0x10000000UL
#define CONFIG_FLASH_TOTAL_BYTES (4UL * 1024UL * 1024UL)
#define CONFIG_SECTOR_SIZE_BYTES 4096UL
#define CONFIG_FLASH_ADDR        (CONFIG_FLASH_XIP_BASE + CONFIG_FLASH_TOTAL_BYTES - CONFIG_SECTOR_SIZE_BYTES)
#define CONFIG_MAGIC             0xCAFEF00DUL
#define CONFIG_HOURS_MIN         1UL
#define CONFIG_HOURS_MAX         24UL
#define CONFIG_DEFAULT_HOURS     24UL

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t hours;
} pico_config_t;

static inline uint32_t config_read_hours(void)
{
    const pico_config_t *cfg = (const pico_config_t *) CONFIG_FLASH_ADDR;
    if (cfg->magic == CONFIG_MAGIC &&
        cfg->hours >= CONFIG_HOURS_MIN &&
        cfg->hours <= CONFIG_HOURS_MAX)
        return cfg->hours;
    return CONFIG_DEFAULT_HOURS;
}

#endif
