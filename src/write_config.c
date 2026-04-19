/*
 *  Host-side tool to write duration config as a UF2 file
 *  directly to the RP2350 BOOTSEL mount point.
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
#define MAX_HOURS         24

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

static void build_block(uf2_block_t *b, uint8_t cfg[8])
{
    memset(b, 0, sizeof(*b));
    b->magic0 = UF2_MAGIC_START0;
    b->magic1 = UF2_MAGIC_START1;
    b->flags  = UF2_FLAG_FAMILY_ID;
    b->target_addr = CONFIG_FLASH_ADDR;
    b->payload_size = UF2_PAYLOAD_SIZE;
    b->block_no = 0;
    b->total_blocks = 1;
    b->family_id = UF2_FAMILY_RP2350;
    b->magic_end = UF2_MAGIC_END;
    memcpy(b->data, cfg, 8);
}

int main(int ac, char **av)
{
    if (ac != 2)
	{
        fprintf(stderr, "Usage: %s <hours>\n", av[0]);
        return 1;
    }

	char *end = NULL;
	long hours = strtol(av[1], &end, 10);

    if ( *end != '\0' || hours < 1 || hours > MAX_HOURS)
	{
        fprintf(stderr, "Invalid hours (1-%d)\n", MAX_HOURS);
        return 1;
    }

	const char *user = getenv("USER");
    if (!user)
	{
        fprintf(stderr, "Error: USER environment variable not set\n");
        return 1;
    }

	uint8_t cfg[8];
	write_le32(cfg, CONFIG_MAGIC);
	write_le32(cfg + 4, (uint32_t)hours);

	uf2_block_t block;
	build_block(&block, cfg);

	char path[512];
	snprintf(path, sizeof(path), "/media/%s/RP2350/pico_hid_cfg.uf2", user);

	FILE *f = fopen(path, "wb");
	if (!f)
	{
		fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
		return 1;
	}

	if (fwrite(&block, 1, sizeof(block), f) != sizeof(block))
	{
		fprintf(stderr, "Write failed: %s\n", strerror(errno));
		fclose(f);
		return 1;
	}

	fclose(f);
	printf("UF2 written to %s\n", path);
    printf("Configured runtime: %ld hours\n", hours);
	return 0;
}
