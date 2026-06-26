system-overview

# System Overview

## Introduction

The ESP32 Industrial Dryer Controller is a professional-grade embedded system designed to replace conventional timer-based dryer controllers with intelligent, safety-focused automation suitable for commercial laundry facilities, industrial drying operations, and textile processing plants.

## Architecture

### Hardware Architecture
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 Microcontroller                    │
│ ┌────────────┐  ┌──────────────┐  ┌──────────────────┐      │
│ │   Core 0   │  │    Core 1    │  │  Watchdog Timer  │      │
│ │ User Tasks │  │ System Tasks │  │   10s Timeout    │      │
│ └────────────┘  └──────────────┘  └──────────────────┘      │
└─────────────────────────────────────────────────────────────┘
          │               │                 │                 │
┌─────────┴────────┬──────┴───────┬─────────┴────────┬────────┐
│                  │              │                  │        
┌───▼────┐    ┌────▼─────┐   ┌────▼─────┐     ┌──────▼──────┐
│ Sensors│    │  Relays  │   │   LCD    │     │   Buttons   │
│        │    │          │   │          │     │             │
│DS18B20 │    │  Heater  │   │   20x4   │     │ Navigation  │
│  Door  │    │   Fan    │   │   I2C    │     │   Control   │
│Airflow │    │ DrumFwd  │   │          │     │             │
│        │    │ DrumRev  │   │          │     │             │
└────────┘    └──────────┘   └──────────┘     └─────────────┘


### Software Architecture

#### State Machine Design

The controller uses a non-blocking state machine with the following states:

| State | Purpose | Duration |
|-------|---------|----------|
| `STATE_SENSOR_INIT` | Validate temperature sensor at startup | 3-10 seconds |
| `STATE_MAIN_MENU` | Main navigation interface | User-controlled |
| `STATE_SELECT_PROGRAM` | Choose drying program | User-controlled |
| `STATE_SETTINGS` | Access configuration options | User-controlled |
| `STATE_CALIBRATE_TEMP` | Adjust temperature offset | User-controlled |
| `STATE_CUSTOM_PROGRAM` | Configure custom drying parameters | User-controlled |
| `STATE_SYSTEM_STATUS` | Real-time system diagnostics | User-controlled |
| `STATE_RESUME_PROMPT` | Power-loss recovery prompt | 30 seconds timeout |
| `STATE_DRYING` | Active drying cycle | Program-dependent |
| `STATE_PAUSED` | Door-open pause state | Until door closed |
| `STATE_COOL_DOWN` | Post-cycle cooling | 5 minutes |
| `STATE_ABORT` | User-initiated stop | 2.5 seconds |
| `STATE_COMPLETE` | Successful cycle completion | 5 seconds |

#### Control Loops

**Main Loop (10ms cycle)**
- Button debouncing
- Safety input monitoring
- State machine execution
- Watchdog reset
- LCD refresh coordination

**Temperature Control (1-second update)**
- Non-blocking sensor reading
- Spike filtering (>5°C/s rejection)
- Hysteresis-based heater control
- Overheat monitoring

**Relay Management (150ms minimum spacing)**
- Non-blocking command queue
- Interlock enforcement
- Readback verification every 5 seconds

## Component Specifications

### Temperature Sensor

**DS18B20 Digital Temperature Sensor**
- Range: -55°C to +125°C
- Accuracy: ±0.5°C (-10°C to +85°C)
- Resolution: 0.0625°C (12-bit)
- Conversion Time: 750ms (12-bit)
- Interface: 1-Wire digital protocol
- Parasitic power: Not used (dedicated power recommended)

