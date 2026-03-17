# MCXE247 System Clock Configuration Guide

> **Based strictly on:** MCXE24x Series Reference Manual, Rev. 2, 06/2025 (Document No. MCXE24XRM)
> **Covers:** Chapter 25 – Clock Distribution | Chapter 26 – System Clock Generator (SCG) | Chapter 27 – Peripheral Clock Controller (PCC)

---

## Table of Contents

1. [The Big Picture — Clock Tree Architecture](#1-the-big-picture--clock-tree-architecture)
2. [Clock Sources Deep Dive](#2-clock-sources-deep-dive)
3. [Internal Clocking Requirements and Limits](#3-internal-clocking-requirements-and-limits)
4. [Real Configuration Examples from the RM](#4-real-configuration-examples-from-the-rm)
5. [Power Modes and Clock Behavior](#5-power-modes-and-clock-behavior)
6. [Step-by-Step Initialization — Reset to High-Speed Stable Clock](#6-step-by-step-initialization--reset-to-high-speed-stable-clock)
7. [Clock Switching — The Safe Protocol](#7-clock-switching--the-safe-protocol)
8. [Clock Monitor Safety Requirements](#8-clock-monitor-safety-requirements)
9. [SCG Register Reference — The Complete Table](#9-scg-register-reference--the-complete-table)
10. [SCG Register Bit-by-Bit Breakdown](#10-scg-register-bit-by-bit-breakdown)
11. [PCC — Peripheral Clock Controller](#11-pcc--peripheral-clock-controller)
12. [Peripheral Module Clocking Summary](#12-peripheral-module-clocking-summary)
13. [Clock Output (CLKOUT) Debug Feature](#13-clock-output-clkout-debug-feature)
14. [External Oscillator Pin Configuration](#14-external-oscillator-pin-configuration)
15. [SPLL Calculation — Pin-to-Pin Math](#15-spll-calculation--pin-to-pin-math)
16. [Common Mistakes and Gotchas](#16-common-mistakes-and-gotchas)
17. [Safe Production-Grade Initialization Flow](#17-safe-production-grade-initialization-flow)
18. [IEC 60730 Clock Safety Considerations](#18-iec-60730-clock-safety-considerations)

---

## 1. The Big Picture — Clock Tree Architecture

Before touching a single register, understand the shape of the clock system. Think of it like a **city water supply**:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         CLOCK SOURCES (Water Tanks)                         │
│                                                                             │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐  │
│   │  SOSC    │  │  SIRC    │  │  FIRC    │  │  SPLL    │  │ LPO128K   │  │
│   │ External │  │  8 MHz   │  │  48 MHz  │  │ (up to   │  │ (always   │  │
│   │ Crystal  │  │ Internal │  │ Internal │  │  160MHz) │  │  on PMC)  │  │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬───────┘  │
└────────┼─────────────┼─────────────┼──────────────┼─────────────┼───────────┘
         │             │             │              │             │
         └─────────────┴─────────────┴──────────────┘             │
                                   │                               │
                         ┌─────────▼─────────┐                    │
                         │   SCG (Central     │                    │
                         │   Distribution Hub)│                    │
                         │                   │                    │
                         │  SCG_RCCR (RUN)   │                    │
                         │  SCG_HCCR (HSRUN) │                    │
                         │  SCG_VCCR (VLPR)  │                    │
                         └────────┬──────────┘                    │
                                  │                                │
            ┌─────────────────────┼───────────────┐               │
            │                     │               │               │
    ┌───────▼──────┐   ┌──────────▼───┐  ┌───────▼────┐          │
    │  CORE_CLK    │   │  BUS_CLK     │  │ FLASH_CLK  │          │
    │  (SYS_CLK)   │   │  (DIVBUS)    │  │ (DIVSLOW)  │          │
    │  Cortex-M4F  │   │  Peripherals │  │ Flash Ctrl │          │
    │  NVIC, DMA   │   │  UART, SPI   │  │ FTFC       │          │
    └──────────────┘   └──────────────┘  └────────────┘          │
                                                                   │
                    ┌──────────────────────────────────────────────┘
                    │   Async Peripheral Source Dividers (per source)
                    │
         ┌──────────┴──────────────────────────────────────────┐
         │  SPLLDIV1_CLK / SPLLDIV2_CLK                        │
         │  FIRCDIV1_CLK / FIRCDIV2_CLK                        │
         │  SIRCDIV1_CLK / SIRCDIV2_CLK  → PCC[PCS] → Module  │
         │  SOSCDIV1_CLK / SOSCDIV2_CLK                        │
         └─────────────────────────────────────────────────────┘
```

**Key terminology from the RM (Table 25-1):**

| Clock Name | What It Is | Where It Comes From |
|---|---|---|
| `CORE_CLK` / `SYS_CLK` | Clocks the Cortex-M4F core, crossbar, NVIC, Flash controller, FTM, PDB | `SCG_xCCR[SCS]` → `SCG_xCCR[DIVCORE]` |
| `BUS_CLK` | Clocks chip peripherals (UART, SPI, ADC, etc.) | `CORE_CLK` → `DIVBUS` |
| `FLASH_CLK` (`SCG_SLOW_CLK`) | Clocks the flash module (FTFC) | `CORE_CLK` → `DIVSLOW` |
| `SPLL_CLK` | Output of PLL (= `VCO_CLK ÷ 2`) | SOSC reference through PLL |
| `FIRC_CLK` | Output of Fast Internal RC | Always available |
| `SIRC_CLK` | Output of Slow Internal RC | Always available, lowest power |
| `SOSC_CLK` | System oscillator — either EXTAL pin or OSC output | `SCG_SOSCCFG[EREFS]` |
| `LPO128K_CLK` | 128 kHz always-on low power oscillator | PMC module |
| `SCG_CLKOUT` | Debug output of SCG clock to external pin | `SCG_CLKOUTCNFG[CLKOUTSEL]` |

> **After Reset:** MCU starts in **RUN mode** with **FIRC_CLK (48 MHz)** as the default system clock source. SPLL and SOSC are disabled. This is the safe starting point.

---

## 2. Clock Sources Deep Dive

### 2.1 FIRC — Fast Internal RC (Default After Reset)

- **Frequency:** 48 MHz (fixed, `FIRCCFG[RANGE] = 00`)
- **Use case:** Default clock, fast startup, no external component needed
- **Key requirement:** `FIRCREGOFF` **must be 0** when FIRC is used — the internal regulator must remain enabled
- **Stability:** Valid once `SCG_FIRCCSR[FIRCVLD] = 1`

### 2.2 SIRC — Slow Internal RC

- **Frequency:** 8 MHz (high range, `SIRCCFG[RANGE] = 1`) — this is the **only supported range on MCXE247**
- **Use case:** VLPR mode (very low power), the only valid system clock in VLPR
- **Low-power behavior:** Can be kept running in VLP modes via `SIRCLPEN = 1`

### 2.3 SOSC — System Oscillator

- **Supported crystal ranges (MCXE247-specific, from §26.1.2):**
  - `RANGE = 10` → Medium frequency: **4 MHz to 8 MHz** crystal
  - `RANGE = 11` → High frequency: **8 MHz to 40 MHz** crystal
  - (`00` and `01` are **Reserved** — do not use)
- **EREFS bit (`SCG_SOSCCFG[EREFS]`):**
  - `EREFS = 1` → Internal crystal oscillator (OSC) selected — use this for a crystal
  - `EREFS = 0` → External reference clock at EXTAL pin (square wave in, no crystal)
- **HGO bit:** `HGO = 0` low-gain (power saving), `HGO = 1` high-gain (better noise immunity)
- **Key rule:** **SOSCCFG cannot be changed while SOSC is enabled.** Configure first, then enable.
- **PLL dependency:** If SPLL is used, SOSC **must be in high range** (`RANGE = 11`)

### 2.4 SPLL — System Phase-Locked Loop

- **Source:** Always uses SOSC as its reference
- **Formula:**
  ```
  VCO_CLK  = SOSC_CLK / (PREDIV + 1) × (MULT + 16)
  SPLL_CLK = VCO_CLK / 2
  ```
- **PREDIV:** 3-bit field, divides reference (÷1 to ÷8)
- **MULT:** 5-bit field, multiplies by (16 to 47) — see §15 for full calculation table
- **Key rule:** `SCG_SPLLCFG` **cannot be changed while SPLL is enabled.** Configure first, then enable.
- **Key rule:** Configure `SCG_RCCR` to valid divider values **BEFORE** enabling SPLL (`SPLLEN = 1`)

### 2.5 Asynchronous Peripheral Dividers (DIV1 / DIV2)

Each main clock source (SPLL, FIRC, SIRC, SOSC) has **two independent post-dividers** feeding the PCC:

| Source | DIV1 Register | DIV2 Register | DIV Options |
|---|---|---|---|
| SPLL | `SCG_SPLLDIV[SPLLDIV1]` | `SCG_SPLLDIV[SPLLDIV2]` | ÷1, 2, 4, 8, 16, 32, 64, or **disabled** (000) |
| FIRC | `SCG_FIRCDIV[FIRCDIV1]` | `SCG_FIRCDIV[FIRCDIV2]` | ÷1, 2, 4, 8, 16, 32, 64, or **disabled** |
| SIRC | `SCG_SIRCDIV[SIRCDIV1]` | `SCG_SIRCDIV[SIRCDIV2]` | ÷1, 2, 4, 8, 16, 32, 64, or **disabled** |
| SOSC | `SCG_SOSCDIV[SOSCDIV1]` | `SCG_SOSCDIV[SOSCDIV2]` | ÷1, 2, 4, 8, 16, 32, 64, or **disabled** |

> **Encoding:** `000 = Output disabled`, `001 = ÷1`, `010 = ÷2`, `011 = ÷4`, `100 = ÷8`, `101 = ÷16`, `110 = ÷32`, `111 = ÷64`

**Per RM note:** Change DIV registers only when the **source clock is disabled**, to prevent glitches on the output divided clock.

---

## 3. Internal Clocking Requirements and Limits

> ⚠️ **CRITICAL:** These are hard limits from §25.4. Violating them causes hard faults, flash read errors, or unpredictable behavior.

### 3.1 Maximum Frequency Table

| Clock Domain | HSRUN Mode | RUN Mode | VLPR Mode |
|---|---|---|---|
| `CORE_CLK` / `SYS_CLK` | **112 MHz** | **80 MHz** | **4 MHz** |
| `BUS_CLK` | **56 MHz** | **48 MHz** (40 MHz when using SPLL) | **4 MHz** |
| `FLASH_CLK` | **28 MHz** | **26.67 MHz** | **1 MHz** |

**Additional hard rules:**
- `CORE_CLK` ≥ `BUS_CLK` (core must never be slower than bus)
- `BUS_CLK` must be an **integer divide** of `CORE_CLK`
- `FLASH_CLK` must be an **integer divide** of `CORE_CLK`
- Core-to-Flash clock ratio is **limited to a maximum of 8** (`CORE_CLK / FLASH_CLK ≤ 8`)
- `DIVSLOW` (FLASH_CLK) only supports ÷1 through ÷8 (values 1000 and above are **Reserved**)
- When SPLL is the system clock source, `DIVCORE` maximum is **÷4**

### 3.2 Asynchronous Peripheral Clock Frequency Limits

| Clock | RUN/HSRUN Max | VLPR/VLPS Max |
|---|---|---|
| `SPLLDIV1_CLK` | 80 MHz | Not valid in VLPR |
| `SPLLDIV2_CLK` | 40 MHz (RUN), 56 MHz (HSRUN) | Not valid in VLPR |
| `FIRCDIV1_CLK` | 48 MHz | Not valid in VLPR |
| `FIRCDIV2_CLK` | 48 MHz | Not valid in VLPR |
| `SIRCDIV1_CLK` | 8 MHz | 4 MHz |
| `SIRCDIV2_CLK` | 8 MHz | 4 MHz |
| `SOSCDIV1_CLK` | 40 MHz | Not valid in VLPR |
| `SOSCDIV2_CLK` | 40 MHz | Not valid in VLPR |

> **RM Note (§25.4):** In VLPR/VLPS mode, **all asynchronous clock sources are also restricted to 4 MHz**, as configured in `SCG_SIRCDIV`.

### 3.3 Mode-Specific Divider Rules

**HSRUN mode (§25.4.2):** Configure clock dividers **before** entering HSRUN. Do not modify dividers while in HSRUN mode.

**VLPR mode (§25.4.3):** Configure clock dividers **before** entering VLPR. Do not modify dividers while in VLPR mode.

**VLPR/VLPS entry (§25.4.4):** System clock **must be SIRC** when entering these modes. FIRC, SOSC, and SPLL **must be disabled by software** in RUN mode before any mode transition.

---

## 4. Real Configuration Examples from the RM

These are the exact register configurations from §25.4 of the RM:

### Option 1 — Slow RUN (FIRC, 48 MHz — Default After Reset)

```c
SCG->RCCR = SCG_RCCR_SCS(3)      // SCS = 0011 = FIRC
          | SCG_RCCR_DIVCORE(0)   // ÷1 → CORE = 48 MHz
          | SCG_RCCR_DIVBUS(0)    // ÷1 → BUS  = 48 MHz
          | SCG_RCCR_DIVSLOW(1);  // ÷2 → FLASH = 24 MHz
```

| Clock | Frequency |
|---|---|
| CORE_CLK | 48 MHz |
| BUS_CLK | 48 MHz |
| FLASH_CLK | 24 MHz |

### Option 2 — Normal RUN at 80 MHz (SPLL, VCO = 320 MHz)

```c
SCG->RCCR = SCG_RCCR_SCS(6)      // SCS = 0110 = SPLL
          | SCG_RCCR_DIVCORE(1)   // ÷2 → CORE = 80 MHz
          | SCG_RCCR_DIVBUS(1)    // ÷2 → BUS  = 40 MHz
          | SCG_RCCR_DIVSLOW(2);  // ÷3 → FLASH = 26.67 MHz
```

| Clock | Frequency |
|---|---|
| CORE_CLK | 80 MHz |
| BUS_CLK | 40 MHz |
| FLASH_CLK | 26.67 MHz |

### Option 3 — Normal RUN at 64 MHz (SPLL, VCO = 256 MHz)

```c
SCG->RCCR = SCG_RCCR_SCS(6)
          | SCG_RCCR_DIVCORE(1)   // ÷2 → CORE = 64 MHz
          | SCG_RCCR_DIVBUS(1)    // ÷2 → BUS  = 32 MHz
          | SCG_RCCR_DIVSLOW(2);  // ÷3 → FLASH = 21.33 MHz
```

### Option 4 — HSRUN at 112 MHz (SPLL, VCO = 224 MHz)

```c
SCG->HCCR = SCG_HCCR_SCS(6)      // SCS = 0110 = SPLL
          | SCG_HCCR_DIVCORE(0)   // ÷1 → CORE = 112 MHz
          | SCG_HCCR_DIVBUS(1)    // ÷2 → BUS  = 56 MHz
          | SCG_HCCR_DIVSLOW(3);  // ÷4 → FLASH = 28 MHz
```

### Option 6 — VLPR at 4 MHz (SIRC, 8 MHz)

```c
SCG->VCCR = SCG_VCCR_SCS(2)      // SCS = 0010 = SIRC
          | SCG_VCCR_DIVCORE(1)   // ÷2 → CORE = 4 MHz
          | SCG_VCCR_DIVBUS(0)    // ÷1 → BUS  = 4 MHz
          | SCG_VCCR_DIVSLOW(3);  // ÷4 → FLASH = 1 MHz
```

---

## 5. Power Modes and Clock Behavior

### 5.1 Valid Clock Sources per Mode

| Power Mode | Register | Valid System Clock Sources |
|---|---|---|
| RUN | `SCG_RCCR` | SOSC (0001), SIRC (0010), FIRC (0011), SPLL (0110) |
| VLPR | `SCG_VCCR` | **SIRC only** (0010) |
| HSRUN | `SCG_HCCR` | FIRC (0011), SPLL (0110) |

### 5.2 SCS Encoding Table (for SCG_xCCR[SCS])

| SCS Value | Clock Source |
|---|---|
| `0000` | Reserved |
| `0001` | System OSC (SOSC_CLK) |
| `0010` | Slow IRC (SIRC_CLK) |
| `0011` | Fast IRC (FIRC_CLK) |
| `0100` | Reserved |
| `0101` | Reserved |
| `0110` | System PLL (SPLL_CLK) |
| `0111` | Reserved |

### 5.3 STOP Mode Behavior

When the MCU enters Stop mode, all SCG clock signals are static **except:**

- `SIRC_CLK` remains available in Normal Stop **and** VLPS mode when:
  - `SCG_SIRCCSR[SIRCEN] = 1`
  - `SCG_SIRCCSR[SIRCSTEN] = 1`
  - `SCG_SIRCCSR[SIRCLPEN] = 1` (required for VLPS)

> **RM Note:** `SIRCLPEN` is applicable for VLPS mode only. `SIRCSTEN` is **not applicable** on MCXE247 and should be ignored.

### 5.4 Mode Transition Rules (§26.4.1)

Valid SCG Mode Transitions:

```
Reset
  └─► RUN  ◄────────────────────────────────────┐
         │                                       │
         ├─► HSRUN (only FIRC or SPLL valid)      │
         │      └─────────────────────────────────┘
         │
         └─► VLPR (only SIRC valid)
               └─► VLPS (SIRC can stay running)
```

> **Critical RM Note:** When transitioning between run modes (RUN ↔ HSRUN ↔ VLPR), **complete the SCG clock source switch first**, then initiate the power mode transition request. Never do them simultaneously.

---

## 6. Step-by-Step Initialization — Reset to High-Speed Stable Clock

This is the **complete professional sequence** to go from reset (FIRC 48 MHz) to SPLL-based high-speed operation.

---

### STEP 0 — Understand Your Starting State After Reset

At reset:
- System clock = **FIRC (48 MHz)**
- `SCG_CSR[SCS] = 0011` (FIRC selected)
- `SCG_RCCR[DIVSLOW] = 0001` (÷2, FLASH = 24 MHz) — reset default per RM Table 26-1
- SPLL disabled, SOSC disabled
- All PCC clocks **gated off** — peripherals inaccessible

Confirm current state:
```c
uint32_t current_scs = (SCG->CSR >> 24) & 0xF;
// Should be 3 (FIRC) after reset
```

---

### STEP 1 — Configure Flash Wait States (Do This FIRST!)

> ⚠️ **This is the most critical step for flash integrity.** Before increasing frequency, the flash controller must know you are operating at a higher speed.

Configure the Flash `LMEM` prefetch and cache accordingly. The flash controller (`FTFC`) is clocked by `FLASH_CLK`. Ensure your target `FLASH_CLK` is ≤ 26.67 MHz in RUN mode. The hardware enforces this via `DIVSLOW` but you must still ensure wait states match your operating voltage and frequency as per the datasheet.

---

### STEP 2 — Configure and Enable SOSC (External Crystal)

SOSC is the reference for SPLL. This step is mandatory if you want to use SPLL.

```c
// Step 2a: Configure SOSC BEFORE enabling it
// SOSCCFG is locked when SOSCEN=1, so configure first.
// Example: 8 MHz crystal, high range, crystal oscillator mode, low-gain
SCG->SOSCCFG = SCG_SOSCCFG_RANGE(3)   // 11 = High frequency (8–40 MHz)
             | SCG_SOSCCFG_EREFS(1)    // 1  = Internal crystal oscillator
             | SCG_SOSCCFG_HGO(0);     // 0  = Low-gain mode

// Step 2b: Configure SOSC dividers (optional, for peripheral use)
// Do this BEFORE enabling SOSC to prevent glitches
SCG->SOSCDIV = SCG_SOSCDIV_SOSCDIV1(1)   // 001 = ÷1 → SOSCDIV1_CLK = 8 MHz
             | SCG_SOSCDIV_SOSCDIV2(1);   // 001 = ÷1 → SOSCDIV2_CLK = 8 MHz

// Step 2c: Enable SOSC
// Note: If SOSCCSR has LK=1, unlock it first: SCG->SOSCCSR &= ~SCG_SOSCCSR_LK_MASK;
SCG->SOSCCSR = SCG_SOSCCSR_SOSCEN(1);

// Step 2d: Wait for SOSC to stabilize — NEVER SKIP THIS
while (!(SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK))
{
    // Poll until SOSCVLD = 1 (valid bit set)
    // Hardware confirms crystal is oscillating and stable
}
```

---

### STEP 3 — Configure and Enable SPLL

```c
// Step 3a: Configure SPLL BEFORE enabling it
// SPLLCFG is locked when SPLLEN=1
// Example: 8 MHz SOSC → VCO = 160 MHz → SPLL = 80 MHz
// VCO_CLK = SOSC / (PREDIV+1) × (MULT+16)
// VCO_CLK = 8 MHz / (0+1) × (4+16) = 8 × 20 = 160 MHz
// SPLL_CLK = 160 / 2 = 80 MHz

// For 160 MHz SPLL (80 MHz SPLL_CLK):
// PREDIV=0 (÷1), MULT=4 → VCO = 8/(1)×(20) = 160 MHz → SPLL_CLK = 80 MHz

// For 320 MHz VCO (SPLL_CLK = 160 MHz) with 8 MHz SOSC:
// PREDIV=0, MULT=24 → VCO = 8×(24+16) = 8×40 = 320 → SPLL_CLK = 160 MHz
SCG->SPLLCFG = SCG_SPLLCFG_PREDIV(0)   // 000 = ÷1
             | SCG_SPLLCFG_MULT(24);    // 11000 = multiply factor 40

// Step 3b: Configure SPLL dividers for peripheral use
SCG->SPLLDIV = SCG_SPLLDIV_SPLLDIV1(2)  // 010 = ÷2 → SPLLDIV1_CLK = 80 MHz
             | SCG_SPLLDIV_SPLLDIV2(3);  // 011 = ÷4 → SPLLDIV2_CLK = 40 MHz

// Step 3c: IMPORTANT — Configure SCG_RCCR dividers to valid values
// BEFORE enabling SPLL, per RM §26.3.17
// Set dividers for 80 MHz CORE (SPLL_CLK=160MHz / DIVCORE=2)
SCG->RCCR = SCG_RCCR_SCS(6)      // Pre-write SCS=SPLL (will switch when SPLL is valid)
          | SCG_RCCR_DIVCORE(1)   // ÷2
          | SCG_RCCR_DIVBUS(1)    // ÷2 → BUS = 40 MHz
          | SCG_RCCR_DIVSLOW(2);  // ÷3 → FLASH = 26.67 MHz

// Step 3d: Enable SPLL
SCG->SPLLCSR = SCG_SPLLCSR_SPLLEN(1);

// Step 3e: Wait for SPLL lock — NEVER SKIP THIS
while (!(SCG->SPLLCSR & SCG_SPLLCSR_SPLLVLD_MASK))
{
    // Poll until SPLLVLD = 1
    // Three consecutive lock-detect samples confirm PLL is stable
}
```

---

### STEP 4 — Perform Safe Clock Switch (Full Protocol)

Follow the mandatory clock switching protocol from §26.1.4:

```c
// Step 4a: Configure ALL reset sources to 'Reset' (not Interrupt) via RCM_SRIE
RCM->SRIE = 0x00000000;   // All reset sources → reset mode

// Step 4b: Program reset sources as Interrupt for minimum 10 LPO cycles delay
// (10 LPO cycles ≈ 10 × 7.8µs ≈ 78µs minimum delay)
// Then restore to all-reset immediately after delay
// (Implementation: busy-wait loop or use LPO timer)

// Step 4c: Execute the clock switch — write SCS field
// SCG_RCCR was already pre-written in Step 3c with SCS=0110 (SPLL)
// The hardware will switch when SPLL became valid

// Step 4d: Wait/Poll for switch completion
while (((SCG->CSR >> 24) & 0xF) != 6)
{
    // Wait until SCG_CSR[SCS] = 0110 (SPLL is active)
}

// Step 4e: Restore RCM_SRIE to original intended configuration
// Re-enable desired reset/interrupt sources
```

---

### STEP 5 — Enable Peripheral Clocks via PCC

Every peripheral needs its PCC clock gate opened **before** you can access it:

```c
// Example: Enable LPUART0 with SPLLDIV2 as functional clock

// Step 5a: Disable the clock gate first (required to change PCS)
PCC->PCCn[PCC_LPUART0_INDEX] &= ~PCC_PCCn_CGC_MASK;  // CGC = 0

// Step 5b: Select functional clock source (PCS)
PCC->PCCn[PCC_LPUART0_INDEX] = PCC_PCCn_PCS(6);  // 110 = SPLLDIV2_CLK

// Step 5c: Enable the clock gate
PCC->PCCn[PCC_LPUART0_INDEX] |= PCC_PCCn_CGC_MASK;  // CGC = 1
```

> ⚠️ **Never write PCS while CGC = 1.** The CGC bit must be 0 to change clock source or divider.

---

### STEP 6 — (Optional) Enable Clock Monitors for Safety

If using SOSC or SPLL as system clock (required for production / IEC 60730):

```c
// Enable SOSC clock monitor with reset on error
SCG->SOSCCSR |= SCG_SOSCCSR_SOSCCM_MASK    // Enable SOSC monitor
              | SCG_SOSCCSR_SOSCCMRE_MASK;  // Error → Reset (not interrupt)

// Enable SPLL clock monitor with reset on error
SCG->SPLLCSR |= SCG_SPLLCSR_SPLLCM_MASK    // Enable SPLL monitor
              | SCG_SPLLCSR_SPLLCMRE_MASK;  // Error → Reset
```

---

### STEP 7 — Verify Final State

```c
// Verify system clock source
uint32_t scs = (SCG->CSR >> 24) & 0xF;
// Expected: 6 (SPLL)

// Verify dividers
uint32_t divcore = (SCG->CSR >> 16) & 0xF;   // Should be 1 (÷2)
uint32_t divbus  = (SCG->CSR >>  4) & 0xF;   // Should be 1 (÷2)
uint32_t divslow = (SCG->CSR >>  0) & 0xF;   // Should be 2 (÷3)

// Optional: verify CLKOUT pin shows expected frequency
SCG->CLKOUTCNFG = SCG_CLKOUTCNFG_CLKOUTSEL(6); // 0110 = SPLL_CLK on CLKOUT pin
```

---

## 7. Clock Switching — The Safe Protocol

The RM §26.1.4 provides a **mandatory protocol** for all system clock switches. Never skip steps.

```
Full Clock Switch Protocol
══════════════════════════

1. RCM_SRIE = 0 (all sources → Reset mode)
                    │
                    ▼
2. Program each source as Interrupt in RCM_SRIE
   Wait minimum 10 LPO cycles (≈ 78 µs)
   Restore RCM_SRIE to 0
                    │
                    ▼
3. Write new SCS to SCG_RCCR/VCCR/HCCR
   (new source must already be enabled and valid)
                    │
                    ▼
4. Poll SCG_CSR[SCS] until it matches new value
                    │
                    ▼
5. Restore RCM_SRIE to original intended config
```

> **Why this protocol?** The LOC (Loss of Clock) flag is raised when SOSC pulses are not detected for 8–16 cycles of SIRC/256. This creates a window of 256 µs to 512 µs from oscillator cutoff to LOC-triggered reset. The RCM_SRIE manipulation prevents spurious resets during the clock switch.

**Additional rule when disabling SOSC while it is the active system clock:**
1. Configure all reset sources as Interrupt via `RCM_SRIE`
2. Disable SOSC via `SCG_SOSCCSR[SOSCEN] = 0`
3. After disabling, restore `RCM_SRIE` to reset configuration
4. When SPLL is enabled, both `SOSCCSR[SOSCCMRE]` and `SPLLCSR[SPLLCMRE]` **must be 1**, and `RCM_SRIE[LOC]` and `RCM_SRIE[LOL]` **must be 0**

---

## 8. Clock Monitor Safety Requirements

From §26.1.5 — **mandatory sequence for safety-critical use:**

### Enabling SOSC or SPLL as System Clock

Must follow this order:
1. Enable the clock source (SOSC/SPLL)
2. Enable clock monitors (`SOSCCSR[SOSCCME]` and `SPLLCSR[SPLLCME]`) and their reset event monitors (`SOSCCMRE`, `SPLLCMRE`)
3. **Then** select SOSC/SPLL as system clock source

### Disabling SOSC or SPLL as System Clock

Must follow this order:
1. Switch system clock source **away** from SOSC/SPLL
2. **Then** disable clock monitors (`SOSCCM`, `SPLLCM`) and reset event monitors

> **RM Warning:** It is imperative to follow these guidelines to safeguard device operation against loss-of-clock scenarios.

---

## 9. SCG Register Reference — The Complete Table

**SCG Base Address:** Chip-specific (see memory map Chapter 3)
**Access:** 32-bit writes only (supervisor mode). 8-bit or 16-bit writes → bus transfer error.

| Register | Offset | Access | Reset Value | Purpose |
|---|---|---|---|---|
| `SCG_VERID` | 0x000 | R | `0100_0000h` | SCG version number |
| `SCG_PARAM` | 0x004 | R | See RM | Present dividers and clock sources |
| **`SCG_CSR`** | **0x010** | **R only** | See RM | **Active system clock source and dividers (read actual state)** |
| **`SCG_RCCR`** | **0x014** | **R/W** | See RM | **RUN mode clock: SCS, DIVCORE, DIVBUS, DIVSLOW** |
| **`SCG_VCCR`** | **0x018** | **R/W** | See RM | **VLPR mode clock: SCS, DIVCORE, DIVBUS, DIVSLOW** |
| **`SCG_HCCR`** | **0x01C** | **R/W** | See RM | **HSRUN mode clock: SCS, DIVCORE, DIVBUS, DIVSLOW** |
| `SCG_CLKOUTCNFG` | 0x020 | R/W | `0300_0000h` | Select clock source for CLKOUT pin (debug) |
| **`SCG_SOSCCSR`** | **0x100** | **R/W** | See RM | **SOSC control: SOSCEN, SOSCVLD, SOSCCM, SOSCCMRE, LK** |
| `SCG_SOSCDIV` | 0x104 | R/W | `0000_0000h` | SOSC DIV1 and DIV2 post-dividers |
| **`SCG_SOSCCFG`** | **0x108** | **R/W** | `0000_0010h` | **SOSC config: RANGE, HGO, EREFS** (write before enabling) |
| `SCG_SIRCCSR` | 0x200 | R/W | `0100_0005h` | SIRC control: SIRCEN, SIRCVLD, SIRCLPEN, LK |
| `SCG_SIRCDIV` | 0x204 | R/W | `0000_0000h` | SIRC DIV1 and DIV2 post-dividers |
| `SCG_SIRCCFG` | 0x208 | R/W | `0000_0001h` | SIRC frequency range (RANGE=1 → 8 MHz) |
| `SCG_FIRCCSR` | 0x300 | R/W | See RM | FIRC control: FIRCEN, FIRCVLD, FIRCREGOFF, LK |
| `SCG_FIRCDIV` | 0x304 | R/W | `0000_0000h` | FIRC DIV1 and DIV2 post-dividers |
| `SCG_FIRCCFG` | 0x308 | R/W | `0000_0000h` | FIRC frequency (RANGE=00 → 48 MHz on MCXE247) |
| **`SCG_SPLLCSR`** | **0x600** | **R/W** | `0000_0000h` | **PLL control: SPLLEN, SPLLVLD, SPLLCM, SPLLCMRE, SPLLERR, LK** |
| `SCG_SPLLDIV` | 0x604 | R/W | `0000_0000h` | PLL DIV1 and DIV2 post-dividers |
| **`SCG_SPLLCFG`** | **0x608** | **R/W** | `0000_0000h` | **PLL: PREDIV (3-bit), MULT (5-bit)** (write before enabling) |

---

## 10. SCG Register Bit-by-Bit Breakdown

### 10.1 SCG_CSR — Clock Status Register (READ ONLY, Offset 0x010)

> This is the **ground truth** — it reflects the actual active clock configuration, not what you wrote to RCCR. Always read this to confirm a switch completed.

| Bits | Field | Description |
|---|---|---|
| 31–28 | — | Reserved |
| **27–24** | **SCS** | **Currently active system clock source (read actual state)** |
| 23–20 | — | Reserved |
| **19–16** | **DIVCORE** | **Active core clock divide ratio (0000=÷1, 0001=÷2, …, 1111=÷16)** |
| 15–8 | — | Reserved |
| **7–4** | **DIVBUS** | **Active bus clock divide ratio** |
| **3–0** | **DIVSLOW** | **Active slow/flash clock divide ratio (max 0111=÷8, 1xxx=Reserved)** |

### 10.2 SCG_RCCR / SCG_VCCR / SCG_HCCR — Clock Control Registers

Same bit layout for all three (used for RUN, VLPR, HSRUN respectively):

| Bits | Field | Description |
|---|---|---|
| 31–28 | — | Reserved |
| **27–24** | **SCS** | System clock source selection (see SCS encoding table in §5.2) |
| 23–20 | — | Reserved |
| **19–16** | **DIVCORE** | Core clock divider (0000=÷1 to 1111=÷16; max ÷4 when SPLL is source) |
| 15–8 | — | Reserved (write 0) |
| **7–4** | **DIVBUS** | Bus clock divider (must be integer multiple of DIVCORE) |
| **3–0** | **DIVSLOW** | Flash/slow clock divider (0000=÷1 to 0111=÷8, 1000+ Reserved) |

### 10.3 SCG_SOSCCSR — System OSC Control Status (Offset 0x100)

| Bit | Field | Description |
|---|---|---|
| 26 | `SOSCERR` | **System OSC Clock Error** flag (w1c — write 1 to clear) |
| 25 | `SOSCSEL` | 1 = SOSC is currently the active system clock source |
| **24** | **`SOSCVLD`** | **1 = SOSC is enabled and output clock is VALID. Poll this!** |
| 23 | `LK` | Lock bit — 1 = register cannot be written |
| 17 | `SOSCCMRE` | Clock Monitor Reset Enable — 1 = error triggers reset |
| 16 | `SOSCCM` | Clock Monitor Enable — 1 = monitor active |
| **0** | **`SOSCEN`** | **1 = Enable SOSC. Read back after writing during clock switch.** |

### 10.4 SCG_SOSCCFG — System OSC Configuration (Offset 0x108)

> **Cannot be changed while SOSCEN = 1!**

| Bits | Field | Values |
|---|---|---|
| 5–4 | `RANGE` | `10` = 4–8 MHz crystal; `11` = 8–40 MHz crystal; `00`,`01` = Reserved |
| 3 | `HGO` | `0` = low-gain (power saving); `1` = high-gain (better noise immunity) |
| 2 | `EREFS` | `0` = external reference clock at EXTAL; `1` = internal crystal oscillator |

### 10.5 SCG_SPLLCSR — System PLL Control Status (Offset 0x600)

| Bit | Field | Description |
|---|---|---|
| 26 | `SPLLERR` | System PLL clock error flag (w1c) |
| 25 | `SPLLSEL` | 1 = SPLL is currently the active system clock source |
| **24** | **`SPLLVLD`** | **1 = SPLL is enabled and locked/valid. Poll this!** |
| 23 | `LK` | Lock bit |
| 17 | `SPLLCMRE` | PLL clock monitor reset enable |
| 16 | `SPLLCM` | PLL clock monitor enable |
| **0** | **`SPLLEN`** | **1 = Enable SPLL. Read back after writing during clock switch.** |

> **RM Note:** `SPLLVLD` should only be used to verify SPLL lock after initialization. To continuously monitor SPLL, use `SPLLCM`.

### 10.6 SCG_SPLLCFG — System PLL Configuration (Offset 0x608)

> **Cannot be changed while SPLLEN = 1!**

| Bits | Field | Description |
|---|---|---|
| 20–16 | `MULT` | PLL multiplier — multiply factor = MULT + 16 (range: 16 to 47) |
| 10–8 | `PREDIV` | PLL reference divider — divide factor = PREDIV + 1 (÷1 to ÷8) |

### 10.7 SCG_FIRCCSR — Fast IRC Control Status (Offset 0x300)

| Bit | Field | Description |
|---|---|---|
| 25 | `FIRCSEL` | 1 = FIRC is active system clock |
| 24 | `FIRCVLD` | 1 = FIRC is valid |
| 23 | `LK` | Lock bit |
| 3 | **`FIRCREGOFF`** | **Must be 0 when FIRC is used. Setting to 1 disables the regulator and breaks FIRC.** |
| 0 | `FIRCEN` | 1 = Enable FIRC |

---

## 11. PCC — Peripheral Clock Controller

**PCC Base Address:** `0x4006_5000`

**Important rules from §27 of the RM:**

1. PCC registers are writable only in **supervisor mode** using **32-bit accesses**
2. After reset, **ALL peripheral clocks are gated off** (`CGC = 0`) — you must enable each peripheral before using it
3. To change `PCS` or dividers: **first set `CGC = 0`**, configure, then **set `CGC = 1`**
4. Any bus access to a peripheral with its clock disabled → **error termination**
5. After changing clock source/divider, perform a **soft reset** of the module (via module's software reset bit, if available)
6. The module must be **disabled and quiescent** before changing its clock configuration

### 11.1 PCC Register Structure

Every peripheral has its own PCC register: `PCC_<MODULE>` at offset from PCC base.

| Bit | Field | Description |
|---|---|---|
| 31 | `PR` | **Present** — read-only. `1` = peripheral exists on this device. Always check this first! |
| **30** | **`CGC`** | **Clock Gate Control** — `1` = clock enabled. When `CGC=1`, PCS/divider fields are **locked** |
| 29 | — | Reserved (don't care) |
| 28–27 | — | Reserved |
| **26–24** | **`PCS`** | **Peripheral Clock Source** — selects functional clock (see encoding below) |
| 2–0 | `PCD` | Peripheral Clock Divider (when available) |

### 11.2 PCS Encoding (for peripherals with selectable functional clock)

| PCS Value | Clock Source |
|---|---|
| `000` | Clock option 0 (external clock or peripheral-specific — see Table 25-9) |
| `001` | SOSCDIV2_CLK |
| `010` | SIRCDIV2_CLK |
| `011` | FIRCDIV2_CLK |
| `100` | Reserved |
| `101` | Reserved |
| `110` | SPLLDIV2_CLK |
| `111` | Reserved |

> **Exception:** Some peripherals use DIV1 sources (e.g., FTM uses SPLLDIV1/FIRCDIV1/SIRCDIV1/SOSCDIV1). Always check the module's clock diagram in Table 25-9.

### 11.3 PCC Memory Map (Selected Peripherals)

| Peripheral | PCC Register | Offset from PCC Base |
|---|---|---|
| FTFC (Flash) | `PCC_FTFC` | 0x080 |
| DMAMUX | `PCC_DMAMUX` | 0x084 |
| FlexCAN0 | `PCC_FlexCAN0` | 0x090 |
| FlexCAN1 | `PCC_FlexCAN1` | 0x094 |
| FlexCAN2 | `PCC_FlexCAN2` | 0x0AC |
| FTM3 | `PCC_FTM3` | 0x098 |
| ADC1 | `PCC_ADC1` | 0x09C |
| LPSPI0 | `PCC_LPSPI0` | 0x0B0 |
| LPSPI1 | `PCC_LPSPI1` | 0x0B4 |
| LPSPI2 | `PCC_LPSPI2` | 0x0B8 |
| CRC | `PCC_CRC` | 0x0C8 |
| LPIT | `PCC_LPIT` | 0x0DC |
| FTM0 | `PCC_FTM0` | 0x0E0 |
| FTM1 | `PCC_FTM1` | 0x0E4 |
| FTM2 | `PCC_FTM2` | 0x0E8 |
| ADC0 | `PCC_ADC0` | 0x0EC |
| RTC | `PCC_RTC` | 0x0F4 |
| LPTMR0 | `PCC_LPTMR0` | 0x100 |
| PORTA | `PCC_PORTA` | 0x124 |
| PORTB | `PCC_PORTB` | 0x128 |
| PORTC | `PCC_PORTC` | 0x12C |
| PORTD | `PCC_PORTD` | 0x130 |
| PORTE | `PCC_PORTE` | 0x134 |
| SAI0 | `PCC_SAI0` | 0x150 |
| SAI1 | `PCC_SAI1` | 0x154 |
| FlexIO | `PCC_FlexIO` | 0x168 |
| EWM | `PCC_EWM` | 0x184 |
| LPI2C0 | `PCC_LPI2C0` | 0x198 |
| LPI2C1 | `PCC_LPI2C1` | 0x19C |
| LPUART0 | `PCC_LPUART0` | 0x1A8 |
| LPUART1 | `PCC_LPUART1` | 0x1AC |
| LPUART2 | `PCC_LPUART2` | 0x1B0 |
| FTM4–FTM7 | `PCC_FTM4–7` | 0x1B8–0x1C4 |
| CMP0 | `PCC_CMP0` | 0x1CC |
| QuadSPI | `PCC_QSPI` | 0x1D8 |
| ENET | `PCC_ENET` | 0x1E4 |

---

## 12. Peripheral Module Clocking Summary

From RM Table 25-8:

| Module | Bus Interface Clock | CGC Gated? | Functional Clock via PCS | Notes |
|---|---|---|---|---|
| LPUART (0,1,2) | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Max freq governed by BUS_CLK |
| LPSPI (0,1,2) | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Max freq governed by BUS_CLK |
| LPI2C (0,1) | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Max freq governed by BUS_CLK |
| FlexIO | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Peripheral clock ≤ 2× bus clock |
| FlexCAN (0,1,2) | SYS_CLK | Yes | — | Also uses SOSCDIV2; SYS_CLK > 1.5× protocol clock |
| LPTMR0 | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Also: LPO1K, RTC_CLK via LPTMR0_PSR |
| LPIT | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Max freq governed by BUS_CLK |
| RTC | BUS_CLK | Yes | — | Uses RTC_CLK, LPO1K |
| PDB (0,1) | SYS_CLK | Yes | — | Max freq governed by SYS_CLK |
| FTM (0–7) | SYS_CLK | Yes | SPLLDIV1, FIRCDIV1, SIRCDIV1, SOSCDIV1 | Counter also uses TCLK0/1/2 pins |
| ADC (0,1) | BUS_CLK | Yes | SPLLDIV2, FIRCDIV2, SIRCDIV2, SOSCDIV2 | Max conversion clock 50 MHz; must be < BUS_CLK |
| WDOG | BUS_CLK | **No** | — | Uses LPO_CLK, SOSC_CLK, or SIRC_CLK |
| EWM | BUS_CLK | Yes | — | Uses LPO_CLK |
| CRC | BUS_CLK | Yes | — | |
| DMA | SYS_CLK | **No** | — | |
| GPIO | SYS_CLK | **No** | — | |
| PORT (A–E) | BUS_CLK | Yes | — | Also uses LPO128K for digital filter |
| FTFC (Flash) | FLASH_CLK | Yes | — | Do not change during flash access! |
| QuadSPI | BUS_CLK/SYS_CLK | Yes | SPLLDIV1 or FIRCDIV1 | MCXE247 only |
| ENET | SYS_CLK | Yes | SIRCDIV1, FIRCDIV1, SOSCDIV1, SPLLDIV1 | MCXE247 only |
| SAI (0,1) | BUS_CLK | Yes | SPLLDIV1 | BCLK must never exceed bus interface clock |
| EIM, ERM, MSCM, MPU, DMA | SYS_CLK | **No** | — | Gated via SIM_PLATCGC |

> **Modules without CGC gating (like WDOG, DMA, GPIO)** are controlled differently — some via `SIM_PLATCGC`, others are always clocked.

---

## 13. Clock Output (CLKOUT) Debug Feature

You can observe any internal clock on the external `CLKOUT` pin for debugging and verification.

**Register:** `SCG_CLKOUTCNFG` (Offset 0x020)
**Field:** `CLKOUTSEL[27:24]`

| CLKOUTSEL | Output on CLKOUT Pin |
|---|---|
| `0000` | SCG SLOW Clock (FLASH_CLK) |
| `0001` | System OSC (SOSC_CLK) |
| `0010` | Slow IRC (SIRC_CLK) |
| `0011` | Fast IRC (FIRC_CLK) |
| `0100` | Reserved |
| `0101` | Reserved |
| **`0110`** | **System PLL (SPLL_CLK)** |

Additionally, `SIM_CHIPCTL[CLKOUTSEL]` selects from a broader set including LPO, BUS_CLK, RTC, QSPI, etc., and `SIM_CHIPCTL[CLKOUTDIV]` divides it by 1–8.

```c
// Route SPLL_CLK to CLKOUT for measurement
SCG->CLKOUTCNFG = SCG_CLKOUTCNFG_CLKOUTSEL(6);  // 0110 = SPLL_CLK

// Then configure the PORT pin multiplexing for CLKOUT function
// and measure with oscilloscope
```

---

## 14. External Oscillator Pin Configuration

When using a crystal (SOSC in crystal mode, `EREFS = 1`):

- **EXTAL pin** → Crystal input (or external reference clock input)
- **XTAL pin** → Crystal output (only for crystal mode)

From §26.1.3 and RM Signal Multiplexing Chapter:
- Configure pins via the **PORT module** (set appropriate `PCR[MUX]` for the oscillator function)
- Ensure no conflicting alternate mux assignment
- The oscillator pins have a dedicated analog function — do **not** configure them as GPIO
- Load capacitors are a **hardware design requirement** (consult crystal datasheet for CL specification)

> **RM Oscillator Guideline:** If PLL is used, SOSC must be in **high range only** (`SCG_SOSCCFG[RANGE] = 11`).

---

## 15. SPLL Calculation — Pin-to-Pin Math

**Formula (from §26.3.19):**
```
VCO_CLK  = SOSC_CLK / (PREDIV + 1) × (MULT + 16)
SPLL_CLK = VCO_CLK / 2
```

**MULT encoding (5-bit, Table 26-2):**

| MULT Bits | Multiply Factor | MULT Bits | Multiply Factor |
|---|---|---|---|
| `00000` | 16 | `10000` | 32 |
| `00001` | 17 | `10001` | 33 |
| `00100` | 20 | `11000` | 40 |
| `01000` | 24 | `11111` | 47 |

**PREDIV encoding (3-bit, Table 26-3):**

| PREDIV Bits | Divide Factor |
|---|---|
| `000` | ÷1 |
| `001` | ÷2 |
| `010` | ÷3 |
| `011` | ÷4 |
| `100` | ÷5 |
| `101` | ÷6 |
| `110` | ÷7 |
| `111` | ÷8 |

### Calculation Examples

**Example A: SOSC = 8 MHz, target SPLL_CLK = 160 MHz**
```
VCO_CLK = 320 MHz needed → 8 MHz / (0+1) × (MULT+16) = 320 MHz
→ MULT + 16 = 40 → MULT = 24 → MULT bits = 11000
→ PREDIV = 0 → PREDIV bits = 000
```

**Example B: SOSC = 8 MHz, target SPLL_CLK = 112 MHz**
```
VCO_CLK = 224 MHz needed → 8 / 1 × (MULT+16) = 224 → MULT = 12 → bits = 01100
→ PREDIV = 0
```

**Example C: SOSC = 16 MHz, target SPLL_CLK = 160 MHz**
```
VCO_CLK = 320 MHz → 16 / (PREDIV+1) × (MULT+16) = 320
→ PREDIV = 1 (÷2): 16/2 = 8 MHz ref → 8 × (MULT+16) = 320 → MULT = 24
```

---

## 16. Common Mistakes and Gotchas

| # | Mistake | What Goes Wrong | Correct Action |
|---|---|---|---|
| 1 | **Skipping Flash wait state configuration before increasing frequency** | Hard fault, data corruption, random crashes during flash reads | Always configure FLASH_CLK ≤ 26.67 MHz in RUN, and validate wait states **before** increasing CPU speed |
| 2 | **Not waiting for SOSCVLD after enabling SOSC** | System switches to an unstable/missing clock, system hangs | Always `while (!(SCG->SOSCCSR & SOSCVLD_MASK));` |
| 3 | **Not waiting for SPLLVLD after enabling SPLL** | PLL not locked, system runs at wrong frequency or hangs | Always `while (!(SCG->SPLLCSR & SPLLVLD_MASK));` |
| 4 | **Writing SCG registers in 8-bit or 16-bit accesses** | Bus transfer error (hardware enforced) | Always use 32-bit (`uint32_t`) writes |
| 5 | **Writing SCG registers in user mode** | Bus transfer error | Ensure processor is in supervisor mode |
| 6 | **Forgetting PCC CGC bit for a peripheral** | Accessing peripheral registers returns error; peripheral is dead; confusing debug | Always `PCC->PCCn[IDX] |= CGC_MASK` before using any peripheral |
| 7 | **Writing PCS while CGC = 1** | Write is silently ignored — clock source not changed | Set CGC=0, configure PCS, then CGC=1 |
| 8 | **Modifying SOSCCFG or SPLLCFG while source is enabled** | Write is silently ignored | Disable source first, configure, then re-enable |
| 9 | **Entering VLPR without switching to SIRC first** | FIRC/SOSC/SPLL not valid in VLPR — system clock lost | Switch to SIRC via SCG_VCCR, disable FIRC/SOSC/SPLL, then request VLPR |
| 10 | **Modifying SCG_VCCR dividers while already in VLPR** | Dividers must be configured **before** entering VLPR | Configure VCCR before entering the low-power mode |
| 11 | **Modifying SCG_HCCR dividers while already in HSRUN** | Same — must configure before entering HSRUN | Configure HCCR before requesting HSRUN |
| 12 | **CORE_CLK configured slower than BUS_CLK** | Undefined behavior, violates §25.4 constraint | Always: CORE ≥ BUS, both integer divides of source |
| 13 | **Core-to-flash ratio exceeding 8** | Flash reads return wrong data | Ensure `CORE_CLK / FLASH_CLK ≤ 8` |
| 14 | **Setting FIRCREGOFF = 1 while using FIRC** | FIRC cannot operate with regulator off | Keep FIRCREGOFF = 0 always |
| 15 | **Forgetting the full clock-switch RCM_SRIE protocol** | Spurious LOC reset during clock switch | Follow the 5-step protocol from §26.1.4 exactly |
| 16 | **Enabling SPLL before configuring SCG_RCCR** | RM warns to configure RCCR to valid dividers before SPLLEN=1 | Write RCCR with valid dividers, then set SPLLEN |
| 17 | **Changing peripheral clock source without disabling module** | Module may latch wrong clock, produce corrupted output | Disable module and CGC before changing PCS |
| 18 | **Assuming DIVSLOW > 0111 is valid** | Bits 1000–1111 in DIVSLOW are Reserved — behavior undefined | Only use ÷1 (0000) through ÷8 (0111) |
| 19 | **Accessing flash while changing FTFC clock settings** | RM §25.8 note: do not change flash clock settings while accessing flash | Execute from SRAM if changing flash clock frequency |
| 20 | **Using SPLL in VLPR mode** | SPLL is not a valid clock source for VLPR | Only SIRC is valid in VLPR |

---

## 17. Safe Production-Grade Initialization Flow

```
Production-Grade Clock Init Sequence
═════════════════════════════════════

1. [FLASH SAFETY] Configure Flash LMEM prefetch/wait for target frequency
         │
         ▼
2. [SOSC CONFIG] Configure SOSCCFG (RANGE, EREFS, HGO) — while SOSC disabled
         │
         ▼
3. [SOSC DIV] Configure SOSCDIV (DIV1, DIV2) — while SOSC disabled
         │
         ▼
4. [SOSC ENABLE] Set SOSCCSR[SOSCEN] = 1
         │
         ▼
5. [SOSC WAIT] Poll SOSCCSR[SOSCVLD] until 1 ✓
         │
         ▼
6. [SPLL CONFIG] Configure SPLLCFG (PREDIV, MULT) — while SPLL disabled
         │
         ▼
7. [SPLL DIV] Configure SPLLDIV (DIV1, DIV2) — while SPLL disabled
         │
         ▼
8. [PRE-CONFIG RCCR] Write SCG_RCCR with valid dividers for SPLL source
                       (DIVCORE, DIVBUS, DIVSLOW within limits)
         │
         ▼
9. [SPLL ENABLE] Set SPLLCSR[SPLLEN] = 1
         │
         ▼
10. [SPLL WAIT] Poll SPLLCSR[SPLLVLD] until 1 ✓
         │
         ▼
11. [CLOCK SWITCH] Execute safe switching protocol (§26.1.4):
    a. RCM_SRIE = 0 (all → Reset)
    b. Interrupt mode delay (10 LPO min)
    c. Write SCG_RCCR[SCS] = SPLL (0110)
    d. Poll SCG_CSR[SCS] until 0110
    e. Restore RCM_SRIE
         │
         ▼
12. [MONITORS] Enable SOSCCM + SOSCCMRE + SPLLCM + SPLLCMRE
         │
         ▼
13. [PCC GATES] Enable clocks for each peripheral via PCC[CGC]
                (CGC=0, set PCS, CGC=1 sequence)
         │
         ▼
14. [VERIFY] Read SCG_CSR and verify SCS, DIVCORE, DIVBUS, DIVSLOW
         │
         ▼
15. [DEBUG] Optional: Route SPLL_CLK to CLKOUT pin and measure
```

---

## 18. IEC 60730 Clock Safety Considerations

From §6.2.4 of the RM (Safety Overview) and §26.1.5:

The MCXE247 implements **clock monitoring** as a safety mechanism. For IEC 60730 functional safety compliance:

**Hardware-level clock monitors:**
- `SOSCCSR[SOSCCM]` — monitors SOSC for loss-of-clock
- `SOSCCSR[SOSCCMRE]` — configures whether LOC → interrupt or reset
- `SPLLCSR[SPLLCM]` — monitors SPLL for loss-of-lock
- `SPLLCSR[SPLLCMRE]` — configures LOL → interrupt or reset
- Error flags: `SOSCCSR[SOSCERR]` and `SPLLCSR[SPLLERR]` (w1c)

**LOC detection timing:** The LOC flag is raised when SOSC pulses are not detected for **8 to 16 clock cycles of SIRC/256**. This gives a LOC-to-reset delay of **256 µs to 512 µs** from oscillator cutoff.

**Software-level runtime supervision (recommended for IEC 60730):**

```c
// Periodic clock verification function (call from task or timer ISR)
void Clock_RuntimeCheck(void)
{
    // 1. Verify active clock source has not changed
    uint32_t scs = (SCG->CSR >> 24) & 0xF;
    if (scs != EXPECTED_SCS_VALUE) {
        // Clock source changed unexpectedly — safety reaction
        Safety_HandleClockError();
    }

    // 2. Verify SPLL is still valid
    if (!(SCG->SPLLCSR & SCG_SPLLCSR_SPLLVLD_MASK)) {
        Safety_HandleClockError();
    }

    // 3. Verify SOSC is still valid
    if (!(SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK)) {
        Safety_HandleClockError();
    }

    // 4. Clear any stale error flags
    SCG->SOSCCSR |= SCG_SOSCCSR_SOSCERR_MASK; // w1c
    SCG->SPLLCSR |= SCG_SPLLCSR_SPLLERR_MASK; // w1c
}
```

**Recommended safety configuration (per §26.1.5):**

- When SOSC/SPLL is system clock source: `SOSCCMRE = 1` and `SPLLCMRE = 1` (errors generate reset, not just interrupt)
- `RCM_SRIE[LOC] = 0` and `RCM_SRIE[LOL] = 0` (LOC/LOL go to reset path, not interrupt)
- Use an **independent watchdog (WDOG)** to catch any scenario where the MCU hangs
- Periodically verify `SCG_CSR[SCS]` in software

---

## Quick Reference Cheat Sheet

```
┌─────────────────────────────────────────────────────────┐
│              MCXE247 Clock Quick Reference               │
├─────────────────────────────────────────────────────────┤
│  AFTER RESET                                             │
│  SCS = FIRC (0011), CORE = 48 MHz, FLASH = 24 MHz       │
├─────────────────────────────────────────────────────────┤
│  MAX FREQUENCIES (RUN MODE)                              │
│  CORE/SYS: 80 MHz  BUS: 48 MHz (40 w/PLL)              │
│  FLASH: 26.67 MHz  Core:Flash ratio ≤ 8                 │
├─────────────────────────────────────────────────────────┤
│  KEY VALID BITS (always poll these!)                     │
│  SOSC: SCG_SOSCCSR[SOSCVLD] bit 24                      │
│  SPLL: SCG_SPLLCSR[SPLLVLD] bit 24                      │
│  FIRC: SCG_FIRCCSR[FIRCVLD] bit 24                      │
│  SIRC: SCG_SIRCCSR[SIRCVLD] bit 24                      │
├─────────────────────────────────────────────────────────┤
│  ACTIVE STATE (always read this to confirm switch)       │
│  SCG_CSR[SCS] bits 27:24                                 │
├─────────────────────────────────────────────────────────┤
│  PCC SEQUENCE: CGC=0 → set PCS → CGC=1                  │
│  PCC BASE: 0x4006_5000                                   │
├─────────────────────────────────────────────────────────┤
│  SPLL FORMULA:                                           │
│  VCO = SOSC/(PREDIV+1) × (MULT+16)                      │
│  SPLL_CLK = VCO / 2                                      │
├─────────────────────────────────────────────────────────┤
│  SCG WRITE RULES                                         │
│  ✦ 32-bit writes only (no 8/16-bit)                     │
│  ✦ Supervisor mode only                                  │
│  ✦ CFG registers: configure BEFORE enabling source      │
│  ✦ DIV registers: change BEFORE enabling source         │
└─────────────────────────────────────────────────────────┘
```

---

*Document prepared based strictly on MCXE24x Series Reference Manual, Rev. 2, 06/2025 (MCXE24XRM). All register addresses, bit fields, and frequency limits are sourced directly from the RM Chapters 25, 26, and 27.*
