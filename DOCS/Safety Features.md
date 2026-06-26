Revision: v6.2
Last Updated: 2026-06-17
Author: Yohannes Gelmessa


---

## docs/safety-features.md

```markdown
# Safety Features

## Overview

The ESP32 Industrial Dryer Controller implements multiple layers of safety protection designed to prevent equipment damage, fire hazards, and textile damage. All safety systems operate independently and are continuously monitored.

---

## 1. Temperature Safety

### 1.1 Absolute Maximum Temperature Protection

**Function:** Prevents catastrophic overheating that could damage equipment or create fire hazards.

**Implementation:**
```cpp
#define MAX_TEMPERATURE 110  // °C

if (current_temp > MAX_TEMPERATURE) {
    force_relay_off(RELAY_HEATER);
    set_fault(FAULT_MAX_TEMP);
    log_error("Absolute max temp exceeded");
    alert_operator();
}
Trigger Conditions:
•	Temperature exceeds 110°C
•	Regardless of program settings
•	Independent of sensor validation state
Response:
•	Immediate heater shutdown (bypasses relay queue)
•	Fan continues running (cool-down assistance)
•	Fault flag set
•	Audible alarm (800Hz, 400ms)
•	LCD displays: ! MAX TEMP FAULT !
Recovery:
•	Automatic when temperature drops below 105°C
•	Cycle remains paused
•	Operator must acknowledge fault
________________________________________
1.2 Sustained Overheat Protection
Function: Detects heater control failure or inadequate airflow causing prolonged elevated temperature.
Algorithm:

Overheat Threshold = Program Temperature + 10°C

Example: 60°C program → Threshold = 70°C

IF temp > 70°C:
    Start timer
    IF timer > 5 minutes:
        FAULT: Overheat duration exceeded
        Shutdown heater permanently
Rationale:
•	Brief excursions during load changes are normal
•	Sustained overheat indicates system malfunction
•	Prevents textile scorching and shrinkage
Response:
•	Heater disabled for remainder of cycle
•	Fan continues running
•	Drum continues rotation (prevents hot spots)
•	LCD displays: ! TIME OVERHEAT !
•	Cycle automatically aborts after cool-down
Recovery:
•	Requires full cycle restart
•	Fault history preserved in EEPROM
•	Manual inspection recommended
________________________________________
1.3 Temperature Sensor Validation
Multi-Stage Validation:
Stage 1: Startup Validation
C++
// Requires 3 consecutive valid readings before allowing cycle start
int valid_count = 0;
for (int attempt = 0; attempt < 12; attempt++) {
    float temp = read_sensor();
    if (temp > -50 && temp < 150 && temp != DISCONNECTED) {
        valid_count++;
        if (valid_count >= 3) {
            sensor_validated = true;
            break;
        }
    } else {
        valid_count = 0;  // Reset on any failure
    }
}

if (!sensor_validated) {
    disable_drying_mode();
    display_sensor_fault();
}
Stage 2: Runtime Monitoring
C++
// Spike filter: Reject physically impossible temperature changes
float spike_threshold = 5.0;  // °C per second

float delta = new_temp - last_good_temp;
if (abs(delta) > spike_threshold) {
    reject_reading();
    error_count++;
} else {
    last_good_temp = new_temp;
    error_count = 0;
}

if (error_count >= 3) {
    set_fault(FAULT_TEMP_SENSOR);
    shutdown_heater();
}
Fault Conditions:
•	Sensor returns DEVICE_DISCONNECTED_C (-127°C)
•	Temperature outside physical range (-50°C to 150°C)
•	Spike >5°C/second detected
•	3 consecutive read errors
Response:
•	Heater immediately disabled
•	Last known good temperature displayed (with warning)
•	After 3 errors: Cycle paused, operator intervention required
________________________________________
2. Door Interlock Safety
2.1 Function
Prevents operation with door open, eliminating burn hazards and heat loss.
2.2 Implementation
Hardware:
•	Normally-Open door switch
•	Mounted on door frame
•	Actuated by door closure
•	Wired to GPIO35 with external pull-up resistor
Software:
C++
// 80ms debounce filtering
struct SafetyInput {
    bool confirmed_state;
    bool last_raw_reading;
    unsigned long last_change_time;
};

