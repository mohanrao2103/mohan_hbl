#ifndef HBL_CLOCK_MCXE31B_H
#define HBL_CLOCK_MCXE31B_H

#ifdef __cplusplus
extern "C" {
#endif
#include "MCXE31B.h"     /* Device register definitions */
#include "PERI_FXOSC.h"  /* FXOSC register interface */
#include "PERI_MC_CGM.h" /* clock generator module */
#include "PERI_PLL.h"    /* PLL register interface */

/**
 * @brief System clock initialization for MCXE31B
 *
 * @details
 * Initializes the primary system clock tree using:
 * - FXOSC external crystal
 * - PLL as main clock source
 * - System clock switched to PLL_PHI0
 *
 * Resulting clock domains:
 * - CORE_CLK      = 160 MHz
 * - AIPS_PLAT_CLK = 80 MHz
 * - AIPS_SLOW_CLK = 40 MHz
 *
 * @note
 * Designed for deterministic start-up behaviour.
 * Intended for safety-critical systems (SIL-4 style minimal logic).
 *
 * @warning
 * Must be called once during system startup before peripheral initialization.
 */
void Clock_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* HBL_CLOCK_MCXE31B_H */
