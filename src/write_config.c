/*
 *  Host-side tool to write the run time or Wi-Fi credentials as a UF2 file
 *  directly to the RP2350 BOOTSEL mount point.
 *
 * CONFIG LAYOUT IN FLASH (last 4KB sector, offset 0x3FF000):
 *   [0..3]   magic    = 0xCAFEF00E  (little-endian u32)
 *   [4..7]   hours    = N           (little-endian u32)
 *   [8..11]  period_s = N           (little-endian u32, re-activation gap)
 *
 * CREDENTIALS LAYOUT IN FLASH (sector before it, offset 0x3FE000):
 *   wifi_creds_t in flash_config.h, sealed with a CRC-32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include "flash_config.h"

/* ── UF2 constants ───────────────────────── */
#define UF2_MAGIC_START0   0x0A324655UL
#define UF2_MAGIC_START1   0x9E5D5157UL
#define UF2_MAGIC_END      0x0AB16F30UL
#define UF2_FLAG_FAMILY_ID 0x00002000UL
#define UF2_FAMILY_RP2350  0xE48BFF59UL
#define UF2_PAYLOAD_SIZE   256
#define UF2_DATA_SIZE      476

typedef struct __attribute__((packed)) {
    uint32_t magic0;
    uint32_t magic1;
    uint32_t flags;
    uint32_t target_addr;
    uint32_t payload_size;
    uint32_t block_no;
    uint32_t total_blocks;
    uint32_t family_id;
    uint8_t  data[UF2_DATA_SIZE];
    uint32_t magic_end;
} uf2_block_t;

_Static_assert(sizeof(uf2_block_t) == 512, "UF2 blocks must be exactly 512 bytes");
_Static_assert(sizeof(wifi_creds_t) <= UF2_PAYLOAD_SIZE, "credentials must fit one UF2 block");

static void write_le32(void *dest, uint32_t v)
{
    uint8_t *buf = dest;

    buf[0] = v;
    buf[1] = v >> 8;
    buf[2] = v >> 16;
    buf[3] = v >> 24;
}

static void build_block(uf2_block_t *b, uint32_t addr, const void *payload, size_t len)
{
    memset(b, 0, sizeof(*b));
    b->magic0 = UF2_MAGIC_START0;
    b->magic1 = UF2_MAGIC_START1;
    b->flags  = UF2_FLAG_FAMILY_ID;
    b->target_addr = addr;
    b->payload_size = UF2_PAYLOAD_SIZE;
    b->block_no = 0;
    b->total_blocks = 1;
    b->family_id = UF2_FAMILY_RP2350;
    b->magic_end = UF2_MAGIC_END;
    memcpy(b->data, payload, len);
}

static int write_uf2(uint32_t addr, const void *payload, size_t len)
{
    const char *user = getenv("USER");
    if (!user)
        return (fprintf(stderr, "Error: USER environment variable not set\n"), 1);

    uf2_block_t block;
    build_block(&block, addr, payload, len);

    char path[512];
    snprintf(path, sizeof(path), "/media/%s/RP2350/pico_hid_cfg.uf2", user);

    FILE *f = fopen(path, "wb");
    if (!f)
        return (fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno)), 1);

    if (fwrite(&block, 1, sizeof(block), f) != sizeof(block))
    {
        fprintf(stderr, "Write failed: %s\n", strerror(errno));
        fclose(f);
        return 1;
    }

    fclose(f);
    printf("UF2 written to %s\n", path);
    return 0;
}

/* Standard input keeps the password out of the shell history and process list. */
static int read_password(char *buf, size_t size)
{
    struct termios normal;
    struct termios hidden;
    int is_terminal = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &normal) == 0;

    if (is_terminal)
    {
        fputs("Wi-Fi password: ", stderr);
        hidden = normal;
        hidden.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
    }

    char *line = fgets(buf, (int)size, stdin);

    if (is_terminal)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &normal);
        fputc('\n', stderr);
    }
    if (!line)
        return -1;

    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

/* Accepts seconds, or an integer with a single unit suffix s m h d w. */
static int parse_period_s(const char *arg, uint32_t *out)
{
    char *end = NULL;
    long long value = strtoll(arg, &end, 10);

    /* Reject out-of-range before the multiply, so it cannot overflow. */
    if (end == arg || value <= 0 || value > (long long)CONFIG_PERIOD_MAX_S)
        return -1;

    unsigned long long mult = 1;
    if (*end != '\0')
    {
        if (end[1] != '\0')
            return -1;
        switch (*end)
        {
            case 's': mult = 1;      break;
            case 'm': mult = 60;     break;
            case 'h': mult = 3600;   break;
            case 'd': mult = 86400;  break;
            case 'w': mult = 604800; break;
            default:  return -1;
        }
    }

    unsigned long long secs = (unsigned long long)value * mult;
    if (secs < CONFIG_PERIOD_MIN_S || secs > CONFIG_PERIOD_MAX_S)
        return -1;

    *out = (uint32_t)secs;
    return 0;
}