void update_door_input() {
    bool raw = (digitalRead(DOOR_SWITCH) == LOW);
    
    if (raw != door_input.last_raw_reading) {
        door_input.last_raw_reading = raw;
        door_input.last_change_time = millis();
    }
    
    if (millis() - door_input.last_change_time >= 80) {
        door_input.confirmed_state = raw;
    }
}

bool is_door_closed() {
    return door_input.confirmed_state;
}
2.3 Behavior
During Cycle:
Door State	Heater	Fan	Drum	Display
Closed	Auto	ON	Rotating	Normal
Open	OFF	OFF	STOPPED	! PAUSED !
Cycle Interruption:

1. Door opens during drying
2. Immediate shutdown (all outputs OFF within 100ms)
3. Elapsed time saved to EEPROM
4. State: PAUSED
5. Display: "Door Open - Close to resume"
6. Wait for door closure...
7. On close: 15-second purge delay, then auto-resume
Advantages:
•	Energy Savings: No heat loss through open door
•	Safety: No rotating drum or hot surfaces accessible
•	Cycle Preservation: Seamless resume without time loss
2.4 Override Protection
Anti-Bypass Measures:
•	Software checks door state every loop (100Hz)
•	Hardware switch cannot be disabled via software
•	Relay queue enforces door-closed requirement for heater
•	No "operator override" mode exists
________________________________________
3. Airflow Monitoring
3.1 Purpose
Ensures adequate ventilation before and during heating to prevent:
•	Heat accumulation causing fire hazard
•	Moisture buildup reducing drying efficiency
•	Fan motor failure detection
3.2 Hardware
Pressure/Airflow Switch:
•	Differential pressure type (duct-mounted)
•	Typical threshold: 20-50 Pa
•	Normally-Open contacts
•	Closes when airflow exceeds threshold
•	Wired to GPIO34 with external pull-up
Recommended Mounting:
•	Exhaust duct, 6-12 inches from fan outlet
•	Measure pressure drop across fan housing
•	Avoid turbulent flow areas
3.3 Software Logic
Two-Stage Gating:
C++
bool heater_can_run = false;

// Stage 1: Purge delay (fan must run 15s before heater enables)
if (millis() >= heater_enable_time && airflow_confirmed) {
    heater_can_run = true;
}

// Stage 2: Continuous monitoring
if (heater_can_run && !is_airflow_ok()) {
    shutdown_heater();
    set_fault(FAULT_AIRFLOW);
    airflow_error = true;
}
Timing Diagram:

