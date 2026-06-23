/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file console.h
 * @brief ANSI colour codes for zego bricks.
 *
 * All macros are compile-time string literals — zero runtime cost.
 *
 * Usage:
 *   #include "console.h"
 *   LOG_INF(CLR_BLU "hello" CLR_RST);
 */

#pragma once

/* ============================================================================
 * ANSI foreground colours
 * ============================================================================
 */
#define CLR_BLK "\033[30m"
#define CLR_RED "\033[31m"
#define CLR_GRN "\033[32m"
#define CLR_YLW "\033[33m"
#define CLR_BLU "\033[34m"
#define CLR_PRP "\033[35m"
#define CLR_CYN "\033[36m"
#define CLR_WHT "\033[37m"

/* ============================================================================
 * ANSI background colours
 * ============================================================================
 */
#define CLR_BLKB "\033[40m"
#define CLR_REDB "\033[41m"
#define CLR_GRNB "\033[42m"
#define CLR_YLWB "\033[43m"
#define CLR_BLUB "\033[44m"
#define CLR_PRPB "\033[45m"
#define CLR_CYNB "\033[46m"
#define CLR_WHTB "\033[47m"

/* ============================================================================
 * Reset
 * ============================================================================
 */
#define CLR_RST "\033[0m"
