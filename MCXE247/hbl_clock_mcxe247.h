/**
 * @file hbl_clock.h
 * @brief System clock initialization interface.
 *
 * Provides deterministic initialization of the MCXE247 system clock
 * using SOSC and SPLL. The configuration is fixed at compile time
 * and does not support runtime reconfiguration.
 */

#ifndef HBL_CLOCK_H
#define HBL_CLOCK_H

#include "MCXE247.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * API
 ******************************************************************************/

/**
 * @brief Initialize the MCU clock tree.
 *
 * Configures SOSC and SPLL to generate the following clock tree:
 *
 *  Core  = 64 MHz
 *  Bus   = 32 MHz
 *  Flash = 21.33 MHz
 *
 * Peripheral clocks:
 *  SPLLDIV1 = 32 MHz
 *  SPLLDIV2 = 32 MHz
 *
 * Board variants:
 *  - FRDM_MCXE247 : 8 MHz crystal
 *  - External board : 16 MHz TCXO
 */
void hblClockInit(void);

#ifdef __cplusplus
}
#endif

#endif /* HBL_CLOCK_H */
