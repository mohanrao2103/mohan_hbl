/**
 * @file hbl_clock.c
 * @brief MCXE31B system clock initialization
 *
 * Supports:
 *  - FRDM board (crystal)
 *  - Custom board (TCXO)
 */

#include "hbl_clock_mcxe31b.h"
#include <stdint.h>

#define MC_ME_KEY1 0x5AF0U
#define MC_ME_KEY2 0xA50FU

#define FXOSC_BIT (1UL << 21U)
#define PLL_BIT (1UL << 24U)

/*------------------------------------------------------------*/

void Clock_Init(void) {
  /* Enable FXOSC and PLL clocks */
  MC_ME->PRTN1_COFB1_CLKEN |= (FXOSC_BIT | PLL_BIT);

  MC_ME->PRTN1_PCONF |= 1U;
  MC_ME->PRTN1_PUPD |= 1U;

  MC_ME->CTL_KEY = MC_ME_KEY1;
  MC_ME->CTL_KEY = MC_ME_KEY2;

  while (MC_ME->PRTN1_PUPD & 1U) {
  }

#ifdef FRDM_MCXE31B

  /* =====================================================
     Crystal configuration
     ===================================================== */

  FXOSC->CTRL = FXOSC_CTRL_OSCON_MASK | FXOSC_CTRL_GM_SEL(12U) |
                FXOSC_CTRL_EOCV(49U) | FXOSC_CTRL_COMP_EN_MASK;

#else

  /* =====================================================
     TCXO configuration (external clock input)
     ===================================================== */

  /* TCXO mode */
  FXOSC->CTRL =
      FXOSC_CTRL_OSCON_MASK | FXOSC_CTRL_OSC_BYP(1U) | FXOSC_CTRL_EOCV(4U);

#endif

  while (!(FXOSC->STAT & FXOSC_STAT_OSC_STAT_MASK)) {
  }

  /*----------------------------------------------------
    PLL configuration
  ----------------------------------------------------*/

  PLL->PLLCR |= PLL_PLLCR_PLLPD_MASK;

#ifdef FRDM_MCXE31B

  /* Crystal = 16 MHz */

  PLL->PLLDV = PLL_PLLDV_MFI(120U) | PLL_PLLDV_RDIV(2U) | PLL_PLLDV_ODIV2(2U);

#else

  /* TCXO = 16 MHz */

  PLL->PLLDV = PLL_PLLDV_MFI(120U) | PLL_PLLDV_RDIV(2U) | PLL_PLLDV_ODIV2(2U);

#endif

  PLL->PLLFM = PLL_PLLFM_SSCGBYP_MASK;

  PLL->PLLODIV[0] = PLL_PLLODIV_DIV(2U);

  PLL->PLLCR &= ~PLL_PLLCR_PLLPD_MASK;

  while (!(PLL->PLLSR & PLL_PLLSR_LOCK_MASK)) {
  }

  PLL->PLLODIV[0] |= PLL_PLLODIV_DE_MASK;

  /*----------------------------------------------------
    Clock dividers
  ----------------------------------------------------*/

  MC_CGM->MUX_0_DC_0 = MC_CGM_MUX_0_DC_0_DE_MASK | MC_CGM_MUX_0_DC_0_DIV(0U);

  MC_CGM->MUX_0_DC_1 = MC_CGM_MUX_0_DC_1_DE_MASK | MC_CGM_MUX_0_DC_1_DIV(1U);

  MC_CGM->MUX_0_DC_2 = MC_CGM_MUX_0_DC_2_DE_MASK | MC_CGM_MUX_0_DC_2_DIV(3U);

  MC_CGM->MUX_0_DIV_TRIG = MC_CGM_MUX_0_DIV_TRIG_TRIGGER(1U);

  while (MC_CGM->MUX_0_DIV_UPD_STAT & MC_CGM_MUX_0_DIV_UPD_STAT_DIV_STAT_MASK) {
  }

  /*----------------------------------------------------
    Switch system clock to PLL
  ----------------------------------------------------*/

  MC_CGM->MUX_0_CSC =
      MC_CGM_MUX_0_CSC_SELCTL(8U) | MC_CGM_MUX_0_CSC_CLK_SW_MASK;

  while (MC_CGM->MUX_0_CSS & MC_CGM_MUX_0_CSS_SWIP_MASK) {
  }

  while (((MC_CGM->MUX_0_CSS & MC_CGM_MUX_0_CSS_SELSTAT_MASK) >>
          MC_CGM_MUX_0_CSS_SELSTAT_SHIFT) != 8U) {
  }

  SystemCoreClock = 160000000U;
}
