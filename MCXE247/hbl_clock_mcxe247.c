/**
 * @file hbl_clock.c
 * @brief Deterministic system clock initialization for MCXE247.
 *
 * This module configures the System Clock Generator (SCG) to use
 * the external oscillator (SOSC) and System PLL (SPLL).
 *
 * Final clock tree:
 *  Core  = 64 MHz
 *  Bus   = 32 MHz
 *  Flash = 21.33 MHz
 *
 * Peripheral clocks:
 *  SPLLDIV1 = 32 MHz
 *  SPLLDIV2 = 32 MHz
 */
#include "hbl_clock.h"

/*******************************************************************************
 * API implementation
 ******************************************************************************/

/**
 * @brief Initialize system clocks using SOSC and SPLL.
 *
 * The configuration depends on the hardware platform:
 *
 *  FRDM_MCXE247
 *      - 8 MHz external crystal
 *
 *  Custom board
 *      - 16 MHz external TCXO
 *
 * The PLL generates 256 MHz VCO which internally produces
 * 128 MHz PLL output used to derive the system clocks.
 */
void hblClockInit(void) {

#ifdef FRDM_MCXE247

  /* =========================================================
   * 8 MHz Crystal Configuration (FRDM board)
   * ========================================================= */

  /* Configure SOSC oscillator characteristics */
  SCG->SOSCCFG =
      SCG_SOSCCFG_RANGE(3u) | /* Select high frequency range (8–40 MHz) */
      SCG_SOSCCFG_EREFS_MASK; /* Enable crystal oscillator mode */

  /* Enable SOSC and activate clock monitor */
  SCG->SOSCCSR =
      SCG_SOSCCSR_SOSCEN_MASK |  /* Enable System Oscillator (SOSC) */
      SCG_SOSCCSR_SOSCCM_MASK |  /* Enable clock monitor for SOSC */
      SCG_SOSCCSR_SOSCCMRE_MASK; /* Reset MCU if SOSC failure detected */

  /* Wait until oscillator stabilizes and becomes valid */
  while (!(SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK)) {
  }

  /* Configure SOSC output dividers */
  SCG->SOSCDIV = SCG_SOSCDIV_SOSCDIV1(0u) | /* Disable SOSCDIV1 output */
                 SCG_SOSCDIV_SOSCDIV2(1u);  /* Divide by 1 → 8 MHz output */

  /* ---------------------------------------------------------
   * Configure PLL input
   *
   * 8 MHz → PREDIV /1 → 8 MHz
   * 8 MHz → MULT ×32 → 256 MHz VCO
   * --------------------------------------------------------- */

  SCG->SPLLCFG = SCG_SPLLCFG_PREDIV(0u) | /* PLL pre-divider = /1 */
                 SCG_SPLLCFG_MULT(16u);   /* PLL multiplier = ×32 */

#else

  /* =========================================================
   * 16 MHz TCXO Configuration (external clock input)
   * ========================================================= */

  /* Configure SOSC for external clock source (bypass crystal) */
  SCG->SOSCCFG = SCG_SOSCCFG_RANGE(3u) | /* High frequency oscillator range */
                 SCG_SOSCCFG_EREFS(0u);  /* External clock input mode */

  /* Enable SOSC and clock monitor */
  SCG->SOSCCSR = SCG_SOSCCSR_SOSCEN_MASK |  /* Enable System Oscillator */
                 SCG_SOSCCSR_SOSCCM_MASK |  /* Enable oscillator monitor */
                 SCG_SOSCCSR_SOSCCMRE_MASK; /* Reset MCU if oscillator fails */

  /* Wait until oscillator becomes valid */
  while (!(SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK)) {
  }

  /* Configure SOSC clock outputs */
  SCG->SOSCDIV = SCG_SOSCDIV_SOSCDIV1(0u) | /* Disable divider output 1 */
                 SCG_SOSCDIV_SOSCDIV2(1u);  /* Divide by 1 → 16 MHz */

  /* ---------------------------------------------------------
   * Configure PLL input
   *
   * 16 MHz → PREDIV /2 → 8 MHz
   * 8 MHz  → MULT ×32 → 256 MHz VCO
   * --------------------------------------------------------- */

  SCG->SPLLCFG = SCG_SPLLCFG_PREDIV(1u) | /* PLL pre-divider = /2 */
                 SCG_SPLLCFG_MULT(16u);   /* PLL multiplier = ×32 */

#endif

  /* =========================================================
   * Configure PLL peripheral clock outputs
   * ========================================================= */

  SCG->SPLLDIV = SCG_SPLLDIV_SPLLDIV1(2u) | /* Divider /4 → 128 / 4 = 32 MHz */
                 SCG_SPLLDIV_SPLLDIV2(2u); /* Divider /4 → 128 / 4 = 32 MHz */

  /* Enable PLL and its clock monitor */
  SCG->SPLLCSR = SCG_SPLLCSR_SPLLEN_MASK |  /* Enable System PLL */
                 SCG_SPLLCSR_SPLLCM_MASK |  /* Enable PLL clock monitor */
                 SCG_SPLLCSR_SPLLCMRE_MASK; /* Reset MCU if PLL fails */

  /* Wait until PLL locks and becomes valid */
  while (!(SCG->SPLLCSR & SCG_SPLLCSR_SPLLVLD_MASK)) {
  }

  /* =========================================================
   * Switch system clock source to SPLL
   * ========================================================= */

  SCG->RCCR = SCG_RCCR_SCS(6u) |     /* Select SPLL as system clock source */
              SCG_RCCR_DIVCORE(1u) | /* Core clock divider /2 → 64 MHz */
              SCG_RCCR_DIVBUS(1u) |  /* Bus clock divider /4 → 32 MHz */
              SCG_RCCR_DIVSLOW(2u);  /* Flash clock divider /6 → 21.33 MHz */

  /* Wait until system clock switch completes */
  while ((SCG->CSR & SCG_CSR_SCS_MASK) != SCG_CSR_SCS(6u)) {
  }

  /* Update CMSIS global variable */
  SystemCoreClock = 64000000U;
}