Time    Fan    Airflow    Heater    Notes
─────────────────────────────────────────────────────
0:00    ON      NO         OFF       Cycle start
0:05    ON      NO         OFF       Waiting for airflow...
0:10    ON      YES        OFF       Airflow confirmed, purge continues
0:15    ON      YES        ON        Heater enabled (temp control begins)
0:20    ON      YES        AUTO      Normal operation
───────────────────────────────────────────────────── 
5:00    ON      NO!        OFF       ! AIRFLOW FAULT !
3.4 Fault Response
If Airflow Lost During Cycle:
1.	Immediate heater shutdown (<180ms)
2.	FAULT_AIRFLOW set
3.	Fan continues running (attempt to restore flow)
4.	Drum continues rotating (prevent hot spots)
5.	LCD: ! AIRFLOW FAULT !
6.	Audible alert (1000Hz, 500ms)
Auto-Recovery:
•	If airflow restored: Fault clears, heater re-enabled after 5s
•	If airflow remains lost: Cycle aborts after 2 minutes
3.5 Common Causes & Troubleshooting
Symptom	Probable Cause	Solution
Fault at cycle start	Lint filter clogged	Clean filter
Intermittent faults	Loose duct connection	Inspect/seal ducts
Persistent fault	Fan motor failure	Replace fan motor
Fault only when loaded	Exhaust vent obstruction	Clear exterior vent
________________________________________
4. Relay Failure Detection
4.1 Problem Statement
Relay contacts can:
•	Weld closed (overheating risk)
•	Fail to close (heater won't activate)
•	Develop high resistance (reduced performance)
Critical Scenario: Heater relay welds ON → continuous heating → fire hazard.
4.2 Readback Verification
Implementation:
C++
#define RELAY_READBACK_INTERVAL 5000  // ms

void verify_relay_states() {
    for (each relay) {
        uint8_t commanded = relay.desired_state;
        uint8_t actual    = digitalRead(relay.pin);
        
        if (commanded != actual) {
            log_error("Relay mismatch detected");
            force_all_relays_off();
            set_fault(FAULT_RELAY_FAIL);
            abort_cycle();
        }
    }
}
Verification Interval: Every 5 seconds during operation.
Limitations:
•	Detects stuck-ON relays
•	Cannot detect stuck-OFF (appears as legitimate OFF command)
•	Requires direct GPIO readback (not all relay boards support this)
4.3 Heater-Without-Fan Interlock
Rule: Heater relay can NEVER be ON while fan relay is OFF.
Enforcement:
C++
void verify_heater_fan_interlock() {
    bool heater_on = digitalRead(RELAY_HEATER);
    bool fan_on    = digitalRead(RELAY_FAN);
    
    if (heater_on && !fan_on) {
        // CRITICAL FAULT: Force everything off
        force_relay_off(RELAY_HEATER);
        force_relay_off(RELAY_FAN);
        set_fault(FAULT_RELAY_FAIL);
        
        log_critical("Heater enabled without fan - SAFETY VIOLATION");
        disable_all_outputs();
        require_power_cycle();
    }
}
Checked:
•	Every relay readback cycle (5s)
•	Before every heater command
•	During relay queue processing
________________________________________
5. Drum Motor Interlock
5.1 Purpose
Prevent simultaneous energization of forward and reverse motor contactors, which would:
•	Create short circuit across motor windings
•	Damage motor commutator
•	Trip circuit breaker
•	Potential fire hazard
5.2 Interlock Sequence
Enforced Timing:

Current State: FORWARD relay ON

Command: Switch to REVERSE

Step 1: Turn OFF both relays
Step 2: Wait 80ms (interlock delay)
Step 3: Verify both relays are physically OFF (readback)
Step 4: Turn ON REVERSE relay
Code Implementation:
C++
void request_drum_reverse() {
    // Step 1: Stop motor
    stop_drum_motor();
    
    // Step 2: Enter interlock wait state
    drum_state = DRUM_INTERLOCK;
    interlock_start_time = millis();
    interlock_target = REVERSE;
}

void update_drum_interlock() {
    if (millis() - interlock_start_time < 80) return;
    
    // Step 3: Verify both OFF
    bool fwd_off = (digitalRead(RELAY_DRUM_FORWARD) == LOW);
    bool rev_off = (digitalRead(RELAY_DRUM_REVERSE) == LOW);
    
    if (!fwd_off || !rev_off) {
        if (millis() - interlock_start_time > 400) {
            // Timeout: Relays not turning off
            set_fault(FAULT_RELAY_FAIL);
            abort_cycle();
            return;
        }
        return;  // Keep waiting...
    }
    
    // Step 4: Safe to energize reverse
    turn_on_relay(RELAY_DRUM_REVERSE);
    drum_state = DRUM_REVERSE;
}
5.3 Timeout Protection
Scenario: Relay mechanically stuck ON.
Detection:
•	If relays don't turn OFF within 400ms (5× normal delay)
•	FAULT_RELAY_FAIL set
•	All outputs disabled
•	Manual inspection required
________________________________________
6. Watchdog Timer
6.1 Purpose
Detects firmware crashes, infinite loops, or I2C bus lockups that would leave outputs in unknown states.
6.2 Configuration
C++
#define WDT_TIMEOUT_SECONDS 10

esp_task_wdt_config_t config = {
    .timeout_ms = 10000,        // 10-second timeout
    .idle_core_mask = 0,        // Monitor all cores
    .trigger_panic = true       // Reset on timeout
};

esp_task_wdt_init(&config);
esp_task_wdt_add(NULL);         // Add current task
Reset Points:
•	Start of every loop() iteration
•	During long-running operations (sensor init, delays)
•	Inside blocking waits (EEPROM writes, LCD updates)
6.3 Recovery Behavior
On Watchdog Timeout:
1.	ESP32 performs hardware reset
2.	All GPIO pins reset to INPUT (relays turn OFF)
3.	Firmware restarts from setup()
4.	Saved state loaded from EEPROM
5.	User prompted to resume or abort cycle
Reset Reason Logging:
C++
esp_reset_reason_t reason = esp_reset_reason();

const char* reasons[] = {
    "UNKNOWN", "POWERON", "EXTERNAL", "SOFTWARE",
    "PANIC", "INT_WDT", "TASK_WDT", ...
};

Serial.printf("Reset reason: %s\n", reasons[reason]);
Crash Investigation:
•	Check serial logs for reset reason
•	TASK_WDT indicates main loop stall
•	PANIC indicates fatal exception
________________________________________
7. EEPROM State Recovery
7.1 Power-Loss Scenario
Without Recovery:

1. User starts 60-minute cycle
2. 45 minutes elapsed
3. Power failure occurs
4. On restore: All progress lost, operator confused
With EEPROM Recovery:
1. Cycle in progress, state saved every 60 seconds
2. Power failure at 45-minute mark
3. On restore: "Resume last cycle? 45 min completed" prompt
4. User confirms: Cycle resumes with 15 min remaining
7.2 Data Structure
C++
struct SavedState {
    bool          in_progress;           // Cycle active flag
    int           selected_program_index; // 0-5
    unsigned long elapsed_time;          // Milliseconds
    float         temperature_offset;    // Calibration value
    uint16_t      checksum;              // CRC-8 validation
};
Storage Layout:
EEPROM Address Map:
──────────────────────────────────────
0x00: Temperature offset (float, 4 bytes)
0x04: Reserved
0x14: Slot 0 (SavedState, 16 bytes)
0x24: Slot 1
0x34: Slot 2
0x44: Slot 3
0x54: Slot 4
0x64: Slot 5
0x74: Slot 6
0x84: Slot 7
7.3 Circular Buffer Algorithm
Why: EEPROM has limited write cycles (~100,000). Rotating through 8 slots extends lifespan.
Write Logic:
C++
current_slot = (current_slot + 1) % 8;
write_to_eeprom(slot_address(current_slot), saved_state);
Read Logic:
C++
int newest_slot = 0;
unsigned long max_elapsed = 0;

for (int slot = 0; slot < 8; slot++) {
    SavedState state = read_eeprom(slot);
    
    if (state.in_progress && 
        validate_checksum(state) &&
        state.elapsed_time > max_elapsed) {
        
        max_elapsed = state.elapsed_time;
        newest_slot = slot;
    }
}

return read_eeprom(newest_slot);
7.4 Data Validation
Checksum Calculation (CRC-8):
C++
uint8_t crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}
Sanity Checks:
C++
bool validate_saved_state(SavedState s) {
    if (s.checksum != calculate_checksum(s)) return false;
    if (s.selected_program_index < 0 || 
        s.selected_program_index >= NUM_PROGRAMS) return false;
    if (s.elapsed_time > 999 * 60000) return false;  // Max 999 min
    if (isnan(s.temperature_offset)) return false;
    if (s.temperature_offset < -10 || 
        s.temperature_offset > 10) return false;
    
    return true;
}
________________________________________
8. Fault Escalation & Logging
8.1 Fault Codes
C++
#define FAULT_NONE          0x00
#define FAULT_TEMP_SENSOR   0x01  // Sensor disconnected/invalid
#define FAULT_OVERHEAT_TIME 0x02  // Sustained overheat >5min
#define FAULT_AIRFLOW       0x04  // Pressure switch open
#define FAULT_MAX_TEMP      0x08  // Absolute limit exceeded
#define FAULT_DOOR_OPEN     0x10  // Door safety interlock
#define FAULT_RELAY_FAIL    0x20  // Readback mismatch
#define FAULT_NO_SENSOR     0x40  // Startup validation failed
8.2 Fault Logging
Active Faults (cleared on recovery):
C++
uint8_t active_faults = FAULT_NONE;