/* The config sector holds both fields, so every write sets the run window and
 * the re-activation period together. period_arg may be NULL, which keeps the
 * one-week default. */
static int write_run_config(const char *hours_arg, const char *period_arg)
{
    char *end = NULL;
    long hours = strtol(hours_arg, &end, 10);

    if (*end != '\0' || hours < (long)CONFIG_HOURS_MIN || hours > (long)CONFIG_HOURS_MAX)
        return (fprintf(stderr, "Error: hours must be %ld-%ld (got %ld)\n",
                        (long)CONFIG_HOURS_MIN, (long)CONFIG_HOURS_MAX, hours), 1);

    uint32_t period_s = CONFIG_DEFAULT_PERIOD_S;
    if (period_arg != NULL && parse_period_s(period_arg, &period_s) != 0)
        return (fprintf(stderr, "Error: period must be 1-%lu s, optional suffix s/m/h/d/w (got %s)\n",
                        (unsigned long)CONFIG_PERIOD_MAX_S, period_arg), 1);

    uint8_t cfg[12];
    write_le32(cfg,     CONFIG_MAGIC);
    write_le32(cfg + 4, (uint32_t)hours);
    write_le32(cfg + 8, period_s);

    if (write_uf2(CONFIG_FLASH_ADDR, cfg, sizeof(cfg)) != 0)
        return 1;
    printf("Configured run window %ld h, re-activation period %lu s\n",
           hours, (unsigned long)period_s);
    return 0;
}

static int write_credentials(const char *auth_name, const char *ssid)
{
    uint32_t auth;

    if (strcmp(auth_name, "wpa2") == 0)
        auth = CREDS_AUTH_WPA2;
    else if (strcmp(auth_name, "wpa3") == 0)
        auth = CREDS_AUTH_WPA3;
    else
        return (fprintf(stderr, "Error: security mode must be wpa2 or wpa3\n"), 1);

    size_t ssid_len = strlen(ssid);
    if (ssid_len == 0 || ssid_len > CREDS_SSID_MAX)
        return (fprintf(stderr, "Error: SSID must be 1-%d bytes\n", CREDS_SSID_MAX), 1);

    /* Two spare bytes: a longer line then reads as too long, not as a cut-off password. */
    char key[CREDS_KEY_MAX + 3];
    if (read_password(key, sizeof(key)) != 0)
        return (fprintf(stderr, "Error: no password on standard input\n"), 1);

    size_t key_len = strlen(key);
    if (key_len < CREDS_KEY_MIN || key_len > CREDS_KEY_MAX)
        return (fprintf(stderr, "Error: password must be %d-%d bytes\n",
                        CREDS_KEY_MIN, CREDS_KEY_MAX), 1);

    /* Zeroed first, so unused bytes are the same in the CRC on both sides. */
    wifi_creds_t creds;
    memset(&creds, 0, sizeof(creds));
    write_le32(&creds.magic, CREDS_MAGIC);
    write_le32(&creds.version, CREDS_VERSION);
    write_le32(&creds.auth, auth);
    memcpy(creds.ssid, ssid, ssid_len);
    memcpy(creds.key, key, key_len);
    write_le32(&creds.crc, creds_crc32(&creds, offsetof(wifi_creds_t, crc)));

    if (write_uf2(CREDS_FLASH_ADDR, &creds, sizeof(creds)) != 0)
        return 1;
    printf("Stored credentials for %s\n", ssid);
    return 0;
}

/* The bootrom erases a sector before its first write, so this removes the old record. */
static int clear_credentials(void)
{
    const uint8_t blank[sizeof(wifi_creds_t)] = {0};

    if (write_uf2(CREDS_FLASH_ADDR, blank, sizeof(blank)) != 0)
        return 1;
    printf("Credentials cleared\n");
    return 0;
}

int main(int ac, char **av)
{
    if (ac == 2 && strcmp(av[1], "--wifi-clear") == 0)
        return clear_credentials();
    if (ac == 4 && strcmp(av[1], "--wifi") == 0)
        return write_credentials(av[2], av[3]);
    if ((ac == 2 || ac == 3) && av[1][0] != '-')
        return write_run_config(av[1], ac == 3 ? av[2] : NULL);

    fprintf(stderr, "Usage: %s <hours> [period]    period e.g. 3600, 30m, 1h, 1d, 7d; default 7d\n", av[0]);
    fprintf(stderr, "       %s --wifi <wpa2|wpa3> <ssid>   (password from standard input)\n", av[0]);
    fprintf(stderr, "       %s --wifi-clear\n", av[0]);
    return 1;
}
