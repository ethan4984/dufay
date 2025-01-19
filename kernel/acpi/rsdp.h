#pragma once

#include <stdint.h>
#include <stddef.h>

struct acpi_hdr {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char OEMID[6];
	char OEM_table_id[8];
	uint32_t OEM_revision;
	uint32_t creator_ID;
	uint32_t creator_revision;
} __attribute__((packed));

struct rsdp {
	char signature[8];
	uint8_t checksum;
	char OEMID[6];
	uint8_t revision; 
	uint32_t rsdt_addr;
	uint32_t length;
	uint64_t xsdt_addr;
	uint8_t ext_checksum;
	uint8_t reserved[3];
} __attribute__((packed));

struct rsdt {
	struct acpi_hdr acpi_hdr;
	uint32_t acpi_ptr[];
} __attribute__((packed));

struct xsdt {
	struct acpi_hdr acpi_hdr;
	uint64_t acpi_ptr[];
} __attribute__((packed));

struct fadt {
	struct acpi_hdr acpi_hdr;
	uint32_t firmware_ctrl;
	uint32_t dsdt;
	uint8_t reserved;
	uint8_t preferred_pm_profile;
	uint16_t sci_int;
	uint32_t sci_cmd;
	uint8_t acpi_enable;
	uint8_t acpi_disable;
	uint8_t s4bios_req;
	uint8_t pstate_cnt;
	uint32_t pm1a_evt_blk;
	uint32_t pm1b_evt_blk;
	uint32_t pm1a_cnt_blk;
	uint32_t pm1b_cnt_blk;
	uint32_t pm2_cnt_blk;
	uint32_t pm_tmr_blk;
	uint32_t gpe0_blk;
	uint32_t gpe1_blk;
	uint8_t pm1_evt_len;
	uint8_t pm1_cnt_len;
	uint8_t pm2_cnt_len;
	uint8_t pm_tmr_len;
	uint8_t gpe0_blk_len;
	uint8_t gpe1_blk_len;
	uint8_t gpe1_base;
	uint8_t cst_cnt;
	uint16_t p_lvl2_lat;
	uint16_t p_lvl3_lat;
	uint16_t flush_size;
	uint16_t flush_stride;
	uint8_t duty_offset;
	uint8_t duty_width;
	uint8_t day_alarm;
	uint8_t month_alarm;
	uint8_t century;
	uint16_t iapc_boot_arch;
	uint8_t reserved1;
	uint32_t flags;
	uint8_t reset_reg[12];
	uint8_t reset_value;
	uint16_t arm_boot_arch;
	uint8_t fadt_minor_version;
	uint64_t x_firmware_ctrl;
	uint64_t x_dsdt;
	uint8_t x_pm1a_evt_blk[12];
	uint8_t x_pm1b_evt_blk[12];
	uint8_t x_pm1a_cnt_blk[12];
	uint8_t x_pm1b_cnt_blk[12];
	uint8_t x_pm2_cnt_blk[12];
	uint8_t x_pm_tmr_blk[12];
	uint8_t x_gpe0_blk[12];
	uint8_t x_gpe1_blk[12];
	uint8_t sleep_control_reg[12];
	uint8_t sleep_status_reg[12];
	uint64_t hypervisor_vendor_id;
} __attribute__((packed));

extern struct rsdp *rsdp;
extern struct rsdt *rsdt;
extern struct xsdt *xsdt;

extern struct fadt *fadt;

void *acpi_find_sdt(const char *signature);
