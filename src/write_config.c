/*
 *  Host-side tool — writes duration config as a UF2 file
 *  directly to the RP2350 BOOTSEL mass storage mount.
 *
 *  UF2 is the native format the RP2350 bootloader accepts over
 *  mass storage. Each 512-byte UF2 block carries 256 bytes of
 *  payload to a target XIP flash address. Wrap the 8-byte
 *  config blob in one UF2 block targeting the last flash sector
 *  (0x103FF000) and copy it to the mounted drive — identical to
 *  how the firmware .uf2 is flashed.
 *
 * CONFIG LAYOUT IN FLASH (last 4KB sector, offset 0x3FF000):
 *   [0..3]  magic  = 0xCAFEF00D  (little-endian u32)
 *   [4..7]  hours  = N           (little-endian u32)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/* ── UF2 constants ───────────────────────── */
#define UF2_MAGIC_START0   0x0A324655UL
#define UF2_MAGIC_START1   0x9E5D5157UL
#define UF2_MAGIC_END      0x0AB16F30UL
#define UF2_FLAG_FAMILY_ID 0x00002000UL
#define UF2_FAMILY_RP2350  0xE48BFF59UL
#define UF2_PAYLOAD_SIZE   256

/* ── Config ──────────────────────────────── */
#define CONFIG_MAGIC      0xCAFEF00DUL
#define CONFIG_FLASH_ADDR 0x103FF000UL
#define MAX_HOURS         48

typedef struct __attribute__((packed)) {
    uint32_t magic0;
    uint32_t magic1;
    uint32_t flags;
    uint32_t target_addr;
    uint32_t payload_size;
    uint32_t block_no;
    uint32_t total_blocks;
    uint32_t family_id;
    uint8_t  data[UF2_PAYLOAD_SIZE];
    uint32_t magic_end;
} uf2_block_t;

static void write_le32(uint8_t *buf, uint32_t v)
{
    buf[0] = v;
    buf[1] = v >> 8;
    buf[2] = v >> 16;
    buf[3] = v >> 24;
}

int main(int ac, char **av)
{
    if (ac != 2)
	{
        fprintf(stderr, "Usage: %s <hours>\n", av[0]);
        return 1;
    }

    long hours = strtol(av[1], NULL, 10);
    if (hours < 1 || hours > MAX_HOURS)
	{
        fprintf(stderr, "Invalid hours (1-%d)\n", MAX_HOURS);
        return 1;
    }

    const char *mount = av[2];

    uint8_t config[8];
    write_le32(config, CONFIG_MAGIC);
    write_le32(config + 4, (uint32_t)hours);

    uf2_block_t block = {0};

    block.magic0 = UF2_MAGIC_START0;
    block.magic1 = UF2_MAGIC_START1;
    block.flags = UF2_FLAG_FAMILY_ID;
    block.target_addr = CONFIG_FLASH_ADDR;
    block.payload_size = UF2_PAYLOAD_SIZE;
    block.block_no = 0;
    block.total_blocks = 1;
    block.family_id = UF2_FAMILY_RP2350;
    block.magic_end = UF2_MAGIC_END;

    memcpy(block.data, config, sizeof(config));

    char path[512];
    snprintf(path, sizeof(path), "%s/pico_hid_cfg.uf2", mount);

    FILE *f = fopen(path, "wb");
    if (!f)
	{
        perror("fopen");
        return 1;
    }

    fwrite(&block, 1, sizeof(block), f);
    fclose(f);

    printf("Wrote config to %s\n", path);
    return 0;
}
