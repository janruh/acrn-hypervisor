/*
 * Copyright (c) 2020 TTTech Computertechnik AG.
 * All rights reserved.
 *
 * Jan Ruh, jan.ruh@tttech.com
 */

#ifdef CONFIG_SYNCHRONIZED_TIME_ENABLED
#include <vm.h>
#include <mmu.h>
#include <ept.h>
#include <logmsg.h>
#include <stshmem.h>
#include <stshmem_cfg.h>
#include "vpci_priv.h"

/* config space of stshmem device */
#define STSHMEM_VENDOR_ID   0x1c7eU
#define STSHMEM_DEVICE_ID   0xbeefU
#define STSHMEM_CLASS       0x05U
#define STSHMEM_REV         0x01U

/* stshmem device only supports bar2 */
#define STSHMEM_SHM_BAR	2U

#define STSHMEM_DEV_NUM     8
#define SYNCTIME_SHM_SIZE   4096

struct stshmem_device {
	struct pci_vdev* pcidev;
	union {
		uint32_t data[4];
		struct {
			uint32_t irq_mask;
			uint32_t irq_state;
			uint32_t ivpos;
			uint32_t doorbell;
		} regs;
	} mmio;
};

static uint8_t stshmem_base[SYNCTIME_SHM_SIZE] __aligned(PDE_SIZE);
static struct stshmem_device stshmem_dev[STSHMEM_DEV_NUM];
static spinlock_t stshmem_dev_lock = { .head = 0U, .tail = 0U, };

void init_stshmem_shared_memory(bool read_only)
{
	uint32_t i;
	uint64_t addr = hva2hpa(&stshmem_base);

	for (i = 0U; i < ARRAY_SIZE(mem_regions); i++) {
		mem_regions[i].hpa = addr;
		addr += mem_regions[i].size;
	}
}

/*
 * @pre name != NULL
 */
static struct stshmem_shm_region *find_shm_region(const char *name)
{
	uint32_t i, num = ARRAY_SIZE(mem_regions);

	for (i = 0U; i < num; i++) {
		if (strncmp(name, mem_regions[i].name, sizeof(mem_regions[0].name)) == 0) {
			break;
		}
	}
	return ((i < num) ? &mem_regions[i] : NULL);
}

/*
 * @post vdev->priv_data != NULL
 */
static void create_stshmem_device(struct pci_vdev *vdev)
{
	uint32_t i;

	spinlock_obtain(&stshmem_dev_lock);
	for (i = 0U; i < STSHMEM_DEV_NUM; i++) {
		if (stshmem_dev[i].pcidev == NULL) {
			stshmem_dev[i].pcidev = vdev;
			vdev->priv_data = &stshmem_dev[i];
			break;
		}
	}
	spinlock_release(&stshmem_dev_lock);
	ASSERT((i < STSHMEM_DEV_NUM), "failed to find and set stshmem device");
}

static int32_t read_stshmem_vdev_cfg(
        const struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t *val)
{
	if (cfg_header_access(offset)) {
		if (vbar_access(vdev, offset)) {
			*val = pci_vdev_read_vbar(vdev, pci_bar_index(offset));
		} else {
			*val = pci_vdev_read_vcfg(vdev, offset, bytes);
		}
	}

	return 0;
}

static void stshmem_vbar_unmap(struct pci_vdev *vdev, uint32_t idx)
{
	struct acrn_vm *vm = vpci2vm(vdev->vpci);
	struct pci_vbar *vbar = &vdev->vbars[idx];

	if (vbar->base_gpa != 0UL) {
        ept_del_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp, vbar->base_gpa, vbar->size);
    }
}

/*
 * @pre vdev->priv_data != NULL
 */
static void stshmem_vbar_map(struct pci_vdev *vdev, uint32_t idx)
{
	struct acrn_vm *vm = vpci2vm(vdev->vpci);
	struct pci_vbar *vbar = &vdev->vbars[idx];
	struct stshmem_device *ivs_dev = (struct stshmem_device *) vdev->priv_data;

	if ((vbar->base_hpa != INVALID_HPA) && (vbar->base_gpa != 0UL)) {
        ept_add_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp, vbar->base_hpa,
                vbar->base_gpa, vbar->size, EPT_RD | EPT_WR | EPT_WB);
    }
}

