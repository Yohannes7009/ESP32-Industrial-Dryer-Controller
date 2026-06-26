Document Revision: v6.2
Last Updated: 2026-06-17
Author: Yohannes Gelmessa
Contact: yohannes.gelmessa@gmail.com

---

## docs/operation-manual.md

```markdown
# Operation Manual

## Table of Contents

1. [Quick Start Guide](#quick-start-guide)
2. [Control Interface](#control-interface)
3. [Operating Modes](#operating-modes)
4. [Drying Programs](#drying-programs)
5. [Custom Program Setup](#custom-program-setup)
6. [Temperature Calibration](#temperature-calibration)
7. [System Status Monitoring](#system-status-monitoring)
8. [Fault Diagnosis & Recovery](#fault-diagnosis--recovery)
9. [Maintenance](#maintenance)
10. [Troubleshooting](#troubleshooting)

---

## Quick Start Guide

### Initial Power-On Sequence

**Step 1: Sensor Validation (Automatic)**
┌─────────────────────────┐
│ Checking sensor... │
│ Valid: 1/3 58.2°C │
│ │
│ │
└─────────────────────────┘

- **Duration:** 3-10 seconds
- **What's Happening:** Controller validates DS18B20 temperature sensor
- **Success:** Display shows "Sensor OK!" → proceeds to main menu
- **Failure:** Display shows "! SENSOR FAULT !" → drying disabled (requires service)

**Step 2: Resume Prompt (If Applicable)**

If power was lost during a previous cycle:
┌─────────────────────────┐
│ Resume last cycle? │
│ Auto-No in: 25s │
│ > Yes │
│ No │
└─────────────────────────┘

- **Timeout:** 30 seconds (auto-selects "No")
- **Press SELECT:** Confirm choice
- **Press UP/DOWN:** Change selection

**Step 3: Main Menu**
┌─────────────────────────┐
│ ** Main Menu ** │
│ > Start Drying │
│ Settings │
│ System Status │
└─────────────────────────┘


---

## Control Interface

### Button Layout
┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐
│ UP │ │ DOWN │ │SELECT│ │ BACK │ │ STOP │
└──────┘ └──────┘ └──────┘ └──────┘ └──────┘


### Button Functions

| Button | Function | Feedback |
|--------|----------|----------|
| **UP** | Navigate up / Increase value | 2200Hz beep (60ms) |
| **DOWN** | Navigate down / Decrease value | 2200Hz beep (60ms) |
| **SELECT** | Confirm selection / Enter submenu | 1800Hz beep (150ms) |
| **BACK** | Return to previous screen / Cancel | 1600Hz beep (150ms) |
| **STOP** | Emergency abort (during cycle) | 800Hz beep (400ms) |

### LCD Display

**Specifications:**
- **Size:** 20 characters × 4 rows
- **Backlight:** Automatic timeout after 30 seconds of inactivity
- **Custom Icons:**
  - █ Progress bar fill
  - 🌡 Thermometer (temperature)
  - 🌀 Fan status
  - ✓ Checkmark (confirmation)

---

## Operating Modes

### Mode Navigation
Main Menu
├─ Start Drying → Select Program → (DRYING)
├─ Settings
│ └─ Calibrate → Adjust Offset → Save
└─ System Status → (Real-time monitoring)


### State Transitions

     ┌─────────────┐
     │ Main Menu   │
     └──────┬──────┘
            │ SELECT "Start Drying"
     ┌──────▼──────┐
     │Select Program│
     └──────┬──────┘
            │ SELECT program
     ┌──────▼──────┐         ┌──────────┐
     │   DRYING    │◄────────┤Door Open │
     └──────┬──────┘ Close   └──────────┘
            │ Cycle completes
     ┌──────▼──────┐
     │  COOL DOWN  │ (5 min)
     └──────┬──────┘
            │
     ┌──────▼──────┐
     │  COMPLETE   │ (5 sec)
     └──────┬──────┘
            │
     ┌──────▼──────┐
     │ Main Menu   │
     └─────────────┘


---

## Drying Programs

### Pre-Configured Programs

| # | Name | Target Temp | Duration | Typical Use |
|---|------|-------------|----------|-------------|
| 1 | **Quick Dry** | 60°C | 20 min | Lightly damp synthetics |
| 2 | **Standard Dry** | 75°C | 40 min | Everyday cotton/poly blends |
| 3 | **Heavy Duty** | 80°C | 60 min | Thick towels, bedding |
| 4 | **Delicate** | 55°C | 30 min | Synthetic fabrics, activewear |
| 5 | **Eco Mode** | 50°C | 50 min | Energy-efficient drying |
| 6 | **Custom** | User-defined | User-defined | Special materials |

### Program Selection

**Step 1:** From Main Menu, select "Start Drying"

**Step 2:** Navigate with UP/DOWN
┌─────────────────────────┐
│ Select Program: │
│ > Standard Dry │
│ T: 75°C Dur: 40min │
│ SEL=OK BACK=Return │
└─────────────────────────┘
**Step 3:** Press SELECT to start

### During Drying Cycle

**Display Layout:**
Row 0: T:75°C N: 68.3°C (Target / Current temp)
Row 1: H:ON F:ON D:FWD (Heater/Fan/Drum status)
Row 2: Rem: 28m 15s (Remaining time)
Row 3: [████████──────────] (Progress bar)


**Status Indicators:**
- **H:ON/OFF** — Heater state (controlled by hysteresis)
- **F:ON** — Fan always ON during cycle
- **D:FWD/REV/PAU/ILK** — Drum rotation direction
  - FWD: Forward rotation (25s)
  - REV: Reverse rotation (25s)
  - PAU: Pause between direction changes (5s)
  - ILK: Interlock delay (80ms safety pause)

**Interactive Controls:**
- **STOP or BACK:** Abort cycle immediately
- **Door Open:** Automatic pause (see [Pause Mode](#pause-mode))

---

## Pause Mode

### Automatic Triggering

**Door opened during cycle:**
┌─────────────────────────┐
│ ! PAUSED ! │
│ Door Open │
│ Close door to resume │
│ STOP=Abort │
└─────────────────────────┘


**Behavior:**
- Heater: **OFF**
- Fan: **OFF**
- Drum: **STOPPED**
- Elapsed time: Saved to EEPROM
- State: Preserved in memory

**Resuming:**
1. Close door
2. Display: "Door closed. Resuming..."
3. 15-second fan purge delay
4. Cycle continues from paused point

---

## Custom Program Setup

### Accessing Custom Program

**Method 1:** Select "Custom" from program list (if not configured, prompts setup)

**Method 2:** From Main Menu → Start Drying → Select "Custom" → Auto-enters setup

### Configuration Screen
┌─────────────────────────┐
│ Adjust Custom Prog │
│ > Temp: 60°C │
│ Time: 40 min │
│ SEL->Set Time │
└─────────────────────────┘


**Step 1: Set Temperature**
- **UP:** Increase by 1°C (max 80°C)
- **DOWN:** Decrease by 1°C (min 30°C)
- **SELECT:** Confirm, move to duration

**Step 2: Set Duration**
┌─────────────────────────┐
│ Adjust Custom Prog │
│ Temp: 60°C │
│ > Time: 40 min │
│ SEL->Start Drying │
└─────────────────────────┘- **UP:** Increase by 5 min (max 995 min)
- **DOWN:** Decrease by 5 min (min 5 min)
- **SELECT:** Save and start cycle immediately

**Step 3: Cycle Starts**
- Custom program values saved
- Cycle begins with selected parameters

---

## Temperature Calibration

### Purpose

Compensate for sensor placement variations or systematic measurement errors.

### Procedure

**Step 1:** From Main Menu, select "Settings"

**Step 2:** Select "Calibrate"
┌─────────────────────────┐
│ Calibrate Temp: │
│ Offset: +0.5°C │
│ UP/DOWN to adjust │
│ SEL=Save BACK=Exit │
└─────────────────────────┘


**Step 3:** Adjust offset
- **UP:** Increase by +0.1°C (max +5.0°C)
- **DOWN:** Decrease by -0.1°C (min -5.0°C)

**Step 4:** Save
- **SELECT:** Save to EEPROM (displays "Saved!")
- **BACK:** Discard changes

### Calibration Example

**Scenario:** Reference thermometer reads 60.0°C, controller displays 58.5°C

**Correction:**
Error = 60.0 - 58.5 = +1.5°C
Set offset to +1.5°C
New reading: 58.5 + 1.5 = 60.0°C ✓


**Recommendations:**
- Use boiling water (100°C at sea level) as reference
- Or professional thermometer calibrated to NIST standards
- Recalibrate every 6 months for critical applications

---

## System Status Monitoring

### Accessing Status Screen

From Main Menu → Select "System Status"

### Display
┌─────────────────────────┐
│ System Status │
│ Temp: 68.3°C │
│ Air:OK Door:CLSD │
│ Faults: 0x00 │
└─────────────────────────┘


**Field Definitions:**

| Field | Values | Meaning |
|-------|--------|---------|
| **Temp** | `68.3°C` | Current temperature (live, updates every 1s) |
| | `N/A` | Sensor disconnected or >3 read errors |
| **Air** | `OK` | Airflow switch closed (adequate ventilation) |
| | `FAIL` | Airflow switch open (blockage or fan failure) |
| **Door** | `CLSD` | Door closed (safe to operate) |
| | `OPEN` | Door open (cycle inhibited) |
| **Faults** | `0x00` | No faults since power-on |
| | `0x14` | Binary fault flags (see [Fault Codes](#fault-codes)) |

### Fault Code Interpretation

**Example:** `Faults: 0x14`
0x14 = 0001 0100 (binary)
│ │
│ └─ Bit 2 (0x04): FAULT_AIRFLOW
└────── Bit 4 (0x10): FAULT_DOOR_OPEN


**Meaning:** Door was opened (0x10) AND airflow was lost (0x04) during this session.

**All Codes:**
0x01 = FAULT_TEMP_SENSOR (Sensor disconnected/invalid)
0x02 = FAULT_OVERHEAT_TIME (Sustained overheat >5min)
0x04 = FAULT_AIRFLOW (Pressure switch open)
0x08 = FAULT_MAX_TEMP (Absolute limit 110°C exceeded)
0x10 = FAULT_DOOR_OPEN (Door safety interlock)
0x20 = FAULT_RELAY_FAIL (Readback mismatch)
0x40 = FAULT_NO_SENSOR (Startup validation failed)


---

## Fault Diagnosis & Recovery

### Common Faults & Solutions

#### 1. FAULT_TEMP_SENSOR (0x01)

**Symptoms:**
- Display shows `! TEMP SENSOR ERR !`
- Heater disabled
- Cycle paused

**Causes:**
- DS18B20 sensor disconnected
- Damaged sensor cable
- Water ingress in sensor housing

**Diagnosis:**
1. Power off controller
2. Check sensor wiring continuity
3. Measure resistance between DATA and GND (should be ~4.7kΩ pull-up)
4. Test sensor with multimeter (should show ~1kΩ resistance at room temp)

**Recovery:**
- Reconnect sensor → Fault auto-clears on next valid reading
- Replace sensor if damaged
- Power cycle controller to reset error counters

---

#### 2. FAULT_AIRFLOW (0x04)

**Symptoms:**
- Display shows `! AIRFLOW FAULT !`
- Heater disabled (fan continues running)
- Audible alert

**Causes:**
- Lint filter clogged (most common)
- Exhaust duct obstruction
- Pressure switch stuck/failed
- Fan motor failure

**Diagnosis:**
1. Check lint filter (clean if clogged)
2. Inspect exhaust vent (exterior) for blockage
3. Verify fan motor rotation (should hear airflow)
4. Test pressure switch with multimeter:
   - Fan OFF → Switch OPEN
   - Fan ON → Switch CLOSED (if airflow adequate)

**Recovery:**
- Clean filter/ducts → Fault auto-clears when airflow restores
- Replace pressure switch if defective
- Check fan motor bearings (should spin freely)

---

#### 3. FAULT_OVERHEAT_TIME (0x02)

**Symptoms:**
- Display shows `! TIME OVERHEAT !`
- Heater permanently disabled for cycle
- Cycle aborts after cool-down

**Causes:**
- Inadequate airflow (partial blockage)
- Heater oversized for dryer capacity
- Temperature sensor misplaced (too close to heater)
- Fan motor running slow

**Diagnosis:**
1. Check exhaust temperature (should not exceed program temp + 15°C)
2. Verify fan motor speed (CFM rating)
3. Inspect sensor placement (should be in air stream, not direct heater radiation)
4. Measure heater element resistance (compare to nameplate)

**Recovery:**
- Requires full cycle restart (cannot resume)
- Address root cause before restarting
- Consider reducing heater power or increasing airflow

---

#### 4. FAULT_DOOR_OPEN (0x10)

**Symptoms:**
- Display shows `! PAUSED !` / `Door Open`
- All outputs OFF
- Cycle preserved in EEPROM

**Causes:**
- User opened door intentionally
- Door switch misaligned
- Door latch worn

**Diagnosis:**
1. Visually inspect door closure
2. Test switch with multimeter (continuity when door closed)
3. Check switch mounting alignment

**Recovery:**
- Close door → Auto-resumes after 15s purge delay
- Adjust switch if misaligned
- Replace worn latch/switch

---

#### 5. FAULT_RELAY_FAIL (0x20)

**Symptoms:**
- Display shows `! RELAY FAIL !`
- All outputs forced OFF
- Requires power cycle

**Causes:**
- Relay contacts welded closed
- GPIO pin damaged
- Wiring short circuit
- Relay coil burned out

**Diagnosis:**
1. Power off controller
2. Measure relay coil resistance (should be 50-200Ω typically)
3. Manually inspect relay contacts (should not be fused)
4. Check wiring for shorts to adjacent pins

**Recovery:**
- Replace failed relay
- Verify GPIO pin function with multimeter (should toggle 0V/3.3V)
- Power cycle required to clear fault

---

#### 6. FAULT_MAX_TEMP (0x08)

**Symptoms:**
- Display shows `! MAX TEMP FAULT !`
- Heater immediately disabled
- Audible alarm (800Hz, 400ms)

**Causes:**
- **CRITICAL:** Fan motor stopped while heater ON
- Airflow blocked completely
- Heater relay stuck ON
- Temperature sensor shorted (reads artificially high)

**Diagnosis:**
1. **IMMEDIATE:** Verify fan motor operation
2. Check heater relay (should be OFF, measure voltage)
3. Test temperature sensor accuracy (known reference)
4. Inspect for physical damage (melted wiring, burned components)

**Recovery:**
- Auto-clears when temp drops below 105°C
- **DO NOT restart until root cause identified**
- Inspect heater element for damage
- Replace relay if contacts welded

---

### Fault Recovery Flowchart
Fault Detected
│
▼
Is it FAULT_DOOR_OPEN?
│
├─ YES → Close door → Auto-resume
│
└─ NO
│
▼
Is it FAULT_AIRFLOW?
│
├─ YES → Clean filter/ducts → Auto-resume
│
└─ NO
│
▼
Is it FAULT_TEMP_SENSOR?
│
├─ YES → Check wiring → Power cycle
│
└─ NO
│
▼
Is it FAULT_RELAY_FAIL or FAULT_MAX_TEMP?
│
├─ YES → STOP OPERATION
│ Inspect hardware
│ Replace faulty components
│ Professional service recommended
│
└─ NO → Log fault code → Contact support


---

## Maintenance

### Daily (or After Each Use)

- [ ] Clean lint filter
- [ ] Visually inspect door seal
- [ ] Check exhaust vent (exterior) for obstructions

### Weekly

- [ ] Review fault history (System Status screen)
- [ ] Test door switch operation (open/close 5×)
- [ ] Inspect drum for foreign objects

### Monthly

- [ ] Clean exhaust duct thoroughly
- [ ] Test airflow switch (blower test)
- [ ] Verify temperature calibration (±2°C tolerance)
- [ ] Inspect relay contacts (no pitting/discoloration)
- [ ] Check all wire connections for tightness

### Quarterly

- [ ] Lubricate drum bearings (if applicable)
- [ ] Replace worn door seal/gasket
- [ ] Test emergency stop response time
- [ ] Review serial logs for intermittent errors

### Annually

- [ ] Professional electrical safety inspection
- [ ] Replace temperature sensor (preventive)
- [ ] Replace all relays (high-use applications)
- [ ] Recalibrate temperature offset
- [ ] Update firmware (if available)

---

## Troubleshooting

### Issue: Display Blank

**Possible Causes:**
1. No power to controller
2. LCD backlight timeout (normal after 30s)
3. I2C connection failure
4. LCD contrast too low

**Solutions:**
1. Check 12VDC power supply voltage
2. Press any button (backlight should activate)
3. Verify I2C wiring (SDA=GPIO21, SCL=GPIO22)
4. Adjust LCD contrast potentiometer (on I2C backpack)

---

### Issue: "No Temp Sensor" Error at Startup

**Possible Causes:**
1. DS18B20 not connected
2. Wrong GPIO pin
3. Missing pull-up resistor
4. Sensor damaged

**Solutions:**
1. Verify sensor wired to GPIO4 (DATA), 3.3V (VCC), GND
2. Add 4.7kΩ resistor between DATA and VCC
3. Test sensor with Arduino "Dallas Temperature" example sketch
4. Replace sensor if validation fails after 12 attempts

---

### Issue: Heater Never Turns On

**Possible Causes:**
1. Temperature already above setpoint
2. Airflow switch not triggered
3. Door switch open
4. Heater relay failed
5. Purge delay not elapsed

**Diagnosis:**
1. Check System Status screen: `Air:OK?` `Door:CLSD?`
2. Wait 15 seconds after cycle start (purge delay)
3. Verify current temp < target temp - 3°C
4. Measure voltage across heater relay coil (should be 12V when ON)

**Solutions:**
- Ensure all safety interlocks satisfied
- Replace heater relay if coil open
- Check firmware logs for fault codes

---

### Issue: Temperature Oscillates Wildly

**Possible Causes:**
1. Sensor in direct heater radiation (not air stream)
2. Poor sensor thermal contact
3. Airflow turbulence
4. Heater element short-cycling

**Solutions:**
1. Relocate sensor to uniform air stream
2. Ensure sensor tip fully inserted in mounting pocket
3. Increase hysteresis (requires firmware modification)
4. Check heater element resistance (should be stable)

---

### Issue: Drum Doesn't Rotate

**Possible Causes:**
1. Motor power supply disconnected
2. Both drum relays stuck OFF
3. Motor bearings seized
4. Belt broken (if belt-driven)

**Diagnosis:**
1. Listen for relay clicking (should hear 2 clicks per direction change)
2. Measure voltage across motor terminals (should be 120/240VAC when relay ON)
3. Manually rotate drum (should spin freely)

**Solutions:**
- Check motor wiring connections
- Replace drum relays
- Lubricate/replace bearings
- Replace drive belt

---

### Issue: Cycle Never Completes

**Possible Causes:**
1. Heater undersized (can't reach temperature)
2. Massive heat loss (poor insulation)
3. Airflow too high (overcooling)
4. Temperature sensor reading low

**Diagnosis:**
1. Monitor temperature trend (should rise steadily)
2. Check exhaust air temperature (should match target ±5°C)
3. Verify sensor calibration (compare to reference)
4. Measure heater power consumption (should match rating)

**Solutions:**
- Reduce airflow (partially close damper)
- Increase heater power rating
- Improve dryer insulation
- Recalibrate temperature sensor

---

### Issue: Frequent Airflow Faults

**Possible Causes:**
1. Lint accumulation in duct (gradual blockage)
2. Pressure switch sensitivity too high
3. Fan motor weak/failing
4. Exhaust vent flapper stuck

**Diagnosis:**
1. Clean entire exhaust path
2. Measure airflow with anemometer (compare to design spec)
3. Check fan motor current draw (compare to nameplate)
4. Inspect exterior vent flapper (should open freely)

**Solutions:**
- Establish regular duct cleaning schedule
- Replace pressure switch with lower threshold model
- Replace fan motor if bearings worn
- Remove/replace stuck vent flapper

---

## Advanced Features

### Serial Console Logging

**Accessing Logs:**
1. Connect USB cable to ESP32
2. Open Arduino Serial Monitor (115200 baud)
3. Observe real-time log messages

**Log Example:**
[ 1234][INFO ][BOOT ] Reset: POWERON
[ 5678][INFO ][SENSOR ] Valid 3/3: 22.5C
[ 6000][INFO ][DRYING ] Initialized
[ 10456][INFO ][HEAT ] Heater ON: 57.2 <= 60.0
[ 15678][WARN ][TEMP ] Spike rejected: was 58.5 got 64.2
[ 20000][INFO ][HEAT ] Heater OFF: 60.1 >= 60.0


**Use Cases:**
- Diagnosing intermittent faults
- Verifying temperature control algorithm
- Debugging relay sequencing
- Recording session history

---

### EEPROM State Recovery

**How It Works:**

Every 60 seconds during drying, controller saves:
- Current program index
- Elapsed time
- Temperature calibration offset
- Checksum (data integrity)

**Triggering Recovery:**

Power loss → Controller restarts → Detects saved state → Prompts user:
┌─────────────────────────┐
│ Resume last cycle? │
│ Auto-No in: 28s │
│ > Yes │
│ No │
└─────────────────────────┘

**Selecting "Yes":**
- Cycle resumes from saved time
- Temperature control re-initializes
- 15-second purge delay (safety)
- Continues to completion

**Selecting "No":**
- Saved state cleared
- Returns to main menu
- Requires fresh cycle start

---

## Specifications Summary

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Temperature Range** | 30°C – 80°C | Program setpoint limits |
| **Absolute Max Temp** | 110°C | Safety cutoff |
| **Temperature Accuracy** | ±0.5°C | DS18B20 spec (-10 to +85°C) |
| **Hysteresis** | 3°C | ON at target-3, OFF at target |
| **Duration Range** | 5 – 995 minutes | User-configurable |
| **Drum Rotation** | 25s FWD, 5s pause, 25s REV, repeat | Alternating pattern |
| **Fan Purge Delay** | 15 seconds | Before heater enables |
| **Cool-Down Duration** | 5 minutes | Post-cycle, fan ON |
| **Backlight Timeout** | 30 seconds | Inactivity-based |
| **Watchdog Timeout** | 10 seconds | Auto-reset on firmware hang |
| **EEPROM Save Interval** | 60 seconds | During active drying |

---

## Warnings & Disclaimers

⚠️ **FIRE HAZARD:** Never operate dryer unattended until fully commissioned and tested.

⚠️ **ELECTRICAL SHOCK:** Disconnect AC power before servicing. Verify with multimeter.

⚠️ **THERMAL BURN:** Drum and exhaust surfaces can exceed 100°C. Allow cool-down before handling.

⚠️ **COMPONENT DAMAGE:** Exceeding 80°C program temperature may damage delicate fabrics.

⚠️ **WARRANTY:** Modifying firmware or hardware voids any implied warranty.

⚠️ **REGULATORY:** This controller is NOT certified for commercial sale. Educational use only.

---

## Support & Contact

**Author:** Yohannes Gelmessa  
**Email:** yohannes.gelmessa@gmail.com  
**Portfolio:** [GitHub Repository Link]  

**For Technical Support:**
1. Review this manual thoroughly
2. Check System Status screen for fault codes
3. Review serial console logs
4. Contact author with:
   - Fault code(s) observed
   - Serial log excerpt
   - Hardware configuration details
   - Steps to reproduce issue

**Firmware Updates:**
- Check repository for version history
- Backup EEPROM data before updating
- Follow installation instructions in README.md

---

**Document Revision:** v6.2  
**Last Updated:** 2026-01-XX  
**Compatible Firmware:** v6.2  

**End of Operation Manual**
