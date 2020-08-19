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

#define SYNCTIME_SHM_SIZE 4096

static uint8_t synctime_shmem_base[SYNCTIME_SHM_SIZE] __aligned(PDE_SIZE);

static void synctime_shmem_map(bool read_only)
{
    uint64_t base_hpa = hva2hpa(&synctime_shmem_base);

    ept_add_mr(vm, (uint64_t*) vm->arch_vm.nworld_eptp, base_hpa, base_gpa,
            SYNCTIME_SHM_SIZE, EPT_RD | EPT_WB | (read_only ? 0: EPT_WR));
}

#endif
