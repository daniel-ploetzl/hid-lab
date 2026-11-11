#ifndef FLASH_CONFIG_H
#define FLASH_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define CONFIG_FLASH_XIP_BASE    0x10000000UL
#define CONFIG_FLASH_TOTAL_BYTES (4UL * 1024UL * 1024UL)
#define CONFIG_SECTOR_SIZE_BYTES 4096UL
#define CONFIG_FLASH_ADDR        (CONFIG_FLASH_XIP_BASE + CONFIG_FLASH_TOTAL_BYTES - CONFIG_SECTOR_SIZE_BYTES)
/* Bumped when the record layout changed to add period_s. A board still holding
 * the old two-field record now fails this check and falls back to defaults. */
#define CONFIG_MAGIC             0xCAFEF00EUL
#define CONFIG_HOURS_MIN         1UL
#define CONFIG_HOURS_MAX         24UL
#define CONFIG_DEFAULT_HOURS     24UL
/* Re-activation idle period, in seconds. 1 week is both the ceiling and the
 * default, so a board with no valid config restarts weekly. */
#define CONFIG_PERIOD_MIN_S      1UL
#define CONFIG_PERIOD_MAX_S      604800UL
#define CONFIG_DEFAULT_PERIOD_S  604800UL

/* A sector of its own, so changing the run time cannot erase the credentials. */
#define CREDS_FLASH_ADDR         (CONFIG_FLASH_ADDR - CONFIG_SECTOR_SIZE_BYTES)
#define CREDS_MAGIC              0x57494649UL
#define CREDS_VERSION            1UL
#define CREDS_AUTH_WPA2          2UL
#define CREDS_AUTH_WPA3          3UL
#define CREDS_SSID_MAX           32
#define CREDS_KEY_MIN            8
#define CREDS_KEY_MAX            64

typedef struct {
    uint32_t magic;
    uint32_t hours;
    uint32_t period_s;
} pico_config_t;

/* The arrays hold one extra byte for the terminating NUL. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t auth;
    char     ssid[CREDS_SSID_MAX + 1];
    char     key[CREDS_KEY_MAX + 1];
    uint8_t  reserved[2];
    uint32_t crc;
} wifi_creds_t;

_Static_assert(sizeof(pico_config_t) == 12, "config layout changed, update write_config.c");
_Static_assert(sizeof(wifi_creds_t) == 116, "credentials layout changed, raise CREDS_VERSION");

static inline uint32_t config_read_hours(void)
{
    const pico_config_t *cfg = (const pico_config_t *) CONFIG_FLASH_ADDR;
    if (cfg->magic == CONFIG_MAGIC &&
        cfg->hours >= CONFIG_HOURS_MIN &&
        cfg->hours <= CONFIG_HOURS_MAX)
        return cfg->hours;
    return CONFIG_DEFAULT_HOURS;
}

/* Split from the read so the validation can be exercised on the host against a
 * crafted record, without a fixed flash address. */
static inline uint32_t config_period_s_at(const pico_config_t *cfg)
{
    if (cfg->magic == CONFIG_MAGIC &&
        cfg->period_s >= CONFIG_PERIOD_MIN_S &&
        cfg->period_s <= CONFIG_PERIOD_MAX_S)
        return cfg->period_s;
    return CONFIG_DEFAULT_PERIOD_S;
}

static inline uint32_t config_read_period_s(void)
{
    return config_period_s_at((const pico_config_t *) CONFIG_FLASH_ADDR);
}

/* Bitwise CRC-32. Shared with write_config so both sides compute the same value. */
static inline uint32_t creds_crc32(const void *data, size_t len)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFUL;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Returns NULL unless the whole record is valid, so bad data can never start a join. */
static inline const wifi_creds_t *creds_read(void)
{
    const wifi_creds_t *creds = (const wifi_creds_t *) CREDS_FLASH_ADDR;

    if (creds->magic != CREDS_MAGIC || creds->version != CREDS_VERSION)
        return NULL;
    if (creds->crc != creds_crc32(creds, offsetof(wifi_creds_t, crc)))
        return NULL;
    if (creds->auth != CREDS_AUTH_WPA2 && creds->auth != CREDS_AUTH_WPA3)
        return NULL;
    if (creds->ssid[0] == '\0' || creds->ssid[CREDS_SSID_MAX] != '\0' ||
        creds->key[CREDS_KEY_MAX] != '\0')
        return NULL;
    return creds;
}

#endif