static int32_t write_stshmem_vdev_cfg(
        struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val)
{
	if (cfg_header_access(offset)) {
		if (vbar_access(vdev, offset)) {
			vpci_update_one_vbar(vdev, pci_bar_index(offset), val,
					stshmem_vbar_map, stshmem_vbar_unmap);
		} else {
			pci_vdev_write_vcfg(vdev, offset, bytes, val);
		}
	}

	return 0;
}

/*
 * @pre vdev != NULL
 * @pre bar_idx < PCI_BAR_COUNT
 */
static void init_stshmem_bar(struct pci_vdev *vdev, uint32_t bar_idx)
{
	struct pci_vbar *vbar;
	enum pci_bar_type type;
	uint64_t addr, mask, size = 0UL;
	struct acrn_vm_pci_dev_config *dev_config = vdev->pci_dev_config;

	addr = dev_config->vbar_base[bar_idx];
	type = pci_get_bar_type((uint32_t) addr);
	mask = (type == PCIBAR_IO_SPACE) ? PCI_BASE_ADDRESS_IO_MASK : PCI_BASE_ADDRESS_MEM_MASK;
	vbar = &vdev->vbars[bar_idx];
	vbar->type = type;

	if (bar_idx == IVSHMEM_SHM_BAR) {
        struct stshmem_shm_region *region = find_shm_region(dev_config->shm_region_name);
        if (region != NULL) {
            size = region->size;
            vbar->base_hpa = region->hpa;
        } else {
            pr_err("%s stshmem device %x:%x.%x has no memory region\n",
                __func__, vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f);
        }
    }

	if (size != 0UL) {
		vbar->size = size;
		vbar->mask = (uint32_t) (~(size - 1UL));
		vbar->fixed = (uint32_t) (addr & (~mask));
		pci_vdev_write_vbar(vdev, bar_idx, (uint32_t) addr);
		if (type == PCIBAR_MEM64) {
			vbar = &vdev->vbars[bar_idx + 1U];
			vbar->type = PCIBAR_MEM64HI;
			vbar->mask = (uint32_t) ((~(size - 1UL)) >> 32U);
			pci_vdev_write_vbar(vdev, (bar_idx + 1U), ((uint32_t)(addr >> 32U)));
		}
	}
}

static void init_stshmem_vdev(struct pci_vdev *vdev)
{
	create_stshmem_device(vdev);

	/* initialize ivshmem config */
	pci_vdev_write_vcfg(vdev, PCIR_VENDOR, 2U, STSHMEM_VENDOR_ID);
	pci_vdev_write_vcfg(vdev, PCIR_DEVICE, 2U, STSHMEM_DEVICE_ID);
	pci_vdev_write_vcfg(vdev, PCIR_REVID, 1U, STSHMEM_REV);
	pci_vdev_write_vcfg(vdev, PCIR_CLASS, 1U, STSHMEM_CLASS);

	/* initialize ivshmem bars */
	vdev->nr_bars = PCI_BAR_COUNT;
	init_stshmem_bar(vdev, STSHMEM_SHM_BAR);

	vdev->user = vdev;
}

/*
 * @pre vdev->priv_data != NULL
 */
static void deinit_stshmem_vdev(struct pci_vdev *vdev)
{
	struct stshmem_device *sts_dev = (struct stshmem_device *) vdev->priv_data;

	sts_dev->pcidev = NULL;
	vdev->priv_data = NULL;
	vdev->user = NULL;
}

const struct pci_vdev_ops vpci_stshmem_ops = {
	.init_vdev	= init_stshmem_vdev,
	.deinit_vdev	= deinit_stshmem_vdev,
	.write_vdev_cfg	= write_stshmem_vdev_cfg,
	.read_vdev_cfg	= read_stshmem_vdev_cfg,
};
#endif
