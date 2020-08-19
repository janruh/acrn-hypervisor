/*
 * Copyright (c) 2020 TTTech Computertechnik AG.
 * All rights reserved.
 *
 * Jan Ruh, jan.ruh@tttech.com
 */

#ifndef TIME_H
#define TIME_H

#ifdef CONFIG_SYNCHRONIZED_TIME_ENABLED

/**
 * @brief Initialize synchronized time memory regions
 *
 * Initialize synchronized time shared memory region
 *
 * @return None
 */
void init_synctime_shared_memory(void);

#endif /* CONFIG_SYNCHRONIZED_TIME_ENABLED */

#endif /* TIME_H */
