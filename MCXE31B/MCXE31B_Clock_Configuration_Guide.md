# MCXE31B Clock Configuration Guide

**Source:** MCXE31x Reference Manual, Rev. 2, 2025-08-05  
**Variant Coverage:** MCXE31B (160 MHz Cortex-M7, full feature set)  
**Chapters Referenced:** 23 (Clocking), 24 (MC_CGM), 25 (FIRC), 26 (SIRC), 27 (FXOSC), 28 (SXOSC), 29 (PLLDIG)

---

## Table of Contents

1. [Clock Tree Architecture](#1-clock-tree-architecture)
2. [Clock Sources Deep Dive](#2-clock-sources-deep-dive)
3. [System Clock Frequency Limits](#3-system-clock-frequency-limits)
4. [Real Configuration Examples (Options A–F)](#4-real-configuration-examples)
5. [Power Modes and Clock Behavior](#5-power-modes-and-clock-behavior)
6. [Step-by-Step Initialization Sequence](#6-step-by-step-initialization-sequence)
7. [Clock Switching Protocol (FIRC ↔ PLL)](#7-clock-switching-protocol)
8. [Clock Divider Update Procedure](#8-clock-divider-update-procedure)
9. [PCFS — Progressive Clock Frequency Switching](#9-pcfs)
10. [Clock Monitor Safety (CMU_FC / CMU_FM)](#10-clock-monitor-safety)
11. [MC_CGM Register Reference](#11-mc_cgm-register-reference)
12. [PLLDIG Register Reference](#12-plldig-register-reference)
13. [PLL Calculation and Formulas](#13-pll-calculation-and-formulas)
14. [Peripheral Clock Sources Summary](#14-peripheral-clock-sources-summary)
15. [CLKOUT Debug Feature](#15-clkout-debug-feature)
16. [External Oscillator Configuration (FXOSC / SXOSC)](#16-external-oscillator-configuration)
17. [Common Mistakes and Gotchas](#17-common-mistakes-and-gotchas)
18. [Safe Production Initialization Checklist](#18-safe-production-initialization-checklist)

---

## 1. Clock Tree Architecture

### MCXE31B Clock System at a Glance

```
Clock Sources
═════════════
FIRC (48 MHz) ────── DIV_SEL(÷1/2/8/16) ──┐
FXOSC (8-40 MHz) ──────────────────────────┤
SIRC (32 kHz) ─────────────────────────────┤
SXOSC (32.768 kHz) ────────────────────────┤──→ MC_CGM MUX_0 → PCFS → CORE_CLK (160 MHz)
PLL_PHI0_CLK ──────────────────────────────┤                          AIPS_PLAT_CLK (80 MHz)
PLL_PHI1_CLK ──────────────────────────────┘                          AIPS_SLOW_CLK (40 MHz)
                                                                       HSE_CLK (80 MHz)
                                                                       DCM_CLK (40 MHz)
                                                                       LBIST_CLK (40 MHz)
                                                                       QSPI_MEM_CLK (160 MHz)

Other Muxes (MCXE31B specific)
══════════════════════════════
MUX_1 → STM0_CLK (÷1..2 from AIPS_PLAT/FXOSC/FIRC)
MUX_3 → FLEXCAN[0:2]_PE_CLK (÷1..4)
MUX_4 → FLEXCAN[3:5]_PE_CLK (÷1..4)  [MCXE31B only]
MUX_5 → CLKOUT_STANDBY (÷1..8)
MUX_6 → CLKOUT_RUN (÷1..64)
MUX_7 → EMAC_RX_CLK (÷1..64)         [MCXE31B only]
MUX_8 → EMAC_TX_CLK (÷1..64)         [MCXE31B only]
MUX_9 → EMAC_TS_CLK (÷1..64)         [MCXE31B only]
MUX_10 → QSPI_SFCK (÷1..8)           [MCXE31B only]
MUX_11 → TRACE_CLK (÷1..8)

CMU Monitors
═══════════
CMU_FC_0: monitors FXOSC_CLK (ref = FIRC) → destructive reset or interrupt
CMU_FC_3: monitors CORE_CLK (ref = FXOSC) → destructive reset
CMU_FC_4: monitors AIPS_PLAT_CLK (ref = FIRC) → destructive reset
CMU_FC_5: monitors HSE_CLK (ref = FIRC) → destructive reset
CMU_FM_1: meters FIRC_CLK (ref = FXOSC) → interrupt
CMU_FM_2: meters SIRC_CLK (ref = FXOSC) → interrupt
```

### Key Terminology

| Term | Meaning |
|------|---------|
| FIRC | Fast Internal RC Oscillator — 48 MHz, always-on in Run mode |
| SIRC | Slow Internal RC Oscillator — 32 kHz, always-on in Run mode |
| FXOSC | Fast Crystal Oscillator — 8–40 MHz external crystal/bypass |
| SXOSC | Slow Crystal Oscillator — 32.768 kHz (not available on MCXE315/316) |
| PLLDIG | PLL Digital Interface — generates PLL_PHI0/PHI1 outputs |
| MC_CGM | Clock Generation Module — all muxes, dividers, PCFS |
| MC_ME | Mode Entry Module — peripheral clock gating via partition control |
| CMU_FC | Clock Monitoring Unit (Frequency Check) — FHH/FLL detection |
| CMU_FM | Clock Monitoring Unit (Frequency Meter) — periodic frequency measurement |
| PCFS | Progressive Clock Frequency Switching — smooth ramp during source change |
| CORE_CLK | Application core clock (Cortex-M7), also clocks AXBS, SRAM, Flash port |
| AIPS_PLAT_CLK | Medium-speed peripheral clock (≤80 MHz) |
| AIPS_SLOW_CLK | Slow-speed peripheral clock (≤40 MHz) |
| HSE_CLK | Hardware Security Engine clock |
| FIRC_DIV_SEL | ELE_HSEB.CONFIG_REG_GPR field controlling FIRC divider (÷1/2/8/16) |

---

## 2. Clock Sources Deep Dive

### 2.1 FIRC — Fast Internal RC Oscillator

**Base address:** 402D_0000h  
**Frequency:** Nominally 48 MHz at the raw oscillator; application software selects output via FIRC_DIV_SEL

| FIRC_DIV_SEL[1:0] | FIRC_CLK output |
|---|---|
| 11 | 48 MHz (÷1) — default after normal reset/standby exit |
| 00 | 24 MHz (÷2) — used in fast standby exit path |
| 01 | 6 MHz (÷8) — low-speed operation |
| 10 | 3 MHz (÷16) — very-low-speed operation |

**Key facts from RM §23.4.4:**
- FIRC_CLK is the **default system clock after any reset** and is always enabled in Run mode; it cannot be disabled in Run mode.
- Serves as the **safe clock** for FCCU, FOSU, SIUL2 filters, and MC_RGM.
- Monitored by CMU_FM_1 (with FXOSC as reference) and when used as system clock by CMU_FC_3/4/5.
- Can optionally be enabled in Standby mode via `FIRC.STDBY_ENABLE[STDBY_EN]`.

**FIRC Status Register (FIRC base + 0x04):**
- Bit 0 `STATUS`: 0 = FIRC off or unstable; 1 = FIRC on and stable

**FIRC Standby Enable Register (FIRC base + 0x08):**
- Bit 0 `STDBY_EN`: 1 = keep FIRC enabled in Standby mode

### 2.2 SIRC — Slow Internal RC Oscillator

**Base address:** 402C_8000h  
**Frequency:** 32 kHz  
- Always enabled in Run mode; cannot be disabled in Run mode.
- Used as clock source for SWT (Software Watchdog Timer) and POR_WDG.
- Monitored by CMU_FM_2 (with FXOSC as reference).
- Stabilization: 96 SIRC_CLK cycles after enable.
- Can be enabled in Standby via `SIRC.MISCELLANEOUS_IN[STANDBY_ENABLE]` (bit 8).

**SIRC SR Register (SIRC base + 0x04):**
- Bit 0 `STATUS`: 0 = SIRC off/unstable; 1 = SIRC on and stable

### 2.3 FXOSC — Fast Crystal Oscillator

**Base address:** 402D_4000h  
**Frequency range:** 8–40 MHz (crystal or bypass input)  

**Two operating modes:**

| Mode | CTRL[OSC_BYP] | CTRL[COMP_EN] | FXOSC_CLK output |
|---|---|---|---|
| Power-Down (default after reset) | X | X | 0 (off) |
| Crystal mode | 0 | 1 | Crystal clock |
| Single-Input Bypass mode | 1 | 0 | EXTAL pin |

**Key register fields (CTRL register, offset 0x00, reset = 019D_00C0h):**

| Field | Bits | Description |
|---|---|---|
| OSC_BYP | [31] | 0 = crystal mode; 1 = bypass (EXTAL pin) |
| COMP_EN | [24] | 1 = comparator on (Crystal mode only) |
| EOCV | [23:16] | End-of-count value; stabilization = EOCV × 128 × (4 × crystal period) |
| GM_SEL | [7:4] | Transconductance setting (0000b = 0×, must be non-zero in crystal mode) |
| OSCON | [0] | 1 = enable FXOSC; 0 = power down |

**FXOSC STAT register (offset 0x04):**
- Bit 31 `OSC_STAT`: 1 = stable; poll this before using FXOSC_CLK.

> **CRITICAL (RM §27.1.1 MCXE31B Note):** In Bypass mode, the EXTAL pin must be driven low when FXOSC is OFF. The external clock may only be enabled after FXOSC is enabled. The external clock must already be inactive before disabling FXOSC.

### 2.4 SXOSC — Slow Crystal Oscillator

**Base address:** 402C_C000h  
**Frequency:** 32.768 kHz  
**Not available on:** MCXE315/MCXE316 packages

- Used by RTC for operation across functional reset (SXOSC survives functional reset; only cleared on destructive reset).
- Default state: **disabled** (OSCON = 0).

**SXOSC_CTRL register (offset 0x00):**
- Bits [23:16] `EOCV`: Stabilization time = EOCV × 128 × (4 × clock period)
- Bit [0] `OSCON`: 0 = disabled (default); 1 = enabled

**SXOSC_STAT register (offset 0x04):**
- Bit 31 `OSC_STAT`: 1 = stable; poll before using SXOSC_CLK

### 2.5 PLL — Phase-Locked Loop

**PLLDIG base address:** 402E_0000h  
**VCO frequency range:** 640–1280 MHz (see MCXE31 Data Sheet for exact limits)  
**Reference clock:** FXOSC_CLK (must be stable before PLL is enabled)

**PLL output paths:**
- `PLLODIV2_CLK` = VCO / (PLLDV[ODIV2] × 2) — pre-divides VCO internally before PHI dividers
- `PLL_PHI0_CLK` = VCO / (2 × PLLDV[ODIV2]) / PLLODIV_0[DIV] — system clock source
- `PLL_PHI1_CLK` = VCO / (2 × PLLDV[ODIV2]) / PLLODIV_1[DIV] — QuadSPI, Trace, EMAC

---

## 3. System Clock Frequency Limits

**From RM §23.7.1, Table 123 (MCXE31B only)**

| Clock Domain | Register | Max Frequency | Constraint |
|---|---|---|---|
| CORE_CLK | MC_CGM.MUX_0_DC_0[DIV] | **160 MHz** | Must be ≥ AIPS_PLAT_CLK |
| AIPS_PLAT_CLK | MC_CGM.MUX_0_DC_1[DIV] | **80 MHz** | Must be ≤ CORE_CLK |
| AIPS_SLOW_CLK | MC_CGM.MUX_0_DC_2[DIV] | **40 MHz** | Must be ≤ AIPS_PLAT_CLK |
| HSE_CLK | MC_CGM.MUX_0_DC_3[DIV] | **120 MHz** (or CORE_CLK/2 if CORE > 120 MHz) | When CORE > 120 MHz, HSE_CLK must be CORE/2 |
| DCM_CLK | MC_CGM.MUX_0_DC_4[DIV] | **48 MHz** | — |
| LBIST_CLK | MC_CGM.MUX_0_DC_5[DIV] | **48 MHz** | — |
| QSPI_MEM_CLK | MC_CGM.MUX_0_DC_6[DIV] | **160 MHz** | Must equal CORE_CLK (except 1:1 mode) |

> **CORE_CLK constraint:** CORE_CLK ≥ AIPS_PLAT_CLK always. Violating this causes undefined behavior.

> **HSE_CLK constraint (RM §23.7.1 note):** When CORE_CLK ≤ 120 MHz, HSE_CLK can equal CORE_CLK. When CORE_CLK > 120 MHz (i.e., 160 MHz), HSE_CLK must be CORE_CLK / 2 = 80 MHz.

> **ADC constraint (RM §23.6.1.8.1):** For MCXE31B, AD_CLK ≤ 80 MHz. Minimum AD_CLK is 6 MHz.

> **TCK/HSE_CLK ratio:** TCK must be ≤ HSE_CLK ÷ 1.5 (RM §23.3.2 note). E.g., at HSE_CLK = 80 MHz, TCK ≤ 53 MHz.

---

## 4. Real Configuration Examples

All examples sourced from RM §23.7.2. MC_CGM.MUX_0_CSC[SELCTL] encoding: 0000b = FIRC, 1000b = PLL_PHI0.

### Option A — High Performance (CORE_CLK = 160 MHz)

**MCXE31B only.** Uses PLL_PHI0 at 160 MHz as system clock source.

| Clock | Value | Divider Register Value |
|---|---|---|
| PLL VCO | 960 MHz | — |
| PLLODIV2_CLK | 480 MHz | PLLDV[ODIV2] = 02h |
| PLL_PHI0_CLK | 160 MHz | PLLODIV_0[DIV] = 0010b (÷3) |
| PLL_PHI1_CLK | 240 MHz | PLLODIV_1[DIV] = 0001b (÷2) |
| CORE_CLK | **160 MHz** | MUX_0_DC_0[DIV] = 0000b (÷1) |
| QSPI_MEM_CLK | 160 MHz | MUX_0_DC_6[DIV] = 0000b (÷1) |
| AIPS_PLAT_CLK | 80 MHz | MUX_0_DC_1[DIV] = 0001b (÷2) |
| AIPS_SLOW_CLK | 40 MHz | MUX_0_DC_2[DIV] = 0011b (÷4) |
| HSE_CLK | 80 MHz | MUX_0_DC_3[DIV] = 0001b (÷2) |
| DCM_CLK | 40 MHz | MUX_0_DC_4[DIV] = 0011b (÷4) |
| LBIST_CLK | 40 MHz | MUX_0_DC_5[DIV] = 0011b (÷4) |
| QSPI_SFCK | 120 MHz | MUX_10_DC_0[DIV] = 0001b (÷2) |
| TRACE_CLK | 120 MHz (fast) / 80 MHz (med) | MUX_11_DC_0[DIV] = 0001b |

**MUX_0_CSC[SELCTL]** = 1000b (PLL_PHI0)  
**MUX_10_CSC[SELCTL] / MUX_11_CSC[SELCTL]** = 1001b (PLL_PHI1)

### Option B — Reduced Speed (CORE_CLK = 120 MHz)

Available on all MCXE31 variants.

| Clock | MCXE31B | MCXE315/316/317 |
|---|---|---|
| PLL VCO | 960 MHz | 960 MHz |
| PLLODIV2_CLK | 480 MHz (02h) | 240 MHz (04h) |
| PLL_PHI0_CLK | 120 MHz (011b) | 120 MHz (001b) |
| CORE_CLK | **120 MHz** (000b) | **120 MHz** (000b) |
| AIPS_PLAT_CLK | 60 MHz (001b) | 60 MHz (001b) |
| AIPS_SLOW_CLK | 30 MHz (011b) | 30 MHz (011b) |
| HSE_CLK | 120 MHz (000b) | 120 MHz (000b) |
| DCM_CLK | 30 MHz (011b) | 30 MHz (011b) |

**MUX_0_CSC[SELCTL]** = 1000b (PLL_PHI0)

### Option C — Boot Standby (CORE_CLK = 24 MHz)

**Default state after fast standby exit.** FIRC as system clock, PLL off.

| Clock | Value | Setting |
|---|---|---|
| FIRC_DIV_SEL | 00b | 24 MHz (÷2) |
| CORE_CLK | **24 MHz** | MUX_0_DC_0[DIV] = 0000b |
| AIPS_PLAT_CLK | 24 MHz | MUX_0_DC_1[DIV] = 0000b |
| AIPS_SLOW_CLK | 12 MHz | MUX_0_DC_2[DIV] = 0001b |
| HSE_CLK | 24 MHz | MUX_0_DC_3[DIV] = 0000b |
| DCM_CLK | 24 MHz | MUX_0_DC_4[DIV] = 0000b |

**MUX_0_CSC[SELCTL]** = 0000b (FIRC)  
Note: After normal reset/standby exit, FIRC_DIV_SEL = 11b (48 MHz). Fast standby exit sets it to 00b (24 MHz). This is managed by the sBAF code.

### Option D — Low-Speed Run (CORE_CLK = 48 MHz)

FIRC at 48 MHz as system clock.

| Clock | Value | Setting |
|---|---|---|
| FIRC_DIV_SEL | 11b | 48 MHz |
| CORE_CLK | **48 MHz** | MUX_0_DC_0[DIV] = 0000b |
| AIPS_PLAT_CLK | 48 MHz | MUX_0_DC_1[DIV] = 0000b |
| AIPS_SLOW_CLK | 24 MHz | MUX_0_DC_2[DIV] = 0001b |
| HSE_CLK | 48 MHz | MUX_0_DC_3[DIV] = 0000b |

**MUX_0_CSC[SELCTL]** = 0000b (FIRC)

### Option E — Low-Speed Run (CORE_CLK = 3 MHz)

FIRC at 3 MHz (÷16 divider).

| Clock | Value | Setting |
|---|---|---|
| FIRC_DIV_SEL | 10b | 3 MHz (÷16) |
| CORE_CLK | **3 MHz** | MUX_0_DC_0[DIV] = 0000b |
| AIPS_PLAT_CLK | 3 MHz | MUX_0_DC_1[DIV] = 0000b |
| AIPS_SLOW_CLK | 1.5 MHz | MUX_0_DC_2[DIV] = 0001b |

> **IMPORTANT (RM §23.7.2.5 Note):** For FIRC_CLK < 24 MHz, **CMU_FC_x instances must be disabled** for safety applications, because these CMUs will generate erroneous FHH events at such low frequencies. Safety applications should run on PLL clocks.

### Option E2 — Very-Low-Speed Run (CORE_CLK = 750 kHz)

FIRC at 3 MHz, then MUX_0_DC_0 divides further.

| Clock | Value | Setting |
|---|---|---|
| FIRC_DIV_SEL | 10b | 3 MHz |
| CORE_CLK | **750 kHz** | MUX_0_DC_0[DIV] = 0011b (÷4 of 3 MHz) |
| AIPS_PLAT_CLK | 750 kHz | MUX_0_DC_1[DIV] = 0011b |
| AIPS_SLOW_CLK | 375 kHz | MUX_0_DC_2[DIV] = 0111b |

### Option F — 1:1 Mode (CORE_CLK = AXBS_CLK, MCXE31B = 160 MHz)

Core and slave ports at the same frequency. PRAM/SRAM wait states enabled.

| Clock | MCXE31B | Setting |
|---|---|---|
| PLL_PHI0_CLK | 160 MHz | PLLODIV_0[DIV] = 010b |
| CORE_CLK | **160 MHz** | MUX_0_DC_0[DIV] = 0000b |
| AIPS_PLAT_CLK | 80 MHz | MUX_0_DC_1[DIV] = 0001b |
| AIPS_SLOW_CLK | 40 MHz | MUX_0_DC_2[DIV] = 0011b |
| HSE_CLK | 80 MHz | MUX_0_DC_3[DIV] = 0001b |
| QSPI_MEM_CLK | 160 MHz | MUX_0_DC_6[DIV] = 0000b |

> **Difference from Option A:** QSPI_MEM_CLK = CORE_CLK in 1:1 mode. PRAM/SRAM wait states must be enabled for this mode. See RM §23.7.3 Gasket configurations.

---

## 5. Power Modes and Clock Behavior

### Run Mode

All clock sources available. FIRC and SIRC always-on. PLL optional.

| Source | Run Mode |
|---|---|
| FIRC | Always on, cannot disable |
| SIRC | Always on, cannot disable |
| FXOSC | Optional (disabled on functional reset) |
| SXOSC | Optional (survives functional reset; only disabled on destructive reset) |
| PLL | Optional (disabled on functional reset) |

### Standby Mode

- CLKOUT_RUN not available during Standby.
- CLKOUT_STANDBY registers are latched at Standby entry and reset in Standby sequence — must be reconfigured on exit.
- FIRC optional in Standby via `FIRC.STDBY_ENABLE[STDBY_EN]`.
- SIRC optional in Standby via `SIRC.MISCELLANEOUS_IN[STANDBY_ENABLE]`.
- SXOSC survives Standby for RTC operation.
- PIT RTI timer can operate in Standby using SIRC_CLK when `PIT_0.MCR[MDIS_RTI]=0`.

### Reset Behavior

| Reset type | Clock state after reset |
|---|---|
| Functional reset | FIRC enabled (FIRC_DIV_SEL = 11b → 48MHz), SIRC enabled, all others disabled |
| Destructive reset | All sources re-initialized to POR state including SXOSC |
| POR | FIRC enabled asynchronously after deassertion |

**Default clock after any functional reset:** FIRC_CLK (48 MHz, FIRC_DIV_SEL = 11b), CORE_CLK = 48 MHz via MUX_0 with DIV=0.

---

## 6. Step-by-Step Initialization Sequence

This covers the full sequence to configure from reset state to Option A (160 MHz PLL).

### Step 1: Configure PRAM/SRAM Wait States (FIRST — do before any frequency change)

For Option A (160 MHz) where CORE_CLK > AIPS_PLAT_CLK, configure the necessary gasket and wait state settings per RM §23.7.3, Table 131. For the 2:1 eDMA (S0) gasket and PRAM/SRAM bypass (no WS for option A).

### Step 2: Configure PMC Last-Mile Regulator

Before enabling PLL, enable the last-mile voltage regulator (required for PLL operation):

```c
// PMC.CONFIG[LMEN] = 1 to enable last mile regulator
// PMC.CONFIG[LMBCTLEN] = 1 if using external BJT transistor
PMC->CONFIG |= PMC_CONFIG_LMEN(1);
```

### Step 3: Configure and Enable FXOSC

PLL requires FXOSC as its reference. Configure FXOSC while it is disabled.

```c
// FXOSC base: 0x402D_4000
#define FXOSC_BASE  0x402D4000U
#define FXOSC_CTRL  (*(volatile uint32_t*)(FXOSC_BASE + 0x00))
#define FXOSC_STAT  (*(volatile uint32_t*)(FXOSC_BASE + 0x04))

// For 8 MHz crystal, Crystal mode:
// COMP_EN=1, GM_SEL=1111b(1x transconductance), EOCV set for startup time
// EOCV = startup_time_ns / (4 * 128 * crystal_period_ns)
// Example: 5ms startup, 8MHz crystal → 5000000 / (4 * 128 * 125) = ~78 → 0x4E
FXOSC_CTRL = (1u << 24)          // COMP_EN
           | (0x4Eu << 16)       // EOCV
           | (0xFu << 4)         // GM_SEL = 1111b (max transconductance)
           | (1u << 0);          // OSCON = enable

// Poll for stable clock
while (!(FXOSC_STAT & (1u << 31))); // Wait OSC_STAT = 1
```

### Step 4: Configure PLL (While PLL is Powered Down)

```c
// PLLDIG base: 0x402E_0000
#define PLLDIG_BASE     0x402E0000U
#define PLLDIG_PLLCR    (*(volatile uint32_t*)(PLLDIG_BASE + 0x00))
#define PLLDIG_PLLSR    (*(volatile uint32_t*)(PLLDIG_BASE + 0x04))
#define PLLDIG_PLLDV    (*(volatile uint32_t*)(PLLDIG_BASE + 0x08))
#define PLLDIG_PLLODIV0 (*(volatile uint32_t*)(PLLDIG_BASE + 0x80))
#define PLLDIG_PLLODIV1 (*(volatile uint32_t*)(PLLDIG_BASE + 0x84))

// 1. Confirm PLL powered down (PLLCR[PLLPD] = 1, default after reset)
// 2. Confirm all PLLODIV_n[DE] = 0 (dividers disabled, default)

// Configure PLLDV for 960 MHz VCO (Option A: 8 MHz FXOSC reference)
// fVCO = fREF / RDIV * MFI = 8 / 1 * 120 = 960 MHz
// PLLDV: MFI=120 (0x78), RDIV=1, ODIV2=2 (÷2 = 480 MHz PLLODC2_CLK)
PLLDIG_PLLDV = (2u << 24)    // ODIV2 = 2 → 480 MHz
             | (1u << 12)    // RDIV = 1 (÷1)
             | (120u << 0);  // MFI = 120

// Configure PHI0: 160 MHz = PLLODIV2_CLK / PHI0_DIV
// 480 / 3 = 160 MHz → DIV = 2 (encodes as ÷3, i.e., DIV+1=3)
PLLDIG_PLLODIV0 = (2u << 16); // DIV = 2 (DE bit written later)

// Configure PHI1: 240 MHz = 480 / 2 → DIV = 1 (÷2)
PLLDIG_PLLODIV1 = (1u << 16); // DIV = 1 (DE bit written later)
```

### Step 5: Configure MC_CGM Dividers for Option A

```c
// MC_CGM base: 0x402D_8000
#define MC_CGM_BASE         0x402D8000U
#define MUX_0_CSC           (*(volatile uint32_t*)(MC_CGM_BASE + 0x300))
#define MUX_0_CSS           (*(volatile uint32_t*)(MC_CGM_BASE + 0x304))
#define MUX_0_DC_0          (*(volatile uint32_t*)(MC_CGM_BASE + 0x308))
#define MUX_0_DC_1          (*(volatile uint32_t*)(MC_CGM_BASE + 0x30C))
#define MUX_0_DC_2          (*(volatile uint32_t*)(MC_CGM_BASE + 0x310))
#define MUX_0_DC_3          (*(volatile uint32_t*)(MC_CGM_BASE + 0x314))
#define MUX_0_DC_4          (*(volatile uint32_t*)(MC_CGM_BASE + 0x318))
#define MUX_0_DC_5          (*(volatile uint32_t*)(MC_CGM_BASE + 0x31C))
#define MUX_0_DC_6          (*(volatile uint32_t*)(MC_CGM_BASE + 0x320))
#define MUX_0_DIV_TRIG_CTRL (*(volatile uint32_t*)(MC_CGM_BASE + 0x334))
#define MUX_0_DIV_TRIG      (*(volatile uint32_t*)(MC_CGM_BASE + 0x338))
#define MUX_0_DIV_UPD_STAT  (*(volatile uint32_t*)(MC_CGM_BASE + 0x33C))

// Configure Option A dividers (PLL_PHI0 @ 160 MHz source)
// Each DC_x register: bit31=DE(enable), bits[21:16]=DIV (0=÷1, 1=÷2, ...)
MUX_0_DC_0 = (1u<<31) | (0u<<16); // CORE: 160 MHz (÷1)
MUX_0_DC_1 = (1u<<31) | (1u<<16); // AIPS_PLAT: 80 MHz (÷2)
MUX_0_DC_2 = (1u<<31) | (3u<<16); // AIPS_SLOW: 40 MHz (÷4)
MUX_0_DC_3 = (1u<<31) | (1u<<16); // HSE: 80 MHz (÷2)
MUX_0_DC_4 = (1u<<31) | (3u<<16); // DCM: 40 MHz (÷4)
MUX_0_DC_5 = (1u<<31) | (3u<<16); // LBIST: 40 MHz (÷4)
MUX_0_DC_6 = (1u<<31) | (0u<<16); // QSPI_MEM: 160 MHz (÷1)

// Trigger divider update with halt handshake (HHEN=1, TCTL=1)
MUX_0_DIV_TRIG_CTRL = (1u<<31) | (1u<<0); // HHEN=1, TCTL=1
MUX_0_DIV_TRIG = 0xFFFFFFFFu;              // Write to trigger
while (MUX_0_DIV_UPD_STAT & 1u);           // Wait update complete
```

### Step 6: Enable PLL and Wait for Lock

```c
// 6a. Power up PLL (write 0 to PLLCR[PLLPD])
PLLDIG_PLLCR = 0; // PLLPD = 0

// 6b. Wait at least 5 µs before checking LOCK
// (hardware requirement before LOCK can be trusted)

// 6c. Poll PLLSR[LOCK] bit 2
while (!(PLLDIG_PLLSR & (1u << 2)));  // Wait LOCK = 1

// 6d. Enable PLL output dividers (set DE bit in each PLLODIV)
PLLDIG_PLLODIV0 |= (1u << 31); // Enable PHI0 output
PLLDIG_PLLODIV1 |= (1u << 31); // Enable PHI1 output
```

### Step 7: Switch System Clock to PLL_PHI0

Use the hardware-controlled MUX_0 clock switch protocol (see Section 7 for full detail).

```c
// Perform PCFS clock switch FIRC → PLL_PHI0 via MUX_0_CSC
// SELCTL = 1000b = 8 = PLL_PHI0, CLK_SW = 1, RAMPUP + RAMPDOWN bits
// (Full PCFS switch — see Section 7 for complete code)
MUX_0_CSC = (0x08u << 24)    // SELCTL = PLL_PHI0
           | (1u << 6)        // RAMPUP = 1
           | (1u << 5)        // RAMPDOWN = 1
           | (1u << 2);       // CLK_SW = 1

// Poll MUX_0_CSS[SWIP] until 0 (switch complete)
while (MUX_0_CSS & (1u << 16)); // Wait SWIP = 0

// Verify SELSTAT = 1000b
uint32_t css = MUX_0_CSS;
if (((css >> 24) & 0x1F) != 0x08) { /* handle error */ }
```

### Step 8: Enable Clock Monitors

After settling on PLL, enable CMU monitoring.

```c
// CMU base addresses (from RM §23.8, Table 133):
// CMU_FC_0: monitors FXOSC_CLK (ref=FIRC)
// CMU_FC_3: monitors CORE_CLK (ref=FXOSC) — destructive reset on FLL/FHH
// CMU_FC_4: monitors AIPS_PLAT_CLK (ref=FIRC) — destructive reset
// CMU_FC_5: monitors HSE_CLK (ref=FIRC) — destructive reset

// Enable CMU_FC_3 (CORE_CLK monitor)
// CMU_FC_3.GCR[FCE] = 1 enables frequency check
// Configure CMU_FC_3.HFREF (high ref) and LFREF (low ref) with
// expected CORE_CLK vs FXOSC reference counts
// Refer to CMU_FC chapter for full configuration details

// Note: CMU must be disabled before changing clock sources
// and before disabling the monitored clock source
// CMUs should be turned ON only after device has moved to PLL source
// (with LMR active) — see RM §23.8 Note
```

### Step 9: Enable Peripheral Clocks via MC_ME

Peripheral clock gating is controlled by MC_ME partition clock enable registers (PRTN_COFB_CLKEN). Unlike the MCXE247's PCC, the MCXE31B uses a partition-based system.

```c
// Example: Enable LPUART0 clock (AIPS_PLAT_CLK domain)
// MC_ME.PRTN0_COFB0_CLKEN[REQ8] = 1 for LPUART0
// Then write MC_ME.PRTN0_PCONF[PCE] and MC_ME.PRTN0_PUPD[PCUD]
// to trigger partition update (see MC_ME chapter for full flow)
```

---

## 7. Clock Switching Protocol

### Hardware-Controlled Multiplexer (MUX_0, MUX_3, MUX_4, etc.)

From RM §24.4.1.1, the sequence for switching a hardware-controlled mux (e.g., MUX_0) is:

```
1. Read MUX_n_CSS
2. Check MUX_n_CSS[SWIP] — if 1, wait until it clears
3. Configure MUX_n_CSC (SELCTL + CLK_SW=1, optionally RAMPUP/RAMPDOWN for PCFS)
4. Auto-clear: CLK_SW bit self-clears after the switch begins
5. Check MUX_n_CSS[SWTRG] for switch outcome
```

**SWTRG field meaning:**

| SWTRG value | Meaning |
|---|---|
| 001b | Switch request succeeded |
| 010b | Failed (target clock inactive), fell back to FIRC |
| 011b | Failed (current clock inactive), fell back to FIRC |
| 100b | Safe clock switch to FIRC succeeded |
| 101b | Safe clock switch to FIRC succeeded, prior clock was inactive |

> **CRITICAL:** A new clock switch request can only be given **3 clock cycles after** the previous request completes (RM §24.5, MUX_n_CSS SWIP description).

### Software-Controlled Multiplexer (MUX_5 CLKOUT_STANDBY, MUX_6 CLKOUT_RUN, MUX_11 TRACE)

From RM §24.4.1.2:

```
1. Write MUX_n_CSC[CG] = 1 (graceful clock gate)
2. If current source is active, wait MUX_n_CSS[CS] = 0
   Else write MUX_n_CSC[FCG] = 1 (forced gate)
3. Write SELCTL to select new source
4. Write MUX_n_CSC[FCG] = 0 (if it was set)
5. Write MUX_n_CSC[CG] = 0 (ungate)
6. Poll MUX_n_CSS[CS] until 1 (source active)
```

> Software-controlled muxes have no switch to safe clock support. No RAMPUP/RAMPDOWN available.

### PLL → FIRC Switch Sequence (RM §23.7.7)

When switching from PLL **to** FIRC (lower-priority direction):

```
1. Disable communication modules (CAN, SPI, etc.) before switching
2. Clock-gate all peripherals via MC_ME
3. Clock-gate all cores except the controlling core
4. Switch system clock: MUX_0_CSC[SELCTL] = 0000b (FIRC), CLK_SW = 1
5. Update dividers as needed
6. Configure MUX_0_DIV_TRIG_CTRL (HHEN=1, TCTL=1)
7. Trigger: write MUX_0_DIV_TRIG
```

### FIRC → PLL Switch Sequence (RM §23.7.7)

```
1. Disable communication modules
2. Clock-gate all peripherals via MC_ME
3. Clock-gate all cores except controlling core
4. Update dividers first: MUX_0_DIV_TRIG_CTRL → MUX_0_DIV_TRIG → poll UPD_STAT
5. Switch: MUX_0_CSC[SELCTL] = 1000b (PLL_PHI0), CLK_SW=1 (with PCFS)
6. Poll MUX_0_CSS[SWIP] until 0
```

> **IMPORTANT:** When enabling PLL, **PMC last-mile regulator must be enabled first** (`PMC_CONFIG[LMEN]`). Disable last-mile regulator only after PLL is fully disabled.

---

## 8. Clock Divider Update Procedure

From RM §24.4.3.2, using the **Trigger Update** method:

```
1. Configure MUX_0_DIV_TRIG_CTRL:
   - TCTL = 1: Use trigger update mode
   - HHEN = 1: Enable halt handshake with AXBS crossbar
   NOTE: HHEN must only be set when TCTL = 1

2. Poll MUX_0_DIV_UPD_STAT until = 0 (no pending update)

3. Update divider registers (MUX_0_DC_x) — these are stored but not applied yet

4. Write to MUX_0_DIV_TRIG — triggers the update process:
   - Halt handshake is sent to AXBS
   - Dividers update only after AXBS acknowledges halt
   - MUX_0_DIV_UPD_STAT is asserted to 1 during update

5. Poll MUX_0_DIV_UPD_STAT until = 0 (update complete)
```

**AXBS halt order** (RM §23.7.6): The halt handshake disables crossbar gaskets in this order:
1. Core gaskets (ELE_HSEB gaskets)
2. Crossbar switch (AXBS)
3. Flash AXBS bridge
4. PRAM/SRAM gasket

> **WARNING:** Multiple writes to dividers without waiting for UPD_STAT to clear leads to **misaligned dividers** — clocks will be out of phase.

> For aligned dividers: LCM of all division values in a single mux must be ≤ 100.

---

## 9. PCFS — Progressive Clock Frequency Switching

### What PCFS Does

PCFS smoothly ramps the clock frequency up or down during a source switch at MUX_0 (the main system clock mux). This prevents sudden power supply transients when large blocks of logic change frequency simultaneously.

### When PCFS is Used

- PCFS is only available on **hardware-controlled multiplexers** (not software-controlled).
- PCFS is triggered when switching to/from a clock source whose frequency is higher than FIRC_CLK.
- A switch to safe clock (FIRC) always performs ramp-down (except when there's an ongoing switch without PCFS).

### PCFS Configuration Registers (MC_CGM base + 0x00 area)

| Register | Offset | Description |
|---|---|---|
| PCFS_SDUR | 0x00 | Step duration in FIRC cycles — how long each PCFS step lasts |
| PCFS_DIVC8 | 0x58 | Divider Change register for clock index 8 (PLL_PHI0) |
| PCFS_DIVE8 | 0x5C | Divider End value (target divider = (DIVE+1)/1000) |
| PCFS_DIVS8 | 0x60 | Divider Start value (starting divider for ramp) |

> **IMPORTANT:** All PCFS registers must be programmed **before any MC_CGM operation**, using FIRC as the configuration clock, and must not be changed thereafter.

### PCFS Calculation (RM §24.4.2, Table 134)

```
amax = fchg / Fi    (where Fi = target clock frequency)

RATE selection from table:
  amax = 0.005 → RATE = 12
  amax = 0.01  → RATE = 48
  amax = 0.15  → RATE = 112
  amax = 0.20  → RATE = 184

k = ceil(0.5 + sqrt(0.25 - (2000 × (1 - (Fi/fsafe)) / RATE)))

PCFS register values:
  PCFS_DIVE[DIV]  = (Fi/fsafe) × 1000 - 1
  PCFS_DIVC[INIT] = RATE × k
  PCFS_DIVC[RATE] = RATE
  PCFS_DIVS[DIV]  = 999 + (RATE × k × (k+1) / 2)
```

Where `fsafe` = FIRC frequency (the "safe" frequency baseline).

> **Note:** If Fi (target frequency) < FIRC, set PCFS registers to default values (divide-by-1 throughout — no actual progressive switching needed).

### Valid PCFS Commands (RM §24.4.2.1, Table 135)

| PCFS State | Command |
|---|---|
| Idle | Ramp-down + clock switch + ramp-up (atomic write) |
| Idle | Clock switch only (no ramp) |
| Any | Switch to safe clock (always completes) |

> All PCFS commands must be **atomic** — a single register write initiates the complete ramp-down / switch / ramp-up sequence.

### Clock Switch with Load Change (RM §24.4.2.4)

When enabling/disabling many peripherals simultaneously, switch frequency first to FIRC to avoid power supply stress:

```
1. Select FIRC at MUX_0 with PCFS (ramp down)
2. Wait PCFS complete
3. Enable/disable peripherals (load change)
4. Select target PLL clock with PCFS (ramp up)
5. Wait PCFS complete
```

---

## 10. Clock Monitor Safety (CMU_FC / CMU_FM)

### CMU Instances Summary (RM §23.8, Table 133)

| CMU | Reference Clock | Monitored Clock | Failure Reaction |
|---|---|---|---|
| CMU_FC_0 | FIRC_CLK | FXOSC_CLK | Destructive reset or interrupt |
| CMU_FC_3 | FXOSC_CLK | CORE_CLK | Destructive reset only |
| CMU_FC_4 | FIRC_CLK | AIPS_PLAT_CLK | Destructive reset only |
| CMU_FC_5 | FIRC_CLK | HSE_CLK | Destructive reset only |
| CMU_FM_1 | FXOSC_CLK | FIRC_CLK | Interrupt (software check) |
| CMU_FM_2 | FXOSC_CLK | SIRC_CLK | Interrupt (software check) |

### CMU_FC Operation

CMU_FC performs **precision over and under frequency checking** (FHH = Frequency Higher than High, FLL = Frequency Lower than Low). When either condition is detected, it triggers a destructive reset (or interrupt for CMU_FC_0).

**Key registers per CMU_FC instance:**
- `GCR[FCE]` — Frequency Check Enable
- `IER[FHHAIE]` / `IER[FLLAIE]` — Interrupt enable for FHH/FLL above threshold
- `SR[FHH]` / `SR[FLL]` — Status flags

### CMU_FM Operation

CMU_FM performs **periodic frequency metering** — software triggers a metering window and reads the count.

- `CMU_FM_1`: Software must periodically check FIRC_CLK counts vs FXOSC reference. If FIRC fails, `SR[FMTO]` (timeout) is set.
- `CMU_FM_2`: Same for SIRC_CLK.
- Software must service CMU_FM_1 interrupts within the POR_WDG timeout; otherwise POR_WDG treats it as a critical FIRC failure.

**FIRC failure recovery cases (RM §23.4.4.1):**

| Case | Detection | Recovery |
|---|---|---|
| FIRC not system clock, goes out of range | CMU_FM_1 interval measurement | SBC power cycle or SW functional reset |
| FIRC not system clock, stuck | CMU_FM_1.SR[FMTO] = 1 | SBC power cycle |
| FIRC is system clock, fails | CMU_FC_3/4/5 assert FLL → destructive reset | System resets, FIRC re-initialized |

### Critical CMU Rules from RM §23.8 Notes

1. **Disable CMU** corresponding to system clocks before changing the system clock source or divider.
2. **Disable CMU monitoring a clock source** before disabling that clock source; re-enable CMU after the clock source is re-enabled.
3. **Turn CMUs ON only after device has moved to PLL source** (with last-mile regulator active).
4. CMU_FC_x **must be disabled** when FIRC_CLK < 24 MHz (Options E and E2) to avoid false FHH events.

### Runtime Safety Check Example (IEC 60730 / ISO 26262)

```c
void Clock_RuntimeCheck(void) {
    // Check CORE_CLK is still correct
    // Read CMU_FC_3.SR and verify no FHH or FLL flags
    
    // Check FIRC frequency using CMU_FM_1
    // Trigger metering window
    // Read CMU_FM_1.SR[MET_CNT] — compare with expected count
    // If out of range, invoke recovery
    
    // Check SIRC frequency using CMU_FM_2
    // Read CMU_FM_2.SR[MET_CNT] — compare with expected
    
    // Check FXOSC alive via CMU_FC_0
    // SR[FHH] and SR[FLL] should both be 0
    
    // This function must be called within FTTI
    // (Fault Tolerance Time Interval per Safety Manual)
}
```

---

## 11. MC_CGM Register Reference

**MC_CGM Base Address:** 402D_8000h  
All registers are 32-bit wide. Only 32-bit read/write accesses are supported; sub-32-bit accesses return a bus error.

### MUX_0 (Main System Clock Mux) — Complete Register Map

| Offset | Register | Access | Reset | Description |
|---|---|---|---|---|
| 0x300 | MUX_0_CSC | RW | 0000_0000h | Clock Source Control — write SELCTL + CLK_SW |
| 0x304 | MUX_0_CSS | R | 0008_0000h | Clock Source Status — read SELSTAT, SWIP, SWTRG |
| 0x308 | MUX_0_DC_0 | RW | 8000_0000h | Divider 0 → CORE_CLK (DE=1 at reset) |
| 0x30C | MUX_0_DC_1 | RW | 8000_0000h | Divider 1 → AIPS_PLAT_CLK (DE=1 at reset) |
| 0x310 | MUX_0_DC_2 | RW | 8001_0000h | Divider 2 → AIPS_SLOW_CLK (÷2 at reset) |
| 0x314 | MUX_0_DC_3 | RW | 8000_0000h | Divider 3 → HSE_CLK |
| 0x318 | MUX_0_DC_4 | RW | 8000_0000h | Divider 4 → DCM_CLK |
| 0x31C | MUX_0_DC_5 | RW | 8003_0000h | Divider 5 → LBIST_CLK (÷4 at reset) |
| 0x320 | MUX_0_DC_6 | RW | 8000_0000h | Divider 6 → QSPI_MEM_CLK |
| 0x334 | MUX_0_DIV_TRIG_CTRL | RW | 0000_0000h | HHEN[31], TCTL[0] trigger config |
| 0x338 | MUX_0_DIV_TRIG | RW | 0000_0000h | Write any value to trigger divider update |
| 0x33C | MUX_0_DIV_UPD_STAT | R | 0000_0000h | Bit 0: 1 = update pending |

### Other Mux Registers

| Mux | CSC Offset | CSS Offset | DC_0 Offset | Output |
|---|---|---|---|---|
| MUX_1 | 0x340 | 0x344 | 0x348 | STM0_CLK |
| MUX_2 | 0x380 | 0x384 | 0x388 | (internal) |
| MUX_3 | 0x3C0 | 0x3C4 | 0x3C8 | FLEXCAN[0:2]_PE_CLK |
| MUX_4 | 0x400 | 0x404 | 0x408 | FLEXCAN[3:5]_PE_CLK |
| MUX_5 | 0x440 | 0x444 | 0x448 | CLKOUT_STANDBY |
| MUX_6 | 0x480 | 0x484 | 0x488 | CLKOUT_RUN |
| MUX_7 | 0x4C0 | 0x4C4 | 0x4C8 | EMAC_RX_CLK |
| MUX_8 | 0x500 | 0x504 | 0x508 | EMAC_TX_CLK |
| MUX_9 | 0x540 | 0x544 | 0x548 | EMAC_TS_CLK |
| MUX_10 | 0x580 | 0x584 | 0x588 | QSPI_SFCK |
| MUX_11 | 0x5C0 | 0x5C4 | 0x5C8 | TRACE_CLK |

### MUX_n_CSC Key Bit Fields

| Bits | Field | Description |
|---|---|---|
| [28:24] | SELCTL | Clock source index (see Table 97 source mapping) |
| [7] | RAMPUP | Enable PCFS ramp-up on switch |
| [6] | RAMPDOWN | Enable PCFS ramp-down on switch |
| [3] | SAFE_SW | Write 1 to switch to FIRC (safe clock) immediately |
| [2] | CLK_SW | Write 1 to initiate clock switch to SELCTL source |

### MUX_n_CSS Key Bit Fields

| Bits | Field | Description |
|---|---|---|
| [28:24] | SELSTAT | Currently active clock source index |
| [19:17] | SWTRG | Switch trigger cause (see Section 7) |
| [16] | SWIP | Switch in progress: 0 = complete, 1 = in progress |
| [3] | SAFE_SW | Safe clock switch was requested |
| [2] | CLK_SW | Clock switch was requested |

### MUX_n_DC_x Key Bit Fields

| Bits | Field | Description |
|---|---|---|
| [31] | DE | Divider Enable: 0 = disabled (output forced 0); 1 = enabled |
| [21:16] | DIV | Division value: output = input / (DIV+1) |

### Clock Source Index Table (from RM §23.5.4, Table 97)

| Index (SELCTL) | Clock Source |
|---|---|
| 0x00 (0000b) | FIRC_CLK (default, safe clock) |
| 0x01 (0001b) | SIRC_CLK |
| 0x02 (0010b) | FXOSC_CLK |
| 0x04 (0100b) | SXOSC_CLK |
| 0x08 (1000b) | PLL_PHI0_CLK |
| 0x09 (1001b) | PLL_PHI1_CLK |
| 0x0C (1100b) | PLL_AUX_PHI0_CLK |
| 0x0D (1101b) | PLL_AUX_PHI1_CLK |
| 0x10 (10000b) | CORE_CLK |
| 0x13 (10011b) | HSE_CLK |
| 0x16 (10110b) | AIPS_PLAT_CLK |
| 0x17 (10111b) | AIPS_SLOW_CLK |
| 0x18 (11000b) | EMAC_RMII_TX_CLK |
| 0x19 (11001b) | EMAC_RX_CLK |

> Writing a reserved SELCTL value results in a **bus transfer error** (RM §24.4.1.1 note).

---

## 12. PLLDIG Register Reference

**PLLDIG Base Address:** 402E_0000h

| Offset | Register | Access | Reset | Description |
|---|---|---|---|---|
| 0x00 | PLLCR | RW | 8000_0000h | Bit 31 PLLPD: 0=power up, 1=power down (default) |
| 0x04 | PLLSR | RW | 0000_0300h | Bit 2 LOCK: 1=PLL locked |
| 0x08 | PLLDV | RW | 0C3F_1032h | VCO multiplier and dividers |
| 0x0C | PLLFM | RW | 4000_0000h | Frequency modulation config |
| 0x10 | PLLFD | RW | 0000_0000h | Fractional divider |
| 0x18 | PLLCAL2 | RW | 0006_0000h | Calibration register 2 |
| 0x80 | PLLODIV_0 | RW | 0000_0000h | PHI0 output divider |
| 0x84 | PLLODIV_1 | RW | 0000_0000h | PHI1 output divider |

### PLLCR Register

| Bit | Field | Description |
|---|---|---|
| 31 | PLLPD | PLL Power Down: 0 = powered up; 1 = powered down (default) |

### PLLSR Register

| Bit | Field | Description |
|---|---|---|
| 2 | LOCK | PLL Lock: 1 = PLL has achieved frequency lock |

### PLLDV Register (key fields)

| Bits | Field | Description |
|---|---|---|
| [31:24] | ODIV2 | VCO pre-divider: PLLODIV2_CLK = VCO / (ODIV2 × 2) |
| [14:12] | RDIV | Reference clock divider (applied before MFI multiplier) |
| [7:0] | MFI | Integer multiplication factor for VCO |

### PLLODIV_0 / PLLODIV_1 Registers

| Bits | Field | Description |
|---|---|---|
| [31] | DE | Divider Enable: 1 = PHI output active; 0 = disabled |
| [20:16] | DIV | PHI output divider: PHI_CLK = PLLODIV2_CLK / (DIV+1) |

> **CRITICAL:** These registers support **word accesses only** (32-bit). Reserved fields must retain their default values when writing.

---

## 13. PLL Calculation and Formulas

### VCO Frequency (Integer Mode, RDIV ≠ 0)

```
VCO_CLK = (FXOSC_CLK / RDIV) × MFI
```

### VCO Frequency (Integer Mode, RDIV = 0)

```
VCO_CLK = FXOSC_CLK × 2 × MFI
```

### PHI Output Frequency

```
PLLODIV2_CLK = VCO_CLK / (ODIV2 × 2)
PLL_PHI0_CLK = PLLODIV2_CLK / (PLLODIV_0[DIV] + 1)
PLL_PHI1_CLK = PLLODIV2_CLK / (PLLODIV_1[DIV] + 1)
```

### Worked Example — Option A (960 MHz VCO → 160 MHz PHI0)

```
Reference: FXOSC_CLK = 8 MHz
Target: CORE_CLK = 160 MHz

Step 1: Choose VCO = 960 MHz (within VCO limits)
  RDIV = 1, MFI = 120
  VCO = (8 / 1) × 120 = 960 MHz ✓

Step 2: Pre-divide VCO
  ODIV2 = 2 → PLLODIV2_CLK = 960 / (2 × 2) = 240 MHz

Step 3: PHI0 divider for 160 MHz
  Need 240 / (DIV+1) = 160 → DIV+1 = 1.5 ← not integer!
  
  Recalculate: Try ODIV2 = 2, PLLODIV2_CLK = 960 / (2×2) = 240
  No... RM shows Option A:
  PLLDV[ODIV2] = 02h → PLLODIV2_CLK = 960 / (2+1?) 
  
  Actually from RM Table 124: PLLDV[ODIV2]=02h gives 480 MHz
  So: PLLODIV2_CLK = VCO / ODIV2 = 960 / 2 = 480 MHz (ODIV2 divides by its value directly)
  
  PHI0: 480 / (PLLODIV_0[DIV]+1) = 160 → DIV+1 = 3 → DIV = 2 (0010b) ✓
  PHI1: 480 / (PLLODIV_1[DIV]+1) = 240 → DIV+1 = 2 → DIV = 1 (0001b) ✓

Register values:
  PLLDV = (0x02 << 24) | (1 << 12) | 120  = 0x0201_0078
  PLLODIV_0 = (1 << 31) | (2 << 16)        = 0x8002_0000
  PLLODIV_1 = (1 << 31) | (1 << 16)        = 0x8001_0000
```

### Worked Example — Option B (960 MHz VCO → 120 MHz PHI0)

```
Reference: FXOSC_CLK = 8 MHz
RDIV = 1, MFI = 120 → VCO = 960 MHz
ODIV2 = 2 → PLLODIV2_CLK = 480 MHz
PHI0 (120 MHz): 480 / (DIV+1) = 120 → DIV+1 = 4 → DIV = 3 (011b)
PHI1 (240 MHz): 480 / (1+1) = 240 MHz → DIV = 1 (0001b)

Register:
  PLLODIV_0 = (1<<31) | (3<<16) = 0x8003_0000
```

---

## 14. Peripheral Clock Sources Summary

From RM §23.6.1, all peripheral clock sources for MCXE31B:

### Communication Modules

| Peripheral | MODULE_CLK | REG_INTF_CLK |
|---|---|---|
| LPUART_0, LPUART_8 | AIPS_PLAT_CLK | AIPS_PLAT_CLK |
| LPUART_1:7, 9:15 | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| LPSPI0 | AIPS_PLAT_CLK | AIPS_PLAT_CLK |
| LPSPI1–5 | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| LPI2C_n | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| FlexCAN_n | AIPS_PLAT_CLK + FLEXCAN_n_PE_CLK | AIPS_PLAT_CLK |
| FlexIO | AIPS_PLAT_CLK (REG) + CORE_CLK (flexio_clk) | AIPS_PLAT_CLK |
| SAI_n | AIPS_SLOW_CLK (REG) + external BCLK/MCLK | AIPS_SLOW_CLK |
| EMAC | AIPS_PLAT_CLK (CSR) + RX/TX/TS clocks | AIPS_PLAT_CLK |
| QuadSPI | AIPS_PLAT_CLK (REG) + QSPI_SFCK + QSPI_MEM_CLK | AIPS_PLAT_CLK |

### System Modules

| Module | MODULE_CLK | REG_INTF_CLK |
|---|---|---|
| MSCM | AIPS_PLAT_CLK | AIPS_PLAT_CLK |
| MCM | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| SIUL2 | AIPS_SLOW_CLK + FIRC_CLK (filter) | AIPS_SLOW_CLK |
| AXBS | CORE_CLK | AIPS_PLAT_CLK |
| eDMA | CORE_CLK | AIPS_PLAT_CLK |

### Safety and Power Modules

| Module | MODULE_CLK | REG_INTF_CLK |
|---|---|---|
| CMU_FC/FM | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| FCCU | AIPS_PLAT_CLK + FIRC_CLK (safe) | AIPS_PLAT_CLK |
| MC_RGM | FIRC_CLK | FIRC_CLK |
| PMC | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| MC_ME | AIPS_SLOW_CLK | AIPS_SLOW_CLK |
| ELE_HSEB | HSE_CLK (PLL module) | AIPS_SLOW_CLK |
| DCM | DCM_CLK | DCM_CLK |

### Memory Modules

| Module | MODULE_CLK | REG_INTF_CLK |
|---|---|---|
| PFLASH / Flash | CORE_CLK | AIPS_SLOW_CLK |
| PRAM / SRAM | CORE_CLK | AIPS_SLOW_CLK |

### Motor Control and Timer Modules

| Module | MODULE_CLK |
|---|---|
| ADC_n | CORE_CLK → prescaler (÷1/2/4/8) → AD_CLK ≤ 80 MHz |
| eMIOS_n | CORE_CLK |
| BCTU | CORE_CLK |
| LCU | CORE_CLK |
| PIT_0, PIT_1, PIT_2 | AIPS_SLOW_CLK |
| SWT_n | AIPS_SLOW_CLK + SIRC_CLK (counter) |
| STM0 | STM0_CLK (from MUX_1) |
| RTC | AIPS_SLOW_CLK + selectable: SIRC/FIRC/FXOSC/SXOSC |

### FlexCAN Clocking Detail

FlexCAN uses two separate clocks:
- `CAN_CHI_CLK` — Host Interface (from AIPS_PLAT_CLK)
- `CAN_PE_CLK` — Protocol Engine (from FLEXCAN_n_PE_CLK via MUX_3 or MUX_4)

Maximum CAN data rate: 8 Mbps with 40 MHz CAN_PE_CLK. With 16 MHz crystal reference: 3.2 Mbps.

---

## 15. CLKOUT Debug Feature

### CLKOUT_RUN (MUX_6)

Available only in Run mode (not in Standby mode).

**Output pins (from RM §23.3.3.1, Table 90):**

| Port | MSCR# | OBE | IBE | SSS |
|---|---|---|---|---|
| PTB5 | 37 | 1 | 0 | 0101b |
| PTD10 | 106 | 1 | 0 | 0110b |
| PTD14 | 110 | 1 | 0 | 0111b |

**Available sources (MCXE31B, MUX_6_CSC[SELCTL]):**

| SELCTL | Source |
|---|---|
| 0x00 | FIRC_CLK |
| 0x01 | SIRC_CLK |
| 0x02 | FXOSC_CLK |
| 0x04 | SXOSC_CLK |
| 0x08 | PLL_PHI0_CLK |
| 0x09 | PLL_PHI1_CLK |
| 0x10 | CORE_CLK |
| 0x13 | HSE_CLK |
| 0x16 | AIPS_PLAT_CLK |
| 0x17 | AIPS_SLOW_CLK |
| 0x18 | EMAC_RMII_TX_CLK |
| 0x19 | EMAC_RX_CLK |

**MUX_6_DC_0[DIV]:** Divides output from 1 to 64 before routing to pin.

### CLKOUT_STANDBY (MUX_5)

Available in both Run and Standby modes.

**Output pins (RM §23.3.3.2, Table 91):**

| Port | MSCR# | OBE | IBE | SSS |
|---|---|---|---|---|
| PTA12 | 12 | 1 | 0 | 0011b |
| PTE10 | 138 | 1 | 0 | 0101b |

**Available sources (MCXE31B):** FIRC_CLK, SIRC_CLK, FXOSC_CLK, SXOSC_CLK, AIPS_SLOW_CLK

> **IMPORTANT (RM §23.4.9):** CLKOUT_STANDBY registers are latched at Standby mode entry and reset in Standby sequence. They must be **reconfigured on Standby mode exit**. CLKOUT across functional reset and Standby is only supported on GPIO[12] with OBE controlled by `DCMRWP1[3]`.

---

## 16. External Oscillator Configuration

### FXOSC Initialization Sequence (from RM §27.4)

```
1. Ensure FXOSC is disabled (OSCON = 0)
2. Write desired OSC_BYP and COMP_EN to select mode:
   - Crystal mode:  OSC_BYP=0, COMP_EN=1
   - Bypass mode:   OSC_BYP=1, COMP_EN=0
3. Configure GM_SEL (transconductance for crystal, 0000b for bypass)
   NOTE: In Crystal mode, GM_SEL MUST be non-zero or FXOSC will not function
4. Set EOCV for appropriate stabilization time:
   EOCV = stabilization_time_ns / (4 × 128 × crystal_period_ns)
5. Write 1 to OSCON to enable
6. Poll STAT[OSC_STAT] until 1 (stable)

To disable FXOSC:
1. Write OSCON = 0
2. Wait ≥ 4 crystal clock cycles (Power-Down mode entry)
3. Wait ≥ 2 µs before re-enabling
4. Do not modify other FXOSC registers for ≥ 16 FXOSC_CLK cycles after disable
```

### SXOSC Initialization Sequence (from RM §28.5)

```
1. Write EOCV to SXOSC_CTRL (EOCV = stabilization_ns / (4 × 128 × period_ns))
2. Write 1 to SXOSC_CTRL[OSCON]
3. Poll SXOSC_STAT[OSC_STAT] until 1
```

> **Note (SXOSC):** Functional reset does not reset SXOSC. Only destructive reset disables it. This is intentional for RTC continuity across application resets.

---

## 17. Common Mistakes and Gotchas

| # | Mistake | Consequence | Correct Action |
|---|---|---|---|
| 1 | Not enabling PMC last-mile regulator before PLL | PLL will not lock correctly, unpredictable VCO output | Enable PMC.CONFIG[LMEN] before writing PLLCR[PLLPD]=0 |
| 2 | Polling PLLSR[LOCK] too soon after PLLPD=0 | False lock indication; RM requires 5 µs wait | Wait ≥ 5 µs after writing PLLPD=0 before polling LOCK |
| 3 | Forgetting to disable PLLODIV_n[DE] before powering down PLL | Residual output from dividers | Write DE=0 for all PLLODIV_n before writing PLLCR[PLLPD]=1 |
| 4 | Writing sub-32-bit accesses to MC_CGM or PLLDIG registers | Bus transfer error | Always use 32-bit (word) access to these registers |
| 5 | Writing PLLODIV_0 without preserving reserved fields | Undefined behavior | Read-modify-write, or use exact documented field values |
| 6 | Setting CORE_CLK < AIPS_PLAT_CLK | Undefined behavior (RM §23.7.1) | Always ensure MUX_0_DC_0 divider ≤ MUX_0_DC_1 divider |
| 7 | Setting HSE_CLK > 80 MHz when CORE_CLK = 160 MHz | HSE/ELE_HSEB instability | At 160 MHz CORE_CLK, HSE_CLK must be ≤ 80 MHz (÷2) |
| 8 | Not disabling CMU before changing system clock | False CMU FHH/FLL events causing spurious destructive reset | Disable CMU before any clock source/divider change |
| 9 | Not waiting MUX_0_DIV_UPD_STAT = 0 between divider updates | Misaligned dividers — clocks out of phase | Always poll UPD_STAT = 0 between successive divider writes |
| 10 | Setting HHEN=1 without TCTL=1 in MUX_0_DIV_TRIG_CTRL | Divider misalignment (RM §24.4.3.2 note) | Always set HHEN only when TCTL = 1 |
| 11 | Not disabling communication modules before clock switch | Corrupted CAN/SPI/UART frames during transition | Disable all comm peripherals before switching MUX_0 source |
| 12 | Disabling FXOSC before disabling PLL | PLL loses reference clock — unpredictable lock behavior | Disable PLL (PLLPD=1) → then disable FXOSC |
| 13 | Using bypass (OSC_BYP=1) but not driving EXTAL low when FXOSC off | MCXE31B erroneous behavior (RM §27.1.1) | Drive EXTAL pin low whenever FXOSC is disabled in bypass mode |
| 14 | Enabling GM_SEL=0000b in crystal mode | FXOSC will not function at all (RM §27.4 note) | Always use non-zero GM_SEL in crystal mode |
| 15 | Using FIRC_CLK < 24 MHz with CMU_FC enabled | Erroneous FHH events — may cause spurious destructive reset | Disable CMU_FC_x before using Options E or E2 |
| 16 | Writing SXOSC registers during operation without disabling | Undefined behavior per RM | Disable SXOSC first (OSCON=0), wait ≥16 cycles, then modify |
| 17 | Not configuring PCFS before any MC_CGM operation | PCFS will use default (divide-by-1) — no smooth ramp | Configure PCFS_SDUR, PCFS_DIVC/DIVE/DIVS before first switch |
| 18 | Making non-atomic PCFS command writes | Partial ramp sequences execute | Use a single write to MUX_0_CSC with all PCFS bits set atomically |
| 19 | Requesting clock switch while SWIP=1 | New request ignored or corrupted switch | Always poll CSS[SWIP]=0 before any new switch request |
| 20 | Writing reserved SELCTL value to MUX_n_CSC | Bus transfer error returned | Only use documented clock source indices from Table 97 |
| 21 | Not reconfiguring CLKOUT_STANDBY after Standby exit | CLKOUT_STANDBY stops working (latched and reset during Standby) | Re-initialize SIUL2 MSCR and MUX_5_CSC after every Standby exit |
| 22 | Enabling CMU monitors before device moves to PLL | Erroneous CMU events while clock settling | Turn CMUs ON only after clock transition to PLL (with LMR active) |
| 23 | LCM of MUX_0 dividers > 100 | Dividers cannot be atomically aligned | Design divider ratios such that LCM of all MUX_0_DC_x values is ≤ 100 |

---

## 18. Safe Production Initialization Checklist

The following 20-step checklist is the recommended production initialization sequence for MCXE31B targeting Option A (160 MHz) from RM §23.7.2 requirements.

```
Step 1:  ☐ Configure PRAM/SRAM gaskets and wait states for target operating mode
            (See RM §23.7.3, Table 131 for your option)

Step 2:  ☐ Enable PMC last-mile regulator: PMC.CONFIG[LMEN]=1
            (Required before PLL — disable only after PLL is fully disabled)

Step 3:  ☐ Configure FXOSC (while OSCON=0):
            - Set OSC_BYP, COMP_EN for crystal or bypass mode
            - Set GM_SEL (non-zero for crystal mode)
            - Calculate and set EOCV for crystal startup time

Step 4:  ☐ Enable FXOSC: FXOSC.CTRL[OSCON] = 1

Step 5:  ☐ Poll FXOSC.STAT[OSC_STAT] = 1 (stable)

Step 6:  ☐ (Optional) Enable SXOSC for RTC applications if needed

Step 7:  ☐ Confirm PLLDIG_PLLCR[PLLPD] = 1 (PLL powered down, should be reset default)

Step 8:  ☐ Confirm PLLDIG_PLLODIV_0[DE] = 0 and PLLDIG_PLLODIV_1[DE] = 0

Step 9:  ☐ Configure PLL:
            - Write PLLDV: ODIV2 = 02h, RDIV = 1, MFI = 120 (for 8 MHz → 960 MHz VCO)
            - Write PLLODIV_0: DIV = 2 (for 160 MHz PHI0), DE = 0 still
            - Write PLLODIV_1: DIV = 1 (for 240 MHz PHI1), DE = 0 still

Step 10: ☐ Configure PCFS registers in MC_CGM (before any mux operation):
            - PCFS_SDUR: step duration in FIRC cycles
            - PCFS_DIVC8, PCFS_DIVE8, PCFS_DIVS8: for PLL_PHI0 ramp

Step 11: ☐ Enable PLL: write 0 to PLLCR[PLLPD]

Step 12: ☐ Wait ≥ 5 µs (hardware settling time before LOCK is valid)

Step 13: ☐ Poll PLLSR[LOCK] = 1 (PLL locked)

Step 14: ☐ Enable PLL output dividers:
            - PLLODIV_0: set DE = 1 (preserve DIV field)
            - PLLODIV_1: set DE = 1 (preserve DIV field)

Step 15: ☐ Configure MC_CGM dividers for target option:
            - Write MUX_0_DC_0 through MUX_0_DC_6 with DE=1 and desired DIV values
            - Configure MUX_0_DIV_TRIG_CTRL: HHEN=1, TCTL=1
            - Write MUX_0_DIV_TRIG to trigger update
            - Poll MUX_0_DIV_UPD_STAT = 0 (update complete)

Step 16: ☐ Disable all communication modules and peripherals (MC_ME clock gating)

Step 17: ☐ Switch system clock to PLL_PHI0 via MUX_0:
            - Write MUX_0_CSC: SELCTL=1000b (PLL_PHI0), RAMPDOWN=1, RAMPUP=1, CLK_SW=1
            - Poll MUX_0_CSS[SWIP] = 0
            - Verify MUX_0_CSS[SELSTAT] = 1000b
            - Verify MUX_0_CSS[SWTRG] = 001b (success)

Step 18: ☐ Enable peripheral clocks via MC_ME partition clock enable registers

Step 19: ☐ Enable clock monitors (after PLL settled):
            - Configure CMU_FC_3 (CORE_CLK monitor, ref = FXOSC)
            - Configure CMU_FC_4 (AIPS_PLAT_CLK, ref = FIRC)
            - Configure CMU_FC_5 (HSE_CLK, ref = FIRC)
            - Configure CMU_FM_1 (FIRC meter, ref = FXOSC)
            - Configure CMU_FM_2 (SIRC meter, ref = FXOSC)
            - Enable periodic CMU_FM software checks within FTTI

Step 20: ☐ Verify final clock state:
            - Read MUX_0_CSS: SELSTAT = 1000b (PLL_PHI0)
            - Read PLLSR: LOCK = 1
            - Read FXOSC.STAT: OSC_STAT = 1
            - (Optional) Route CORE_CLK to CLKOUT_RUN and measure
```

### Quick Reference — Frequency Options Summary

| Option | CORE_CLK | Source | MUX_0 SELCTL |
|---|---|---|---|
| A (High Performance) | **160 MHz** | PLL_PHI0 | 1000b (8) |
| B (Reduced Speed) | **120 MHz** | PLL_PHI0 | 1000b (8) |
| C (Boot Standby) | **24 MHz** | FIRC (÷2) | 0000b (0) |
| D (Low Speed) | **48 MHz** | FIRC (÷1) | 0000b (0) |
| E (Very Low) | **3 MHz** | FIRC (÷16) | 0000b (0) |
| E2 (Ultra Low) | **750 kHz** | FIRC (÷16 × 4) | 0000b (0) |
| F (1:1 Mode) | **160 MHz** | PLL_PHI0 | 1000b (8) |

### Key Base Addresses Quick Reference

| Module | Base Address |
|---|---|
| MC_CGM | 402D_8000h |
| PLLDIG | 402E_0000h |
| FIRC | 402D_0000h |
| SIRC | 402C_8000h |
| FXOSC | 402D_4000h |
| SXOSC | 402C_C000h |
| PMC | (see PMC chapter) |
| MC_ME | (see MC_ME chapter) |

---

*Guide created from MCXE31x Reference Manual, Rev. 2, 2025-08-05 (Preliminary Information)*  
*Chapters 23–29. All register addresses, bit fields, and frequency values sourced directly from RM.*