**Implementation Details:**
```cpp
OneWire oneWire(GPIO4);
DallasTemperature sensor(&oneWire);
sensor.setWaitForConversion(false);  // Non-blocking mode
sensor.requestTemperatures();        // Initiate conversion
// Wait 800ms...
float temp = sensor.getTempCByIndex(0);
Safety Inputs
Door Switch
•	Type: Normally-Open mechanical switch
•	Mounting: Door frame magnetic or mechanical
•	Debounce: 80ms software filtering
•	Active State: Closed = LOW (pulled to ground)
•	Wiring: GPIO35 with external pull-up
Airflow/Pressure Switch
•	Type: Differential pressure switch
•	Threshold: 20-50 Pa typical for dryer applications
•	Response Time: <500ms
•	Mounting: Exhaust duct or fan housing
•	Debounce: 80ms software filtering
•	Wiring: GPIO34 with external pull-up
Output Relays
All relays are solid-state or electromechanical rated for:
•	Heater Relay: 10A @ 240VAC (resistive load)
•	Fan Relay: 5A @ 240VAC (inductive load, snubber recommended)
•	Drum Motor Relays: 3A @ 240VAC each (forward/reverse)
Safety Features:
•	Interlock delay: 80ms between forward/reverse commands
•	Minimum toggle interval: 150ms
•	Readback verification: Every 5 seconds
•	Failure detection: Automatic fault state on mismatch
Display & Interface
LCD Display
•	Type: 20x4 character LCD with I2C backpack
•	I2C Address: 0x27 (configurable)
•	Bus Speed: 50kHz (reduced for EMI immunity)
•	Backlight: Auto-timeout after 30 seconds of inactivity
•	Custom Characters:
•	Progress bar blocks
•	Thermometer icon
•	Fan icon
•	Checkmark icon
Button Interface
•	5 tactile buttons with hardware pull-ups
•	Debounce: 50ms per button
•	Functions: UP, DOWN, SELECT, BACK, STOP
•	Feedback: Audible buzzer confirmation
Power Supply
Requirements:
•	Input: 12VDC, 1A minimum (2A recommended)
•	ESP32: 3.3V regulated (onboard)
•	Relays: 12VDC coil voltage
•	Total System: <15W typical, <25W peak
Recommended:
•	Isolated AC-DC converter (medical-grade for commercial use)
•	Overcurrent protection
•	Transient suppression on relay outputs
Control Algorithms
Temperature Hysteresis Control
Objective: Maintain temperature within ±3°C of setpoint while minimizing relay cycling.
Algorithm:

Target Temperature = Program Setpoint (e.g., 60°C)
On-Point = Target - HYSTERESIS (60 - 3 = 57°C)

IF Heater is OFF:
    IF Temperature ≤ On-Point:
        Turn Heater ON
    ELSE:
        Keep Heater OFF  // Dead band or above target

IF Heater is ON:
    IF Temperature ≥ Target:
        Turn Heater OFF
    ELSE:
        Keep Heater ON   // Still heating to target
Benefits:
•	Prevents rapid relay cycling (contact wear)
•	Maintains stable drying temperature
•	Reduces power consumption
•	Extends heater element life
Example Cycle:

Time    Temp    Heater    Action
────────────────────────────────────
0:00    55.0°C    OFF     55.0 ≤ 57 → Turn ON
0:30    57.5°C    ON      57.5 < 60 → Stay ON
1:00    59.2°C    ON      59.2 < 60 → Stay ON
1:30    60.0°C    ON      60.0 ≥ 60 → Turn OFF
2:00    59.5°C    OFF     59.5 > 57 → Stay OFF (dead band)
2:30    58.0°C    OFF     58.0 > 57 → Stay OFF (dead band)
3:00    56.8°C    OFF     56.8 ≤ 57 → Turn ON
Spike Filtering
Problem: DS18B20 sensors occasionally produce erroneous readings due to EMI, power fluctuations, or bus contention.
Solution:
C++
#define SPIKE_FILTER_DEGREES 5.0f

float new_reading = sensor.getTempCByIndex(0);

if (!isnan(last_good_temp)) {
    float delta = new_reading - last_good_temp;
    if (abs(delta) > SPIKE_FILTER_DEGREES) {
        // Reject spike, keep last good value
        error_count++;
        return;
    }
}

last_good_temp = new_reading;
error_count = 0;
Rationale: Industrial dryer temperature cannot physically change >5°C/second.
Overheat Protection
Dual-Layer Protection:
1.	Absolute Maximum (110°C)
•	Immediate heater shutdown
•	Fault flag set
•	Operator notification
2.	Sustained Overheat (Program Temp + 10°C for >5 minutes)
•	Tracks duration above threshold
•	Allows brief transients (door open/close)
•	Permanent fault after timeout
•	Prevents thermal damage to textiles
C++
if (temp > (program_temp + 10)) {
    if (overheat_start == 0) {
        overheat_start = millis();
    } else if (millis() - overheat_start > 300000) {  // 5 min
        set_fault(FAULT_OVERHEAT_TIME);
        shutdown_heater();
    }
} else {
    overheat_start = 0;  // Reset timer
}
Drum Rotation Control
Bi-Directional Alternating Pattern:
•	Forward: 25 seconds
•	Pause: 5 seconds
•	Reverse: 25 seconds
•	Pause: 5 seconds
•	Repeat...
Interlock Safety:
C++
void request_drum_forward() {
    stop_motor();               // Turn OFF both relays
    wait(80ms);                 // Interlock delay
    verify_both_off();          // Check actual state
    turn_on_forward_relay();
}
Purpose:
•	Prevents mechanical damage (both relays ON simultaneously)
•	Ensures even drying distribution
•	Reduces textile tangling
•	Extends motor brush life
EEPROM Wear Leveling
Circular Buffer Implementation:
•	8 rotating slots
•	Each slot stores: program index, elapsed time, calibration offset, checksum
•	Write on: cycle start, every 60 seconds during drying, pause events
•	Limits writes to ~500 per 8-hour shift (EEPROM rated 100k cycles)
Power-Loss Recovery:
1.	On startup, scan all 8 slots
2.	Validate checksums
3.	Find slot with highest elapsed time
4.	Prompt user to resume (30s timeout)
5.	Restore state if confirmed
Communication Protocols
I2C (LCD)
•	Clock: 50kHz (reduced from standard 100kHz)
•	Timeout: 5ms per transaction
•	Recovery: Bus reset on timeout (16 clock pulses)
•	Error Handling: Full reinit on 2 consecutive failures
1-Wire (DS18B20)
•	Standard 1-Wire protocol (Maxim/Dallas)
•	4.7kΩ pull-up resistor required
•	No parasitic power mode
•	CRC-8 checksum validation per read
Timing Budget (Worst-Case Analysis)
Main Loop Target: 100Hz (10ms period)

┌─────────────────────────────────┬─────────┐
│ Task                             │ Time    │
├─────────────────────────────────┼─────────┤
│ Button debounce (5 buttons)     │  0.2ms  │
│ Safety input debounce (2 inputs)│  0.1ms  │
│ Relay queue processing (4 relays)│ 0.3ms  │
│ LCD row write (I2C @ 50kHz)      │  2.0ms  │
│ State machine logic              │  0.5ms  │
│ Watchdog reset                   │  0.1ms  │
│ Misc (logging, etc.)             │  1.0ms  │
├─────────────────────────────────┼─────────┤
│ **Total (typical)**              │  4.2ms  │
│ **Margin**                       │  5.8ms  │
└─────────────────────────────────┴─────────┘

Background tasks (non-blocking):
- Temperature conversion: 800ms (request → wait → read)
- EEPROM write: 20ms every 60 seconds
- LCD full refresh: 50ms every 30 seconds
Fault Handling Matrix
Fault Code	Trigger	Heater	Fan	Drum	Recovery
FAULT_NONE	Normal operation	Auto	Auto	Auto	-
FAULT_TEMP_SENSOR	3 bad readings	OFF	ON	ON	Auto-clear on valid read
FAULT_OVERHEAT_TIME	>5min above limit	OFF	ON	ON	Manual reset (abort cycle)
FAULT_AIRFLOW	Pressure switch open	OFF	ON	ON	Auto-clear when restored
FAULT_MAX_TEMP	Temp >110°C	OFF	ON	ON	Auto-clear below threshold
FAULT_DOOR_OPEN	Door switch open	OFF	OFF	OFF	Auto-resume on close
FAULT_RELAY_FAIL	Readback mismatch	OFF	OFF	OFF	Manual reset (power cycle)
FAULT_NO_SENSOR	Startup validation fail	OFF	OFF	OFF	Manual (fix wiring)
Performance Characteristics
Temperature Stability:
•	Steady-State Ripple: ±1.5°C (with 3°C hysteresis)
•	Overshoot: <3°C on program start
•	Settling Time: <10 minutes to ±2°C
Relay Life Expectancy:
•	Heater Relay: ~50,000 cycles @ 3°C hysteresis (typical: 5-10 cycles/hour)
•	Drum Relays: ~100,000 cycles (typ: 72 cycles/hour with 25s/25s pattern)
Response Times:
•	Door Open → Heater OFF: <100ms
•	Airflow Lost → Heater OFF: <180ms (80ms debounce + relay queue)
•	Temperature Fault → Shutdown: <1.2 seconds (sensor read + decision)
User Interface Responsiveness:
•	Button Press → LCD Update: <50ms
•	Menu Navigation: <100ms per screen
•	Cycle Start → First Display: <200ms
Compliance & Standards
Design Considerations (not certified):
•	IEC 61010-1: Safety requirements for electrical equipment
•	IEC 60730-1: Automatic electrical controls (thermal cutoff)
•	EN 55011: EMC emissions limits for ISM equipment
•	UL 2404: Standard for heating appliances (informational)
Recommended Testing:
•	High-voltage isolation testing (>1000V)
•	Ground continuity verification
•	Temperature calibration against traceable standard
•	Relay contact resistance measurement
•	Power-loss recovery validation