void set_fault(uint8_t code) {
    active_faults |= code;
    fault_history |= code;  // Never cleared (diagnostic)
}

void clear_fault(uint8_t code) {
    active_faults &= ~code;
}
Fault History (persistent until power cycle):
•	Displayed on System Status screen
•	Useful for diagnosing intermittent issues
•	Example: Faults: 0x14 indicates door was opened (0x10) and airflow lost (0x04) during session
8.3 Serial Logging
Log Levels:
C++
#define LOG_LEVEL_DEBUG   0  // Detailed debugging info
#define LOG_LEVEL_INFO    1  // Operational events
#define LOG_LEVEL_WARNING 2  // Anomalies (recoverable)
#define LOG_LEVEL_ERROR   3  // Faults requiring action
Example Output:

[    5234][INFO ][TEMP    ] Fresh read: 58.2°C
[    5235][INFO ][HEAT    ] Heater ON: 58.2 <= 60.0
[   10456][WARN ][TEMP    ] Spike rejected: was 58.5 got 64.2
[   15678][ERROR][AIRFLOW ] No airflow after purge delay
[   15679][ERROR][FAULT   ] 0x04 set. Hist=0x04
________________________________________
9. Emergency Stop
9.1 Trigger
Pressing STOP button at any time (except MAIN_MENU):
9.2 Response
1.	Immediate relay shutdown (all outputs OFF)
2.	Cycle aborted (cannot resume)
3.	EEPROM state cleared
4.	Display: ** ABORTED **
5.	Audible alert (800Hz, 400ms)
6.	After 2.5 seconds: Return to main menu
9.3 Use Cases
•	Smoke/burning smell detected
•	Unexpected noises from drum
•	Testing/maintenance
•	Load forgotten inside (door open not working)
________________________________________
10. Safety Checklist (Pre-Operation)
Before First Use:
•	  Verify door switch mechanical operation (open/close 10×)
•	  Confirm airflow switch triggers at expected CFM
•	  Test temperature sensor with known reference (ice water, boiling water)
•	  Inspect all relay connections for proper polarity
•	  Verify heater element resistance (should match rating)
•	  Check fan motor rotation direction (exhaust, not intake)
•	  Confirm drum motor forward/reverse operation
•	  Test emergency stop button response time
•	  Review wiring diagram against actual installation
•	  Verify AC power has proper ground connection
Monthly Maintenance:
•	  Clean lint filter and exhaust duct
•	  Inspect door switch alignment
•	  Test airflow sensor response (blower test)
•	  Check relay contact condition (no pitting/discoloration)
•	  Review fault history log (serial console)
•	  Verify temperature calibration (±2°C tolerance)
•	  Lubricate drum bearings if applicable
After Fault Event:
•	  Note fault code and circumstances
•	  Check serial log for error messages
•	  Inspect relevant component (sensor, switch, relay)
•	  Verify repairs before resuming operation
•	  Document in maintenance log
________________________________________
11. Failure Mode & Effects Analysis (FMEA)
Component	Failure Mode	Detection	Effect	Mitigation
Temperature Sensor	Open circuit	3 consecutive read errors	Heater disabled, cycle paused	Redundant sensor (future), operator alert
Door Switch	Stuck closed	Cannot detect (requires periodic test)	None (fail-safe)	Monthly manual test recommended
Door Switch	Stuck open	Immediate (cannot start cycle)	Cannot operate	Operator troubleshooting
Airflow Switch	Stuck open	Immediate (15s after fan start)	Cycle aborts	Bypass switch (maintenance mode only)
Airflow Switch	Stuck closed	Undetectable during normal operation	Fan failure not detected	Periodic manual test
Heater Relay	Welded closed	Readback verification (5s interval)	Continuous heating	Automatic shutdown + fault flag
Heater Relay	Failed open	Heater never activates	No drying occurs	Operator observation (no heat)
Fan Relay	Welded closed	Not critical (fan can run continuously)	Increased power consumption	Acceptable degraded mode
Fan Relay	Failed open	Airflow switch triggers fault	Cycle cannot start	Operator troubleshooting
Drum Relay	Both welded closed	Interlock timeout (400ms)	Motor stall/damage	Automatic shutdown + fault
ESP32	Firmware crash	Watchdog timer (10s)	Hardware reset, cycle paused	EEPROM recovery on restart
Power Supply	Voltage drop	Brownout detector (ESP32 hardware)	Reset, cycle paused	EEPROM recovery on restart
LCD	I2C bus lockup	Timeout + recovery attempt	Display frozen	Auto bus reset, operation continues
________________________________________
12. Regulatory Considerations
This controller is provided for educational/portfolio purposes. Commercial deployment requires:
•	Electrical Certification: UL/CE marking for jurisdiction
•	Thermal Testing: Verify maximum case temperatures under fault conditions
•	EMC Compliance: Emissions and immunity testing per EN 55011
•	Safety Agency Review: Third-party inspection of safety circuits
•	User Manual: Warning labels, installation instructions, maintenance schedules
•	Liability Insurance: Professional indemnification
Recommended Standards:
•	IEC 61010-1: General safety requirements for electrical equipment
•	IEC 60730-1: Automatic electrical controls for household appliances
•	UL 2404: Heating appliances (US market)
