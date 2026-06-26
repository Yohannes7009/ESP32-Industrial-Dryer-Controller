// ESP32 Industrial Dryer Controller v6.1
// FIX: WDT-safe sensor validation, non-blocking relay writes,
//      robust LCD anti-corruption, brownout detection,
//      stack-safe display buffering

// ============================================================
//  LIBRARIES
// ============================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <math.h>
#include <esp_task_wdt.h>
#include <esp_system.h>        // esp_reset_reason()
#include <driver/rtc_io.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define ONE_WIRE_BUS        4
#define BUTTON_UP          18
#define BUTTON_DOWN        19
#define BUTTON_SELECT      33
#define BUTTON_BACK        15
#define BUTTON_STOP        23
#define NUM_BUTTONS         5

#define RELAY_HEATER       25
#define RELAY_FAN          26
#define RELAY_DRUM_FORWARD 27
#define RELAY_DRUM_REVERSE 14

#define PRESSURE_SWITCH    34
#define DOOR_SWITCH        35
#define BUZZER_PIN         32
#define I2C_SDA_PIN        21
#define I2C_SCL_PIN        22

// ============================================================
//  SAFETY & CONTROL CONSTANTS
// ============================================================
#define MAX_TEMPERATURE                 110
#define PROGRAM_MAX_TEMPERATURE          80
#define HEATER_HYSTERESIS                 3   // 3°C dead band
#define OVERHEAT_THRESHOLD_OFFSET        10
#define MAX_OVERHEAT_DURATION_SECONDS   300
#define MAX_TEMP_READ_ERRORS_HEATER       3
#define MAX_DISPLAY_TEMP_ERRORS           2
#define SENSOR_STARTUP_READS              3
#define AIRFLOW_DEBOUNCE_READS            3
#define DOOR_DEBOUNCE_READS               3

// ============================================================
//  TIMING CONSTANTS
// ============================================================
const unsigned long TEMP_READ_INTERVAL_MS      =  1000UL;
const unsigned long EEPROM_UPDATE_INTERVAL_MS  = 60000UL;
const unsigned long HEATER_DELAY_MS            = 15000UL;
const unsigned long COOL_DOWN_DURATION_MS      = 300000UL;
const unsigned long BACKLIGHT_TIMEOUT_MS       = 30000UL;
const unsigned long TEMP_CONVERSION_MS         =   800UL;

// ---- Relay timing ----
// REMOVED delay() inside safeRelayWrite — all relay timing is now
// non-blocking using millis(). This prevents WDT triggers from
// accumulated relay settle delays (4 relays × 30ms = 120ms blocked).
const unsigned long MIN_RELAY_TOGGLE_MS        =   150UL;
const unsigned long RELAY_SETTLE_MS            =    30UL;
const unsigned long RELAY_INTERLOCK_DELAY_MS   =    80UL;
const unsigned long RELAY_READBACK_INTERVAL_MS =  5000UL;

// ---- LCD timing ----
// LCD_CLEAR_INTERVAL: minimum ms between lcd.clear() calls.
// Calling clear() too rapidly on a corrupted I2C bus causes
// repeated garbage writes that compound the corruption.
const unsigned long LCD_CLEAR_INTERVAL_MS      =   200UL;
const unsigned long LCD_RECOVER_INTERVAL_MS    = 10000UL;
const unsigned long LCD_REFRESH_INTERVAL_MS    = 30000UL;
const unsigned long LCD_REINIT_COOLDOWN_MS     =  3000UL;
const unsigned long I2C_WRITE_TIMEOUT_MS       =     5UL;

// ---- Safety input debounce ----
const unsigned long SAFETY_INPUT_DEBOUNCE_MS   =    80UL;

// ---- State display durations ----
const unsigned long ABORT_DISPLAY_MS           =  2500UL;
const unsigned long COMPLETE_DISPLAY_MS        =  5000UL;
const unsigned long RESUME_PROMPT_TIMEOUT_MS   = 30000UL;

// ---- Drum ----
#define DRUM_FORWARD_DURATION_MS   25000UL
#define DRUM_REVERSE_DURATION_MS   25000UL
#define DRUM_PAUSE_DURATION_MS      5000UL

// ============================================================
//  EEPROM
// ============================================================
#define EEPROM_SIZE                512
#define EEPROM_TEMP_OFFSET_ADDRESS   0
#define EEPROM_NUM_SLOTS             8
#define EEPROM_BASE_ADDRESS         20

// ============================================================
//  WATCHDOG
// ============================================================
#define WDT_TIMEOUT_SECONDS         10
#define DEBOUNCE_DELAY_MS           50UL

// ============================================================
//  BUZZER
// ============================================================
#define BUZZ_FREQ_NAV        2200
#define BUZZ_FREQ_SELECT     1800
#define BUZZ_FREQ_BACK       1600
#define BUZZ_FREQ_ALERT      1000
#define BUZZ_FREQ_ERROR       800
#define BUZZ_FREQ_COMPLETE_1 1200
#define BUZZ_FREQ_COMPLETE_2 1500
#define BUZZ_DUR_SHORT         60
#define BUZZ_DUR_MEDIUM       150
#define BUZZ_DUR_LONG         400
#define BUZZ_DUR_ALERT        500
#define BUZZ_DUR_COMPLETE     300

// ============================================================
//  LOGGING
// ============================================================
#define LOG_LEVEL_DEBUG   0
#define LOG_LEVEL_INFO    1
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_ERROR   3
#define CURRENT_LOG_LEVEL LOG_LEVEL_INFO

// Stack-safe: all log strings are char arrays on stack, max 64 bytes
#define LOG_BUF_SIZE 64
#define LOG_D(mod, msg) logMessage(LOG_LEVEL_DEBUG,   mod, msg)
#define LOG_I(mod, msg) logMessage(LOG_LEVEL_INFO,    mod, msg)
#define LOG_W(mod, msg) logMessage(LOG_LEVEL_WARNING, mod, msg)
#define LOG_E(mod, msg) logMessage(LOG_LEVEL_ERROR,   mod, msg)

// ============================================================
//  TYPE DEFINITIONS
// ============================================================
typedef uint8_t FaultCode;
#define FAULT_NONE          0x00
#define FAULT_TEMP_SENSOR   0x01
#define FAULT_OVERHEAT_TIME 0x02
#define FAULT_AIRFLOW       0x04
#define FAULT_MAX_TEMP      0x08
#define FAULT_DOOR_OPEN     0x10
#define FAULT_RELAY_FAIL    0x20
#define FAULT_NO_SENSOR     0x40

// ============================================================
//  STATE MACHINE
// ============================================================
enum State : uint8_t {
    STATE_MAIN_MENU,
    STATE_SELECT_PROGRAM,
    STATE_SETTINGS,
    STATE_SYSTEM_STATUS,
    STATE_CALIBRATE_TEMP,
    STATE_CUSTOM_PROGRAM,
    STATE_DRYING,
    STATE_PAUSED,
    STATE_ABORT,
    STATE_COMPLETE,
    STATE_COOL_DOWN,
    STATE_RESUME_PROMPT,
    STATE_SENSOR_INIT      // NEW: non-blocking sensor validation state
};

enum DrumState : uint8_t {
    DRUM_STOPPED,
    DRUM_FORWARD,
    DRUM_REVERSE,
    DRUM_PAUSED,
    DRUM_INTERLOCK
};

enum InterlockStep : uint8_t {
    IL_IDLE,
    IL_WAIT,
    IL_APPLY
};

// ============================================================
//  DATA STRUCTURES
// ============================================================
struct Program {
    char name[16];
    int  temperature;
    int  duration;
};

struct SavedState {
    bool          inProgress;
    int           selectedProgramIndex;
    unsigned long elapsedTime;
    float         temperatureOffset;
    uint16_t      checksum;
};

struct SettingsCtx {
    int  idx;
    bool needsRedraw;
};

struct CalibrateCtx {
    float savedOffset;
};

struct CustomProgramCtx {
    int  stage;
    int  tempVal;
    int  durationVal;
};

struct ResumePromptCtx {
    int           selection;
    unsigned long entryTimeMs;
    bool          needsRedraw;
};

struct SystemStatusCtx {
    float         lastTemp;
    int           errCount;
    unsigned long reqMs;
    bool          convPending;
    bool          firstEntry;
    unsigned long lastDrawMs;
};

// ---- Debounced safety input ----
struct SafetyInput {
    bool          confirmedState;
    bool          lastRaw;
    unsigned long lastChangeMs;
    int           stableCount;
};

// ---- Non-blocking relay state ----
// Replaces blocking delay() inside safeRelayWrite.
// Each relay tracks when it was last commanded and whether
// its settle period has elapsed before the next write is allowed.
struct RelayChannel {
    uint8_t       pin;
    uint8_t       desiredState;     // What we want
    uint8_t       actualState;      // What we last wrote
    unsigned long lastToggleMs;     // When we last wrote
    bool          settleComplete;   // Has settle time elapsed?
};

struct DryingContext {
    unsigned long cycleStartTime;
    unsigned long programDurationMs;
    unsigned long endTime;
    unsigned long heaterEnableTime;
    unsigned long overheatStartMs;
    unsigned long lastTempReadTime;
    unsigned long lastEepromUpdateTime;
    unsigned long lastRelayReadbackMs;
    float         lastGoodTemp;
    int           displayErrorCount;
    int           tempReadErrorCount;
    bool          heaterOn;
    bool          heaterCanRun;
    bool          airflowConfirmedOK;
    bool          airflowError;
    bool          timeOverheatFault;
    DrumState     drumState;
    unsigned long drumStateStartMs;
    bool          drumLastWasForward;
    InterlockStep ilStep;
    unsigned long ilStartMs;
    bool          ilTargetForward;
    unsigned long tempRequestMs;
    bool          tempConversionPending;
};

struct ButtonState {
    bool          lastReading;
    bool          currentState;
    unsigned long lastDebounceMs;
    bool          pressedEvent;
};

// ---- Sensor init state (non-blocking validation) ----
struct SensorInitCtx {
    int           validCount;
    int           attempts;
    unsigned long lastAttemptMs;
    bool          convPending;
    unsigned long convStartMs;
    bool          postBootCheck;    // true = called from normal boot
};

// ============================================================
//  HARDWARE OBJECTS
// ============================================================
LiquidCrystal_I2C lcd(0x27, 20, 4);
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// ============================================================
//  RELAY CHANNELS
//  Index mapping: 0=Heater 1=Fan 2=DrumFwd 3=DrumRev
// ============================================================
static RelayChannel relays[4];

inline int relayIdx(uint8_t pin) {
    for (int i = 0; i < 4; i++)
        if (relays[i].pin == pin) return i;
    return -1;
}

// ============================================================
//  GLOBAL STATE
// ============================================================
uint8_t activeFaults  = FAULT_NONE;
uint8_t faultHistory  = FAULT_NONE;
State   currentState  = STATE_SENSOR_INIT;

DryingContext      dryCtx;
SettingsCtx        settingsCtx;
CalibrateCtx       calibrateCtx;
CustomProgramCtx   customCtx;
ResumePromptCtx    resumeCtx;
SystemStatusCtx    sysStatusCtx;
SensorInitCtx      sensorInitCtx;
SafetyInput        doorInput;
SafetyInput        airflowInput;

bool          dryingInitialized   = false;
bool          resumeRequested     = false;
bool          sensorValidated     = false;
int           selectedProgram     = 0;
int           menuIndex           = 0;
float         temperatureOffset   = 0.0f;
unsigned long elapsedDryingTime   = 0;
unsigned long coolDownStartTime   = 0;
int           currentEEPROMSlot   = 0;

const int buttonPins[NUM_BUTTONS] = {
    BUTTON_UP, BUTTON_DOWN, BUTTON_SELECT, BUTTON_BACK, BUTTON_STOP
};
ButtonState   buttons[NUM_BUTTONS];
unsigned long lastActivityTime    = 0;
bool          isBacklightOn       = true;

Program programs[] = {
    {"Quick Dry",    60, 20},
    {"Standard Dry", 75, 40},
    {"Heavy Duty",   80, 60},
    {"Delicate",     55, 30},
    {"Eco Mode",     50, 50},
    {"Custom",        0,  0}
};
const int NUM_PROGRAMS = sizeof(programs) / sizeof(programs[0]);

// LCD custom characters stored in PROGMEM to save RAM
static const uint8_t charFilledBlock[8] PROGMEM = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
static const uint8_t charThermometer[8] PROGMEM = {0x04,0x0A,0x0A,0x0E,0x0E,0x1F,0x1F,0x0E};
static const uint8_t charFan[8]         PROGMEM = {0x0A,0x1F,0x0E,0x04,0x0E,0x1F,0x0A,0x00};
static const uint8_t charCheck[8]       PROGMEM = {0x00,0x01,0x03,0x16,0x1C,0x08,0x00,0x00};

// ---- LCD protection state ----
static unsigned long lastLCDRecoverMs   = 0;
static unsigned long lastLCDReinitMs    = 0;
static unsigned long lastLCDRefreshMs   = 0;
static unsigned long lastLCDClearMs     = 0;   // Rate-limit lcd.clear()
static bool          lcdNeedsFullReinit = false;
static int           lcdErrorCount      = 0;

// ---- Transient state timers ----
static unsigned long abortEntryMs    = 0;
static unsigned long completeEntryMs = 0;
static bool          transientDrawn  = false;

// ---- LCD row cache ----
// We keep a 20-char cache of what is currently displayed on each row.
// Before writing, we compare new content to cached content.
// If identical, we skip the I2C write entirely.
// This dramatically reduces I2C bus activity and garbage character risk.
static char lcdCache[4][21];
static bool lcdCacheValid = false;

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
void logMessage(int level, const char* module, const char* message);
void setFault(FaultCode f);
void clearFault(FaultCode f);
bool hasFault(FaultCode f);
void clearAllFaults();
uint16_t calculateChecksum(const SavedState& s);
void updateButtons();
bool isButtonPressed(int pin);
void resetActivityTimer();
void updateBacklight();
int  eepromSlotAddress(int slot);
void findNewestEEPROMSlot();
void saveStateToEEPROM();
bool loadStateFromEEPROM();
void clearStateInEEPROM();
bool checkSavedStateInEEPROM();
void buzzNavigate();
void buzzSelect();
void buzzBack();
void buzzAlert(int dur);
void buzzError(int dur);
void buzzComplete();
void initCustomChars();
void updateProgressBar(int row, float pct);
void updateSafetyInputs();
bool isDoorClosed();
bool isAirflowOK();
void processRelayQueue();
void commandRelay(uint8_t pin, uint8_t state);
void forceRelayOff(uint8_t pin);
void stopDrumMotor();
void requestDrumForward(DryingContext& ctx);
void requestDrumReverse(DryingContext& ctx);
void updateDrumInterlock(DryingContext& ctx);
void updateDrumMotor(DryingContext& ctx);
void verifyRelayStates(DryingContext& ctx);
void validatePrograms();
bool i2cBusCheck();
void i2cBusRecover();
void fullLCDReinit();
void lcdRecover();
void lcdRowWrite(int row, const char* text);
void lcdSafeClear();
void lcdPrintPadded(int col, int row, const char* text, int width);
void lcdPeriodicRefresh();
void forceRedrawCurrentScreen();
void displayMainMenu();
void displaySelectProgram();
void displayCalibratePage();
void displayCustomPage();
void displaySettingsPage();
void enterState(State next);
void initDrying(bool resume);
void updateDrying();
void handleCoolDown();
void handlePaused();
void handleResumePrompt();
void handleSensorInit();
void updateSystemStatus();
void handleAbort();
void handleComplete();

// ============================================================
//  LOGGING — stack-safe, max LOG_BUF_SIZE per message
// ============================================================
void logMessage(int level, const char* module, const char* message) {
    if (level < CURRENT_LOG_LEVEL) return;
    const char* lvl[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    // Use Serial.printf directly — avoids extra stack frame
    Serial.printf("[%8lu][%s][%-8s] %s\n",
                  millis(), lvl[level], module, message);
}

// ============================================================
//  FAULT MANAGEMENT
// ============================================================
void setFault(FaultCode f) {
    activeFaults |= f;
    faultHistory |= f;
    char buf[LOG_BUF_SIZE];
    snprintf(buf, LOG_BUF_SIZE, "0x%02X set. Hist=0x%02X", f, faultHistory);
    LOG_E("FAULT", buf);
}
void clearFault(FaultCode f) { activeFaults &= (uint8_t)(~f); }
bool hasFault(FaultCode f)   { return (activeFaults & f) != 0; }
void clearAllFaults()         { activeFaults = FAULT_NONE; LOG_I("FAULT","Cleared"); }

// ============================================================
//  CRC / CHECKSUM
// ============================================================
static uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

uint16_t calculateChecksum(const SavedState& s) {
    const uint8_t* d = (const uint8_t*)&s;
    size_t sz = sizeof(SavedState) - sizeof(s.checksum);
    uint8_t c = crc8(d, sz);
    return (uint16_t)(((uint16_t)c << 8) | (uint8_t)(~c));
}

// ============================================================
//  BUTTONS
// ============================================================
void updateButtons() {
    unsigned long now = millis();
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool reading = (digitalRead(buttonPins[i]) == LOW);
        if (reading != buttons[i].lastReading)
            buttons[i].lastDebounceMs = now;
        if ((now - buttons[i].lastDebounceMs) >= DEBOUNCE_DELAY_MS) {
            if (reading != buttons[i].currentState) {
                buttons[i].currentState = reading;
                if (reading) buttons[i].pressedEvent = true;
            }
        }
        buttons[i].lastReading = reading;
    }
}

bool isButtonPressed(int pin) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (buttonPins[i] == pin && buttons[i].pressedEvent) {
            buttons[i].pressedEvent = false;
            resetActivityTimer();
            return true;
        }
    }
    return false;
}

void resetActivityTimer() {
    lastActivityTime = millis();
    if (!isBacklightOn) { lcd.backlight(); isBacklightOn = true; }
}

void updateBacklight() {
    if (!isBacklightOn) return;
    if ((millis() - lastActivityTime) <= BACKLIGHT_TIMEOUT_MS) return;
    switch (currentState) {
        case STATE_MAIN_MENU: case STATE_SELECT_PROGRAM: case STATE_SETTINGS:
        case STATE_SYSTEM_STATUS: case STATE_CALIBRATE_TEMP:
        case STATE_CUSTOM_PROGRAM: case STATE_COMPLETE:
            lcd.noBacklight(); isBacklightOn = false; break;
        default:
            lastActivityTime = millis(); break;
    }
}

// ============================================================
//  SAFETY INPUTS — hardware debounce
//  Uses time-based debounce (not count-based) for better
//  noise immunity with varying loop timing.
// ============================================================
static void updateOneSafetyInput(SafetyInput& inp, bool rawActive) {
    unsigned long now = millis();
    if (rawActive != inp.lastRaw) {
        inp.lastRaw      = rawActive;
        inp.lastChangeMs = now;
    }
    if ((now - inp.lastChangeMs) >= SAFETY_INPUT_DEBOUNCE_MS) {
        inp.confirmedState = rawActive;
    }
}

void updateSafetyInputs() {
    updateOneSafetyInput(doorInput,    digitalRead(DOOR_SWITCH)     == LOW);
    updateOneSafetyInput(airflowInput, digitalRead(PRESSURE_SWITCH) == LOW);
}

bool isDoorClosed() { return doorInput.confirmedState;    }
bool isAirflowOK()  { return airflowInput.confirmedState; }

// ============================================================
//  NON-BLOCKING RELAY QUEUE
//
//  Problem with v6.0: safeRelayWrite() called delay(30) for
//  settle time and delay() for minimum toggle time. With 4
//  relays, this blocked up to 120ms per cycle, triggering WDT
//  restarts when multiple relays changed state together.
//
//  Solution: commandRelay() stores the desired state.
//  processRelayQueue() runs every loop and applies pending
//  changes only when the relay's settle time has elapsed.
//  No blocking delay() anywhere in relay code.
// ============================================================
void processRelayQueue() {
    unsigned long now = millis();
    for (int i = 0; i < 4; i++) {
        if (relays[i].desiredState == relays[i].actualState) continue;
        if ((now - relays[i].lastToggleMs) < MIN_RELAY_TOGGLE_MS) continue;

        portDISABLE_INTERRUPTS();
        digitalWrite(relays[i].pin, relays[i].desiredState);
        portENABLE_INTERRUPTS();

        relays[i].actualState  = relays[i].desiredState;
        relays[i].lastToggleMs = now;

        char buf[LOG_BUF_SIZE];
        snprintf(buf, LOG_BUF_SIZE, "Pin %d -> %s",
                 relays[i].pin, relays[i].desiredState ? "ON" : "OFF");
        LOG_D("RELAY", buf);
    }
}

// Command a relay to a desired state (non-blocking)
void commandRelay(uint8_t pin, uint8_t state) {
    int i = relayIdx(pin);
    if (i < 0) return;
    relays[i].desiredState = state;
}

// Immediately force a relay off (safety use only — still non-blocking)
void forceRelayOff(uint8_t pin) {
    int i = relayIdx(pin);
    if (i < 0) return;
    relays[i].desiredState = LOW;
    // Apply immediately without waiting for queue
    portDISABLE_INTERRUPTS();
    digitalWrite(pin, LOW);
    portENABLE_INTERRUPTS();
    relays[i].actualState  = LOW;
    relays[i].lastToggleMs = millis();
}

void stopDrumMotor() {
    commandRelay(RELAY_DRUM_FORWARD, LOW);
    commandRelay(RELAY_DRUM_REVERSE, LOW);
}

// ============================================================
//  RELAY READBACK VERIFICATION
// ============================================================
void verifyRelayStates(DryingContext& ctx) {
    unsigned long now = millis();
    if ((now - ctx.lastRelayReadbackMs) < RELAY_READBACK_INTERVAL_MS) return;
    ctx.lastRelayReadbackMs = now;

    for (int i = 0; i < 4; i++) {
        uint8_t actual = (uint8_t)digitalRead(relays[i].pin);
        if (actual != relays[i].actualState) {
            char buf[LOG_BUF_SIZE];
            snprintf(buf, LOG_BUF_SIZE,
                     "Pin %d: want=%d actual=%d MISMATCH",
                     relays[i].pin, relays[i].actualState, actual);
            LOG_E("RELAY", buf);
            // Re-apply without blocking
            portDISABLE_INTERRUPTS();
            digitalWrite(relays[i].pin, relays[i].actualState);
            portENABLE_INTERRUPTS();
            relays[i].lastToggleMs = millis();
        }
    }

    // Safety cross-check: heater ON without fan is forbidden
    int hi = relayIdx(RELAY_HEATER);
    int fi = relayIdx(RELAY_FAN);
    if (relays[hi].actualState == HIGH && relays[fi].actualState == LOW) {
        LOG_E("RELAY", "Heater ON without fan — forcing heater OFF");
        forceRelayOff(RELAY_HEATER);
        ctx.heaterOn = false;
        setFault(FAULT_RELAY_FAIL);
    }
}

// ============================================================
//  LCD ROW CACHE
//
//  Root cause of garbage characters:
//  1. Every loop iteration was writing all 4 rows unconditionally,
//     generating constant I2C traffic that is corrupted by relay EMI.
//  2. Corrupted writes set random pixels on the LCD CGRAM.
//
//  Fix: lcdRowWrite() compares new text against cached content.
//  Only writes when content has actually changed.
//  This cuts I2C traffic by ~95% during steady-state drying,
//  dramatically reducing the window for EMI corruption.
// ============================================================
void invalidateLCDCache() {
    lcdCacheValid = false;
    for (int r = 0; r < 4; r++)
        memset(lcdCache[r], 0xFF, 21); // 0xFF = impossible char, forces redraw
}

// Write one row only if content differs from cache.
// 'text' must be exactly 20 chars (pad with spaces if needed).
void lcdRowWrite(int row, const char* text) {
    if (lcdNeedsFullReinit) return;
    if (row < 0 || row > 3) return;

    // Build a 20-char zero-terminated copy for comparison
    char padded[21];
    int i = 0;
    while (i < 20 && text[i]) { padded[i] = text[i]; i++; }
    while (i < 20)             { padded[i] = ' ';     i++; }
    padded[20] = '\0';

    // Skip if identical to cached
    if (lcdCacheValid && memcmp(lcdCache[row], padded, 20) == 0) return;

    // Write to LCD
    lcd.setCursor(0, row);
    lcd.print(padded);

    // Update cache
    memcpy(lcdCache[row], padded, 21);
    lcdCacheValid = true;
}

// ============================================================
//  LCD SAFE CLEAR
//  Rate-limited to prevent hammering the I2C bus.
// ============================================================
void lcdSafeClear() {
    if (lcdNeedsFullReinit) { fullLCDReinit(); return; }

    unsigned long now = millis();
    if ((now - lastLCDClearMs) < LCD_CLEAR_INTERVAL_MS) {
        delay(LCD_CLEAR_INTERVAL_MS - (now - lastLCDClearMs));
    }

    Wire.beginTransmission(0x27);
    if (Wire.endTransmission() != 0) {
        lcdNeedsFullReinit = true;
        fullLCDReinit();
        return;
    }
    lcd.clear();
    lastLCDClearMs = millis();
    delayMicroseconds(1700); // HD44780 clear command needs ~1.5ms
    invalidateLCDCache();
}

// Print text padded/truncated to exactly 'width' chars, then cache-write
void lcdPrintPadded(int col, int row, const char* text, int width) {
    if (lcdNeedsFullReinit || width <= 0 || col + width > 20) return;
    char buf[21];
    int i = 0;
    while (i < width && text[i]) { buf[i] = text[i]; i++; }
    while (i < width)             { buf[i] = ' ';     i++; }
    buf[width] = '\0';

    // For partial-row writes we fall through to direct lcd.print
    // because lcdRowWrite always writes from col 0.
    if (col == 0 && width == 20) {
        lcdRowWrite(row, buf);
    } else {
        if (lcdNeedsFullReinit) return;
        lcd.setCursor(col, row);
        lcd.print(buf);
        // Update the relevant portion of the cache
        for (int k = 0; k < width && (col + k) < 20; k++)
            lcdCache[row][col + k] = buf[k];
    }
}

// ============================================================
//  I2C / LCD EMI RECOVERY
// ============================================================
bool i2cBusCheck() {
    Wire.beginTransmission(0x27);
    return (Wire.endTransmission() == 0);
}

void i2cBusRecover() {
    LOG_W("I2C", "Bus recovery");
    Wire.end();
    delay(10);
    pinMode(I2C_SCL_PIN, OUTPUT);
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    for (int i = 0; i < 16; i++) {
        digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
        if (digitalRead(I2C_SDA_PIN) == HIGH) break;
    }
    // STOP condition
    pinMode(I2C_SDA_PIN, OUTPUT);
    digitalWrite(I2C_SDA_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
    digitalWrite(I2C_SDA_PIN, HIGH); delayMicroseconds(5);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(50000);
    delay(50);
    LOG_I("I2C", "Recovery done");
}

void initCustomChars() {
    // Load from PROGMEM into RAM buffer before writing
    uint8_t buf[8];
    memcpy_P(buf, charFilledBlock, 8); lcd.createChar(0, buf);
    memcpy_P(buf, charThermometer, 8); lcd.createChar(1, buf);
    memcpy_P(buf, charFan,         8); lcd.createChar(2, buf);
    memcpy_P(buf, charCheck,       8); lcd.createChar(3, buf);
}

void fullLCDReinit() {
    unsigned long now = millis();
    if ((now - lastLCDReinitMs) < LCD_REINIT_COOLDOWN_MS) return;
    lastLCDReinitMs = now;
    LOG_W("LCD", "Full reinit");

    i2cBusRecover();
    delay(150);

    lcd.init();
    delay(80);

    // Reinit twice: first init often doesn't fully recover a corrupted LCD
    lcd.init();
    delay(50);

    if (isBacklightOn) lcd.backlight();
    else               lcd.noBacklight();

    initCustomChars();

    // Wait for custom char programming to settle before clearing
    delay(20);
    lcd.clear();
    delayMicroseconds(1700);

    lcdNeedsFullReinit = false;
    lcdErrorCount      = 0;
    lastLCDRefreshMs   = millis();
    lastLCDClearMs     = millis();
    invalidateLCDCache();
    LOG_I("LCD", "Reinit complete");
}

void lcdRecover() {
    unsigned long now = millis();
    if ((now - lastLCDRecoverMs) < LCD_RECOVER_INTERVAL_MS) return;
    lastLCDRecoverMs = now;
    if (!i2cBusCheck()) {
        lcdErrorCount++;
        LOG_W("LCD", "I2C check failed");
        if (lcdErrorCount >= 2) fullLCDReinit();
        else                    i2cBusRecover();
    } else {
        if (lcdErrorCount > 0) { lcdErrorCount = 0; LOG_D("LCD","I2C OK"); }
    }
}

// ============================================================
//  PROGRESS BAR
// ============================================================
void updateProgressBar(int row, float pct) {
    if (lcdNeedsFullReinit) return;
    const int W = 18;
    pct = constrain(pct, 0.0f, 100.0f);
    int filled = (int)((pct / 100.0f) * W);
    char buf[21];
    buf[0] = '[';
    for (int i = 0; i < W; i++) buf[1+i] = (i < filled) ? '\x00' : '-';
    buf[W+1] = ']';
    buf[W+2] = '\0';
    lcdRowWrite(row, buf);
}

// ============================================================
//  PERIODIC FULL REFRESH
// ============================================================
void forceRedrawCurrentScreen() {
    LOG_D("LCD","Periodic refresh");
    invalidateLCDCache();
    lcd.clear();
    delay(5);
    initCustomChars();
    delay(5);

    switch (currentState) {
        case STATE_MAIN_MENU:       displayMainMenu();                    break;
        case STATE_SELECT_PROGRAM:  displaySelectProgram();               break;
        case STATE_SETTINGS:
            settingsCtx.needsRedraw = true;
            displaySettingsPage();
            break;
        case STATE_CALIBRATE_TEMP:  displayCalibratePage();               break;
        case STATE_CUSTOM_PROGRAM:  displayCustomPage();                  break;
        case STATE_SYSTEM_STATUS:   sysStatusCtx.lastDrawMs = 0;          break;
        case STATE_RESUME_PROMPT:   resumeCtx.needsRedraw   = true;       break;
        case STATE_PAUSED:
            lcdRowWrite(0, "! PAUSED !          ");
            lcdRowWrite(1, "Door Open           ");
            lcdRowWrite(2, "Close door to resume");
            lcdRowWrite(3, "STOP=Abort          ");
            break;
        case STATE_SENSOR_INIT:
            lcdRowWrite(0, "Checking sensor...  ");
            break;
        default: break;
    }
    lastLCDRefreshMs = millis();
}

void lcdPeriodicRefresh() {
    if ((millis() - lastLCDRefreshMs) < LCD_REFRESH_INTERVAL_MS) return;
    if (!i2cBusCheck()) { lcdNeedsFullReinit = true; return; }
    forceRedrawCurrentScreen();
}

// ============================================================
//  DRUM MOTOR
// ============================================================
void requestDrumForward(DryingContext& ctx) {
    if (ctx.drumState == DRUM_FORWARD && ctx.ilStep == IL_IDLE) return;
    stopDrumMotor();
    ctx.ilStep          = IL_WAIT;
    ctx.ilStartMs       = millis();
    ctx.ilTargetForward = true;
    ctx.drumState       = DRUM_INTERLOCK;
}

void requestDrumReverse(DryingContext& ctx) {
    if (ctx.drumState == DRUM_REVERSE && ctx.ilStep == IL_IDLE) return;
    stopDrumMotor();
    ctx.ilStep          = IL_WAIT;
    ctx.ilStartMs       = millis();
    ctx.ilTargetForward = false;
    ctx.drumState       = DRUM_INTERLOCK;
}

void updateDrumInterlock(DryingContext& ctx) {
    if (ctx.ilStep != IL_WAIT) return;
    if ((millis() - ctx.ilStartMs) < RELAY_INTERLOCK_DELAY_MS) return;

    // Both relay queues must show LOW (off)
    int fi = relayIdx(RELAY_DRUM_FORWARD);
    int ri = relayIdx(RELAY_DRUM_REVERSE);
    bool fwdOff = (relays[fi].actualState == LOW);
    bool revOff = (relays[ri].actualState == LOW);

    if (!fwdOff || !revOff) {
        // Relays may still be settling — wait another cycle
        // Only fault after extended wait
        if ((millis() - ctx.ilStartMs) > (RELAY_INTERLOCK_DELAY_MS * 5)) {
            LOG_E("DRUM", "Interlock timeout — FAULT");
            setFault(FAULT_RELAY_FAIL);
            enterState(STATE_ABORT);
        }
        return;
    }

    if (ctx.ilTargetForward) {
        commandRelay(RELAY_DRUM_FORWARD, HIGH);
        ctx.drumState = DRUM_FORWARD;
    } else {
        commandRelay(RELAY_DRUM_REVERSE, HIGH);
        ctx.drumState = DRUM_REVERSE;
    }
    ctx.ilStep           = IL_IDLE;
    ctx.drumStateStartMs = millis();
}

void updateDrumMotor(DryingContext& ctx) {
    unsigned long now     = millis();
    unsigned long elapsed = now - ctx.drumStateStartMs;

    updateDrumInterlock(ctx);
    if (ctx.ilStep == IL_WAIT) return;

    switch (ctx.drumState) {
        case DRUM_STOPPED:
            requestDrumForward(ctx);
            ctx.drumLastWasForward = true;
            break;
        case DRUM_FORWARD:
            if (elapsed >= DRUM_FORWARD_DURATION_MS) {
                stopDrumMotor();
                ctx.drumState = DRUM_PAUSED;
                ctx.drumStateStartMs = now;
            }
            break;
        case DRUM_PAUSED:
            if (elapsed >= DRUM_PAUSE_DURATION_MS) {
                if (ctx.drumLastWasForward) {
                    requestDrumReverse(ctx);
                    ctx.drumLastWasForward = false;
                } else {
                    requestDrumForward(ctx);
                    ctx.drumLastWasForward = true;
                }
            }
            break;
        case DRUM_REVERSE:
            if (elapsed >= DRUM_REVERSE_DURATION_MS) {
                stopDrumMotor();
                ctx.drumState = DRUM_PAUSED;
                ctx.drumStateStartMs = now;
            }
            break;
        case DRUM_INTERLOCK: break;
    }
}

// ============================================================
//  PROGRAM VALIDATION
// ============================================================
void validatePrograms() {
    for (int i = 0; i < NUM_PROGRAMS; i++) {
        if (programs[i].temperature > PROGRAM_MAX_TEMPERATURE)
            programs[i].temperature = PROGRAM_MAX_TEMPERATURE;
    }
}

// ============================================================
//  EEPROM
// ============================================================
int eepromSlotAddress(int slot) {
    return EEPROM_BASE_ADDRESS + slot * (int)sizeof(SavedState);
}

void findNewestEEPROMSlot() {
    unsigned long newest = 0;
    currentEEPROMSlot = 0;
    for (int i = 0; i < EEPROM_NUM_SLOTS; i++) {
        SavedState s;
        EEPROM.get(eepromSlotAddress(i), s);
        if (s.inProgress && calculateChecksum(s) == s.checksum
            && s.elapsedTime > newest) {
            newest = s.elapsedTime;
            currentEEPROMSlot = i;
        }
    }
}

void saveStateToEEPROM() {
    if (currentState != STATE_DRYING && currentState != STATE_PAUSED) return;
    currentEEPROMSlot = (currentEEPROMSlot + 1) % EEPROM_NUM_SLOTS;
    SavedState s;
    s.inProgress           = true;
    s.selectedProgramIndex = selectedProgram;
    s.elapsedTime          = elapsedDryingTime;
    s.temperatureOffset    = temperatureOffset;
    s.checksum             = calculateChecksum(s);
    EEPROM.put(eepromSlotAddress(currentEEPROMSlot), s);
    EEPROM.commit();
    LOG_D("EEPROM","Saved");
}

bool loadStateFromEEPROM() {
    findNewestEEPROMSlot();
    SavedState s;
    EEPROM.get(eepromSlotAddress(currentEEPROMSlot), s);
    if (!s.inProgress || calculateChecksum(s) != s.checksum) {
        LOG_W("EEPROM","Bad checksum"); return false;
    }
    if (s.selectedProgramIndex < 0 || s.selectedProgramIndex >= NUM_PROGRAMS
        || s.elapsedTime > (999UL * 60000UL)
        || isnan(s.temperatureOffset)
        || s.temperatureOffset < -10.0f || s.temperatureOffset > 10.0f) {
        LOG_W("EEPROM","Sanity fail"); return false;
    }
    selectedProgram   = s.selectedProgramIndex;
    elapsedDryingTime = s.elapsedTime;
    temperatureOffset = s.temperatureOffset;
    LOG_I("EEPROM","Loaded OK");
    return true;
}

void clearStateInEEPROM() {
    for (int i = 0; i < EEPROM_NUM_SLOTS; i++) {
        SavedState s;
        EEPROM.get(eepromSlotAddress(i), s);
        if (s.inProgress) {
            s.inProgress = false; s.elapsedTime = 0;
            s.selectedProgramIndex = 0;
            s.temperatureOffset = temperatureOffset;
            s.checksum = calculateChecksum(s);
            EEPROM.put(eepromSlotAddress(i), s);
        }
    }
    EEPROM.commit();
    LOG_I("EEPROM","Cleared");
}

bool checkSavedStateInEEPROM() {
    findNewestEEPROMSlot();
    SavedState s;
    EEPROM.get(eepromSlotAddress(currentEEPROMSlot), s);
    if (!s.inProgress || calculateChecksum(s) != s.checksum) return false;
    if (s.selectedProgramIndex < 0 || s.selectedProgramIndex >= NUM_PROGRAMS
        || s.elapsedTime > (999UL * 60000UL)) {
        clearStateInEEPROM(); return false;
    }
    return true;
}

// ============================================================
//  BUZZER
// ============================================================
void buzzNavigate()     { tone(BUZZER_PIN, BUZZ_FREQ_NAV,      BUZZ_DUR_SHORT);  }
void buzzSelect()       { tone(BUZZER_PIN, BUZZ_FREQ_SELECT,   BUZZ_DUR_MEDIUM); }
void buzzBack()         { tone(BUZZER_PIN, BUZZ_FREQ_BACK,     BUZZ_DUR_MEDIUM); }
void buzzAlert(int dur) { tone(BUZZER_PIN, BUZZ_FREQ_ALERT,    dur); }
void buzzError(int dur) { tone(BUZZER_PIN, BUZZ_FREQ_ERROR,    dur); }
void buzzComplete() {
    tone(BUZZER_PIN, BUZZ_FREQ_COMPLETE_1, BUZZ_DUR_COMPLETE); delay(350);
    esp_task_wdt_reset();
    tone(BUZZER_PIN, BUZZ_FREQ_COMPLETE_2, BUZZ_DUR_COMPLETE); delay(350);
    esp_task_wdt_reset();
    tone(BUZZER_PIN, BUZZ_FREQ_COMPLETE_1, BUZZ_DUR_COMPLETE);
}

// ============================================================
//  DISPLAY FUNCTIONS — all use lcdRowWrite for cache efficiency
// ============================================================
void displayMainMenu() {
    resetActivityTimer();
    char r1[21], r2[21], r3[21];
    snprintf(r1, 21, "%sStart Drying      ", menuIndex==0?"> ":"  ");
    snprintf(r2, 21, "%sSettings          ", menuIndex==1?"> ":"  ");
    snprintf(r3, 21, "%sSystem Status     ", menuIndex==2?"> ":"  ");
    lcdRowWrite(0, "** Main Menu **     ");
    lcdRowWrite(1, r1);
    lcdRowWrite(2, r2);
    lcdRowWrite(3, r3);
    lastLCDRefreshMs = millis();
}

void displaySelectProgram() {
    resetActivityTimer();
    char r1[21], r2[21];
    snprintf(r1, 21, "> %-18s", programs[selectedProgram].name);
    bool uncfg = (strcmp(programs[selectedProgram].name,"Custom")==0
                  && programs[selectedProgram].temperature==0);
    if (!uncfg)
        snprintf(r2, 21, "T:%3d%cC  Dur:%3dmin",
                 programs[selectedProgram].temperature, (char)223,
                 programs[selectedProgram].duration);
    else
        snprintf(r2, 21, "%-20s", "(Configure First)");
    lcdRowWrite(0, "Select Program:     ");
    lcdRowWrite(1, r1);
    lcdRowWrite(2, r2);
    lcdRowWrite(3, "SEL=Confirm BACK=   ");
    lastLCDRefreshMs = millis();
}

void displaySettingsPage() {
    char r1[21], r2[21];
    snprintf(r1, 21, "%sCalibrate         ", settingsCtx.idx==0?"> ":"  ");
    snprintf(r2, 21, "%sBack              ", settingsCtx.idx==1?"> ":"  ");
    lcdRowWrite(0, "Settings            ");
    lcdRowWrite(1, r1);
    lcdRowWrite(2, r2);
    lcdRowWrite(3, "                    ");
    settingsCtx.needsRedraw = false;
    lastLCDRefreshMs = millis();
}

void displayCalibratePage() {
    char r1[21];
    snprintf(r1, 21, "Offset: %+5.1f%cC     ", temperatureOffset, (char)223);
    lcdRowWrite(0, "Calibrate Temp:     ");
    lcdRowWrite(1, r1);
    lcdRowWrite(2, "UP/DOWN to adjust   ");
    lcdRowWrite(3, "SEL=Save BACK=Exit  ");
    lastLCDRefreshMs = millis();
}

void displayCustomPage() {
    char r1[21], r2[21], r3[21];
    snprintf(r1, 21, "%sTemp: %3d%cC      ",
             customCtx.stage==0?"> ":"  ", customCtx.tempVal, (char)223);
    snprintf(r2, 21, "%sTime: %3d min    ",
             customCtx.stage==1?"> ":"  ", customCtx.durationVal);
    snprintf(r3, 21, "%-20s",
             customCtx.stage==0?"SEL->Set Time":"SEL->Start Drying");
    lcdRowWrite(0, "Adjust Custom Prog  ");
    lcdRowWrite(1, r1);
    lcdRowWrite(2, r2);
    lcdRowWrite(3, r3);
    lastLCDRefreshMs = millis();
}

// ============================================================
//  STATE TRANSITION — centralised init
// ============================================================
void enterState(State next) {
    currentState = next;
    switch (next) {
        case STATE_MAIN_MENU:
            menuIndex = 0;
            displayMainMenu();
            break;
        case STATE_SELECT_PROGRAM:
            selectedProgram = 0;
            displaySelectProgram();
            break;
        case STATE_SETTINGS:
            settingsCtx.idx = 0; settingsCtx.needsRedraw = true;
            displaySettingsPage();
            break;
        case STATE_CALIBRATE_TEMP:
            calibrateCtx.savedOffset = temperatureOffset;
            displayCalibratePage();
            break;
        case STATE_CUSTOM_PROGRAM:
            customCtx.stage = 0;
            customCtx.tempVal = programs[NUM_PROGRAMS-1].temperature > 0
                                ? programs[NUM_PROGRAMS-1].temperature : 60;
            customCtx.durationVal = programs[NUM_PROGRAMS-1].duration > 0
                                    ? programs[NUM_PROGRAMS-1].duration : 40;
            displayCustomPage();
            break;
        case STATE_RESUME_PROMPT:
            resumeCtx.selection   = 0;
            resumeCtx.entryTimeMs = millis();
            resumeCtx.needsRedraw = true;
            break;
        case STATE_SYSTEM_STATUS:
            sysStatusCtx = {NAN, 0, 0, false, true, 0};
            tempSensor.setWaitForConversion(false);
            break;
        case STATE_DRYING:
            dryingInitialized = false;
            break;
        case STATE_ABORT:
            abortEntryMs   = millis();
            transientDrawn = false;
            break;
        case STATE_COMPLETE:
            completeEntryMs = millis();
            transientDrawn  = false;
            break;
        case STATE_COOL_DOWN:
            coolDownStartTime = millis();
            invalidateLCDCache();
            break;
        case STATE_SENSOR_INIT:
            sensorInitCtx = {0, 0, 0, false, 0, true};
            lcdRowWrite(0, "Checking sensor...  ");
            lcdRowWrite(1, "                    ");
            lcdRowWrite(2, "                    ");
            lcdRowWrite(3, "                    ");
            tempSensor.setWaitForConversion(false);
            break;
        default: break;
    }
}

// ============================================================
//  NON-BLOCKING SENSOR VALIDATION
//
//  In v6.0, validateSensorAtStartup() used a blocking while loop
//  with delay(900) per attempt. With 3 reads needed and up to
//  9 attempts, this blocked for up to 8.1 seconds — far exceeding
//  the 10-second WDT timeout if anything went wrong mid-loop.
//
//  Fix: sensor validation is now STATE_SENSOR_INIT, handled in
//  the main loop like every other state. Each conversion is
//  requested and read asynchronously using millis() timing.
// ============================================================
void handleSensorInit() {
    esp_task_wdt_reset();
    unsigned long now = millis();
    const int maxAttempts = SENSOR_STARTUP_READS * 4;

    // Start first conversion
    if (!sensorInitCtx.convPending) {
        if (sensorInitCtx.attempts >= maxAttempts) {
            // Failed to get enough valid reads
            LOG_E("SENSOR","Validation FAILED");
            setFault(FAULT_NO_SENSOR);
            sensorValidated = false;
            lcdSafeClear();
            lcdRowWrite(0, "! SENSOR FAULT !    ");
            lcdRowWrite(1, "No DS18B20 found    ");
            lcdRowWrite(2, "Check wiring        ");
            lcdRowWrite(3, "Drying DISABLED     ");
            buzzError(BUZZ_DUR_LONG);
            // Go to main menu — drying will be blocked
            delay(3000);
            esp_task_wdt_reset();
            if (checkSavedStateInEEPROM()) enterState(STATE_RESUME_PROMPT);
            else                           enterState(STATE_MAIN_MENU);
            return;
        }
        tempSensor.requestTemperatures();
        sensorInitCtx.convStartMs   = now;
        sensorInitCtx.convPending   = true;
        sensorInitCtx.attempts++;
        return;
    }

    // Wait for conversion
    if ((now - sensorInitCtx.convStartMs) < TEMP_CONVERSION_MS) return;

    float raw = tempSensor.getTempCByIndex(0);
    sensorInitCtx.convPending = false;

    if (raw != DEVICE_DISCONNECTED_C && raw > -50.0f && raw < 150.0f) {
        sensorInitCtx.validCount++;
        char buf[LOG_BUF_SIZE];
        snprintf(buf, LOG_BUF_SIZE, "Valid read %d/%d: %.1fC",
                 sensorInitCtx.validCount, SENSOR_STARTUP_READS, raw);
        LOG_I("SENSOR", buf);

        // Update display with progress
        char row[21];
        snprintf(row, 21, "Valid: %d/%d  %.1fC   ",
                 sensorInitCtx.validCount, SENSOR_STARTUP_READS, raw);
        lcdRowWrite(1, row);
    } else {
        // Bad read — reset count, require consecutive reads
        sensorInitCtx.validCount = 0;
        LOG_W("SENSOR","Bad read — resetting count");
        lcdRowWrite(1, "Bad read - retrying ");
    }

    if (sensorInitCtx.validCount >= SENSOR_STARTUP_READS) {
        LOG_I("SENSOR","Validation OK");
        sensorValidated = true;
        lcdRowWrite(0, "Sensor OK           ");
        lcdRowWrite(1, "                    ");
        delay(800);
        esp_task_wdt_reset();
        // Now do the rest of boot
        if (checkSavedStateInEEPROM()) enterState(STATE_RESUME_PROMPT);
        else {
            lcdSafeClear();
            lcdRowWrite(0, "Yohannes Automation");
            lcdRowWrite(1, "& Electromechanical");
            lcdRowWrite(2, "     Service       ");
            lcdRowWrite(3, "  ***094427623**** ");
            buzzSelect();
            delay(3000);
            esp_task_wdt_reset();
            enterState(STATE_MAIN_MENU);
        }
    }
}

// ============================================================
//  initDrying — non-blocking
// ============================================================
void initDrying(bool resume) {
    LOG_I("DRYING", resume ? "Resuming" : "Fresh start");
    clearAllFaults();
    resetActivityTimer();

    if (!sensorValidated) {
        lcdSafeClear();
        lcdRowWrite(1, "No Temp Sensor!     ");
        lcdRowWrite(2, "Cannot start cycle  ");
        buzzError(BUZZ_DUR_LONG);
        delay(2500); esp_task_wdt_reset();
        enterState(STATE_MAIN_MENU); return;
    }

    if (resume) {
        if (!loadStateFromEEPROM()) {
            elapsedDryingTime = 0;
            clearStateInEEPROM();
            lcdSafeClear();
            lcdRowWrite(1, "Resume failed!      ");
            lcdRowWrite(2, "Starting fresh...   ");
            buzzError(BUZZ_DUR_MEDIUM);
            delay(2000); esp_task_wdt_reset();
            resume = false;
        }
    } else {
        elapsedDryingTime = 0;
    }

    // Non-blocking door check
    if (!isDoorClosed()) {
        setFault(FAULT_DOOR_OPEN);
        lcdSafeClear();
        lcdRowWrite(0, "! Door Open !       ");
        lcdRowWrite(1, "Please close door   ");
        lcdRowWrite(2, "to start drying     ");
        lcdRowWrite(3, "STOP=Cancel         ");
        buzzAlert(BUZZ_DUR_ALERT);
        if (resume) saveStateToEEPROM();
        currentState = STATE_PAUSED; return;
    }

    // Start fan
    commandRelay(RELAY_FAN, HIGH);

    unsigned long now = millis();
    dryCtx.programDurationMs    = (unsigned long)programs[selectedProgram].duration * 60000UL;
    dryCtx.cycleStartTime       = now - elapsedDryingTime;
    dryCtx.endTime              = dryCtx.cycleStartTime + dryCtx.programDurationMs;
    dryCtx.heaterEnableTime     = now + HEATER_DELAY_MS;
    dryCtx.overheatStartMs      = 0;
    dryCtx.lastTempReadTime     = now - TEMP_READ_INTERVAL_MS;
    dryCtx.lastEepromUpdateTime = now;
    dryCtx.lastRelayReadbackMs  = now;
    dryCtx.lastGoodTemp         = NAN;
    dryCtx.displayErrorCount    = 0;
    dryCtx.tempReadErrorCount   = 0;
    dryCtx.heaterOn             = false;
    dryCtx.heaterCanRun         = false;
    dryCtx.airflowConfirmedOK   = false;
    dryCtx.airflowError         = false;
    dryCtx.timeOverheatFault    = false;
    dryCtx.drumState            = DRUM_STOPPED;
    dryCtx.drumStateStartMs     = now;
    dryCtx.drumLastWasForward   = true;
    dryCtx.ilStep               = IL_IDLE;
    dryCtx.tempRequestMs        = 0;
    dryCtx.tempConversionPending= false;

    tempSensor.setWaitForConversion(false);
    tempSensor.requestTemperatures();
    dryCtx.tempConversionPending = true;
    dryCtx.tempRequestMs         = now;

    if (!resume) saveStateToEEPROM();
    invalidateLCDCache();
    lastLCDRefreshMs  = millis();
    dryingInitialized = true;
    LOG_I("DRYING","Initialized");
}

// ============================================================
//  updateDrying
// ============================================================
void updateDrying() {
    unsigned long now = millis();
    resetActivityTimer();
    esp_task_wdt_reset();

    // Process relay queue first (non-blocking)
    processRelayQueue();

    lcdRecover();
    lcdPeriodicRefresh();
    verifyRelayStates(dryCtx);
    updateDrumMotor(dryCtx);

    // Door check
    if (!isDoorClosed()) {
        setFault(FAULT_DOOR_OPEN);
        forceRelayOff(RELAY_HEATER);
        dryCtx.heaterOn = false;
        commandRelay(RELAY_FAN, LOW);
        stopDrumMotor();
        processRelayQueue();
        buzzAlert(BUZZ_DUR_ALERT);
        elapsedDryingTime = now - dryCtx.cycleStartTime;
        saveStateToEEPROM();
        currentState = STATE_PAUSED;
        lcdSafeClear();
        return;
    }

    // Abort buttons
    if (isButtonPressed(BUTTON_STOP) || isButtonPressed(BUTTON_BACK)) {
        enterState(STATE_ABORT); return;
    }

    // Async temperature reading
    float freshTemp = NAN;
    if (!dryCtx.tempConversionPending
        && (now - dryCtx.lastTempReadTime >= TEMP_READ_INTERVAL_MS)) {
        tempSensor.requestTemperatures();
        dryCtx.tempConversionPending = true;
        dryCtx.tempRequestMs         = now;
    }
    if (dryCtx.tempConversionPending
        && (now - dryCtx.tempRequestMs >= TEMP_CONVERSION_MS)) {
        float raw = tempSensor.getTempCByIndex(0);
        dryCtx.tempConversionPending = false;
        dryCtx.lastTempReadTime      = now;
        if (raw != DEVICE_DISCONNECTED_C && raw > -50.0f && raw < 150.0f) {
            freshTemp                 = raw + temperatureOffset;
            dryCtx.lastGoodTemp       = freshTemp;
            dryCtx.displayErrorCount  = 0;
            dryCtx.tempReadErrorCount = 0;
            clearFault(FAULT_TEMP_SENSOR);
        } else {
            dryCtx.tempReadErrorCount++;
            dryCtx.displayErrorCount++;
            if (dryCtx.tempReadErrorCount >= MAX_TEMP_READ_ERRORS_HEATER)
                setFault(FAULT_TEMP_SENSOR);
        }
    }

    float tempToShow = (!isnan(dryCtx.lastGoodTemp)
                        && dryCtx.displayErrorCount < MAX_DISPLAY_TEMP_ERRORS)
                       ? dryCtx.lastGoodTemp : NAN;
    float tempForControl = isnan(freshTemp) ? dryCtx.lastGoodTemp : freshTemp;

    elapsedDryingTime = now - dryCtx.cycleStartTime;
    unsigned long remainMs = (dryCtx.endTime > now) ? (dryCtx.endTime - now) : 0UL;
    unsigned int remMin = (unsigned int)(remainMs / 60000UL);
    unsigned int remSec = (unsigned int)((remainMs / 1000UL) % 60UL);

    // ---- Build display rows using cache (min I2C writes) ----
    {
        char r0[21], r1[21], r2[21], r3[21];

        // Row 0: target and measured temperature
        if (isnan(tempToShow))
            snprintf(r0, 21, "T:%3d%cC  Now:---  %c",
                     programs[selectedProgram].temperature, (char)223, (char)223);
        else
            snprintf(r0, 21, "T:%3d%cC  N:%5.1f%cC",
                     programs[selectedProgram].temperature, (char)223,
                     tempToShow, (char)223);

        // Row 1: heater / fan / drum
        const char* ds = "---";
        switch (dryCtx.drumState) {
            case DRUM_FORWARD:   ds = "FWD"; break;
            case DRUM_REVERSE:   ds = "REV"; break;
            case DRUM_PAUSED:    ds = "PAU"; break;
            case DRUM_INTERLOCK: ds = "ILK"; break;
            default: break;
        }
        snprintf(r1, 21, "H:%-3s  F:ON  D:%-3s ",
                 dryCtx.heaterOn ? "ON" : "OFF", ds);

        // Row 2: remaining time
        snprintf(r2, 21, "Rem: %02um %02us          ", remMin, remSec);

        // Row 3: status
        if (dryCtx.timeOverheatFault)
            snprintf(r3, 21, "%-20s", "! TIME OVERHEAT !  ");
        else if (hasFault(FAULT_TEMP_SENSOR))
            snprintf(r3, 21, "%-20s", "! TEMP SENSOR ERR !");
        else if (hasFault(FAULT_RELAY_FAIL))
            snprintf(r3, 21, "%-20s", "! RELAY FAIL !     ");
        else if (dryCtx.airflowError)
            snprintf(r3, 21, "%-20s", "! AIRFLOW FAULT !  ");
        else if (!dryCtx.heaterCanRun) {
            unsigned long pr = (dryCtx.heaterEnableTime > now)
                               ? (dryCtx.heaterEnableTime - now) / 1000UL : 0;
            snprintf(r3, 21, "Purge: %3lus           ", pr);
        } else {
            // Progress bar — handled separately, skip cache for row 3
            float pct = dryCtx.programDurationMs > 0
                        ? ((float)elapsedDryingTime /
                           (float)dryCtx.programDurationMs * 100.0f) : 0.0f;
            updateProgressBar(3, pct);
            // updateProgressBar writes via lcdRowWrite — skip snprintf path
            r3[0] = '\0'; // signal to skip
        }

        lcdRowWrite(0, r0);
        lcdRowWrite(1, r1);
        lcdRowWrite(2, r2);
        if (r3[0] != '\0') lcdRowWrite(3, r3);
    }

    // ---- Airflow + heater enable gate ----
    bool timerReady   = (now >= dryCtx.heaterEnableTime);
    bool airflowReady = isAirflowOK();
    dryCtx.airflowConfirmedOK = airflowReady;

    if (!dryCtx.heaterCanRun) {
        if (timerReady && airflowReady) {
            dryCtx.heaterCanRun = true;
            LOG_I("DRYING","Heater enabled: timer + airflow OK");
        }
        if (timerReady && !airflowReady && !dryCtx.airflowError) {
            dryCtx.airflowError = true;
            setFault(FAULT_AIRFLOW);
            LOG_E("DRYING","Airflow absent after purge");
            buzzAlert(BUZZ_DUR_ALERT);
        }
    }

    if (dryCtx.heaterCanRun) {
        if (!airflowReady && !dryCtx.airflowError) {
            dryCtx.airflowError = true;
            setFault(FAULT_AIRFLOW);
            forceRelayOff(RELAY_HEATER);
            dryCtx.heaterOn = false;
            buzzAlert(BUZZ_DUR_ALERT);
            LOG_E("DRYING","Airflow lost");
        } else if (airflowReady && dryCtx.airflowError) {
            dryCtx.airflowError = false;
            clearFault(FAULT_AIRFLOW);
            LOG_I("DRYING","Airflow restored");
        }
    }

    // ---- Force-off conditions ----
    bool forceOff = dryCtx.timeOverheatFault
                 || hasFault(FAULT_TEMP_SENSOR)
                 || hasFault(FAULT_RELAY_FAIL)
                 || !dryCtx.heaterCanRun
                 || dryCtx.airflowError;

    if (!forceOff && !isnan(tempForControl)
        && tempForControl > (float)MAX_TEMPERATURE) {
        forceOff = true;
        setFault(FAULT_MAX_TEMP);
        LOG_E("DRYING","Absolute max temp exceeded");
        buzzError(BUZZ_DUR_LONG);
    }

    // ---- Heater control — corrected hysteresis ----
    //  Target=60°C, HYSTERESIS=3:
    //    ON  when temp < 57°C   (lower band)
    //    OFF when temp >= 60°C  (setpoint)
    //    Dead band 57–60°C: hold current state
    if (forceOff) {
        if (dryCtx.heaterOn) {
            forceRelayOff(RELAY_HEATER);
            dryCtx.heaterOn = false;
        }
    } else if (!isnan(tempForControl)) {
        float target    = (float)programs[selectedProgram].temperature;
        float lowerBand = target - (float)HEATER_HYSTERESIS;
        if (tempForControl < lowerBand && !dryCtx.heaterOn) {
            commandRelay(RELAY_HEATER, HIGH);
            dryCtx.heaterOn = true;
            LOG_I("DRYING","Heater ON");
        } else if (tempForControl >= target && dryCtx.heaterOn) {
            commandRelay(RELAY_HEATER, LOW);
            dryCtx.heaterOn = false;
            LOG_I("DRYING","Heater OFF at setpoint");
        }
        // Between lowerBand and target: maintain state (dead band)
    }

    // ---- Overheat duration fault ----
    if (!isnan(tempForControl) && !dryCtx.timeOverheatFault) {
        float thresh = (float)(programs[selectedProgram].temperature
                               + OVERHEAT_THRESHOLD_OFFSET);
        if (tempForControl > thresh) {
            if (dryCtx.overheatStartMs == 0) dryCtx.overheatStartMs = now;
            else if ((now - dryCtx.overheatStartMs)
                     > (MAX_OVERHEAT_DURATION_SECONDS * 1000UL)) {
                dryCtx.timeOverheatFault = true;
                setFault(FAULT_OVERHEAT_TIME);
                forceRelayOff(RELAY_HEATER);
                dryCtx.heaterOn = false;
                buzzError(BUZZ_DUR_ALERT);
            }
        } else {
            dryCtx.overheatStartMs = 0;
        }
    }

    // ---- Periodic EEPROM save ----
    if (now - dryCtx.lastEepromUpdateTime >= EEPROM_UPDATE_INTERVAL_MS) {
        saveStateToEEPROM();
        dryCtx.lastEepromUpdateTime = now;
    }

    // ---- Cycle complete ----
    if (now >= dryCtx.endTime && !dryCtx.timeOverheatFault) {
        forceRelayOff(RELAY_HEATER);
        dryCtx.heaterOn = false;
        stopDrumMotor();
        processRelayQueue();
        clearStateInEEPROM();
        tempSensor.setWaitForConversion(true);
        dryingInitialized = false;
        LOG_I("DRYING","Cycle complete");
        buzzSelect();
        enterState(STATE_COOL_DOWN);
    }
}

// ============================================================
//  handleCoolDown
// ============================================================
void handleCoolDown() {
    esp_task_wdt_reset();
    resetActivityTimer();
    processRelayQueue();
    lcdRecover();
    lcdPeriodicRefresh();

    unsigned long now     = millis();
    unsigned long elapsed = now - coolDownStartTime;
    unsigned long remain  = (elapsed < COOL_DOWN_DURATION_MS)
                            ? (COOL_DOWN_DURATION_MS - elapsed) : 0UL;

    forceRelayOff(RELAY_HEATER);
    commandRelay(RELAY_FAN, HIGH);
    stopDrumMotor();

    char r2[21];
    snprintf(r2, 21, "Rem: %02lum %02lus       ",
             remain / 60000UL, (remain / 1000UL) % 60UL);
    lcdRowWrite(0, "Cooling Down        ");
    lcdRowWrite(1, "Fan ON  Heater OFF  ");
    lcdRowWrite(2, r2);

    float pct = (float)elapsed / (float)COOL_DOWN_DURATION_MS * 100.0f;
    updateProgressBar(3, pct);

    if (now >= coolDownStartTime + COOL_DOWN_DURATION_MS) {
        enterState(STATE_COMPLETE); return;
    }
    if (isButtonPressed(BUTTON_STOP) || isButtonPressed(BUTTON_BACK)) {
        enterState(STATE_ABORT);
    }
}

// ============================================================
//  handlePaused
// ============================================================
void handlePaused() {
    esp_task_wdt_reset();
    resetActivityTimer();
    processRelayQueue();
    lcdRecover();
    lcdPeriodicRefresh();

    lcdRowWrite(0, "! PAUSED !          ");
    lcdRowWrite(1, "Door Open           ");
    lcdRowWrite(2, "Close door to resume");
    lcdRowWrite(3, "STOP=Abort          ");

    if (isDoorClosed()) {
        clearFault(FAULT_DOOR_OPEN);
        lcdSafeClear();
        lcdRowWrite(1, "Door closed.        ");
        lcdRowWrite(2, "Resuming...         ");
        buzzSelect();
        // Brief non-blocking-friendly pause
        unsigned long t = millis();
        while (millis() - t < 1200) {
            esp_task_wdt_reset();
            delay(50);
        }

        unsigned long now = millis();
        dryCtx.heaterEnableTime      = now + HEATER_DELAY_MS;
        dryCtx.lastEepromUpdateTime  = now;
        dryCtx.lastTempReadTime      = now - TEMP_READ_INTERVAL_MS;
        dryCtx.lastRelayReadbackMs   = now;
        dryCtx.tempReadErrorCount    = 0;
        dryCtx.displayErrorCount     = 0;
        dryCtx.lastGoodTemp          = NAN;
        dryCtx.overheatStartMs       = 0;
        dryCtx.timeOverheatFault     = false;
        dryCtx.airflowError          = false;
        dryCtx.airflowConfirmedOK    = false;
        dryCtx.heaterCanRun          = false;
        dryCtx.cycleStartTime        = now - elapsedDryingTime;
        dryCtx.endTime               = dryCtx.cycleStartTime + dryCtx.programDurationMs;

        commandRelay(RELAY_FAN, HIGH);
        dryCtx.drumState        = DRUM_STOPPED;
        dryCtx.drumStateStartMs = now;
        dryCtx.ilStep           = IL_IDLE;

        tempSensor.setWaitForConversion(false);
        tempSensor.requestTemperatures();
        dryCtx.tempConversionPending = true;
        dryCtx.tempRequestMs         = now;

        invalidateLCDCache();
        lastLCDRefreshMs  = millis();
        currentState      = STATE_DRYING;
        dryingInitialized = true;
        return;
    }

    if (isButtonPressed(BUTTON_BACK) || isButtonPressed(BUTTON_STOP)) {
        enterState(STATE_ABORT);
        buzzError(BUZZ_DUR_LONG);
    }
}

// ============================================================
//  handleResumePrompt — non-blocking with timeout
// ============================================================
void handleResumePrompt() {
    esp_task_wdt_reset();

    if (resumeCtx.needsRedraw) {
        lcdSafeClear();
        lcdRowWrite(0, "Resume last cycle?  ");
        lcdRowWrite(1, "                    ");
        char r2[21], r3[21];
        snprintf(r2, 21, "%sYes               ", resumeCtx.selection==0?"> ":"  ");
        snprintf(r3, 21, "%sNo                ", resumeCtx.selection==1?"> ":"  ");
        lcdRowWrite(2, r2); lcdRowWrite(3, r3);
        resumeCtx.needsRedraw = false;
    }

    // Show countdown on row 1
    unsigned long remaining = RESUME_PROMPT_TIMEOUT_MS
                              - min((millis()-resumeCtx.entryTimeMs),
                                    RESUME_PROMPT_TIMEOUT_MS);
    char rt[21];
    snprintf(rt, 21, "Auto-No in: %2lus     ", remaining / 1000UL);
    lcdRowWrite(1, rt);

    if ((millis() - resumeCtx.entryTimeMs) >= RESUME_PROMPT_TIMEOUT_MS) {
        LOG_I("RESUME","Timed out — declining");
        clearStateInEEPROM(); buzzBack();
        enterState(STATE_MAIN_MENU); return;
    }

    if (isButtonPressed(BUTTON_UP) || isButtonPressed(BUTTON_DOWN)) {
        resumeCtx.selection   = 1 - resumeCtx.selection;
        resumeCtx.needsRedraw = true;
        buzzNavigate();
    }
    if (isButtonPressed(BUTTON_SELECT)) {
        buzzSelect();
        if (resumeCtx.selection == 0) {
            resumeRequested = true;
            enterState(STATE_DRYING);
        } else {
            clearStateInEEPROM();
            enterState(STATE_MAIN_MENU);
        }
    }
    if (isButtonPressed(BUTTON_BACK) || isButtonPressed(BUTTON_STOP)) {
        buzzBack(); clearStateInEEPROM(); enterState(STATE_MAIN_MENU);
    }
}

// ============================================================
//  updateSystemStatus
// ============================================================
void updateSystemStatus() {
    esp_task_wdt_reset();
    resetActivityTimer();
    processRelayQueue();
    lcdRecover();
    lcdPeriodicRefresh();
    unsigned long now = millis();

    if (sysStatusCtx.firstEntry) {
        tempSensor.requestTemperatures();
        sysStatusCtx.reqMs = now; sysStatusCtx.convPending = true;
        sysStatusCtx.firstEntry = false;
    }
    if (sysStatusCtx.convPending && (now-sysStatusCtx.reqMs) >= TEMP_CONVERSION_MS) {
        float raw = tempSensor.getTempCByIndex(0);
        sysStatusCtx.convPending = false;
        if (raw != DEVICE_DISCONNECTED_C && raw > -50.0f && raw < 150.0f) {
            sysStatusCtx.lastTemp = raw + temperatureOffset;
            sysStatusCtx.errCount = 0;
        } else {
            if (++sysStatusCtx.errCount >= MAX_DISPLAY_TEMP_ERRORS)
                sysStatusCtx.lastTemp = NAN;
        }
        tempSensor.requestTemperatures();
        sysStatusCtx.reqMs = millis(); sysStatusCtx.convPending = true;
    }
    if ((now - sysStatusCtx.lastDrawMs) >= 1000UL) {
        sysStatusCtx.lastDrawMs = now;
        char r1[21], r2[21], r3[21];
        if (isnan(sysStatusCtx.lastTemp))
            snprintf(r1, 21, "Temp: N/A           ");
        else
            snprintf(r1, 21, "Temp: %6.1f%cC     ", sysStatusCtx.lastTemp, (char)223);
        snprintf(r2, 21, "Air:%-4s Door:%-4s  ",
                 isAirflowOK()?"OK":"FAIL", isDoorClosed()?"CLSD":"OPEN");
        snprintf(r3, 21, "Faults: 0x%02X        ", faultHistory);
        lcdRowWrite(0, "System Status       ");
        lcdRowWrite(1, r1); lcdRowWrite(2, r2); lcdRowWrite(3, r3);
    }
    if (isButtonPressed(BUTTON_BACK) || isButtonPressed(BUTTON_STOP)) {
        tempSensor.setWaitForConversion(true);
        buzzBack(); enterState(STATE_MAIN_MENU);
    }
}

// ============================================================
//  handleAbort — non-blocking
// ============================================================
void handleAbort() {
    if (!transientDrawn) {
        forceRelayOff(RELAY_HEATER);
        forceRelayOff(RELAY_FAN);
        forceRelayOff(RELAY_DRUM_FORWARD);
        forceRelayOff(RELAY_DRUM_REVERSE);
        tempSensor.setWaitForConversion(true);
        clearStateInEEPROM();
        dryingInitialized = false; resumeRequested = false;
        clearAllFaults();
        lcdSafeClear();
        lcdRowWrite(1, "   ** ABORTED **    ");
        lcdRowWrite(2, "Returning to menu...");
        buzzError(BUZZ_DUR_ALERT);
        transientDrawn = true;
        LOG_I("ABORT","Aborted");
    }
    esp_task_wdt_reset();
    processRelayQueue();
    if ((millis() - abortEntryMs) >= ABORT_DISPLAY_MS)
        enterState(STATE_MAIN_MENU);
}

// ============================================================
//  handleComplete — non-blocking
// ============================================================
void handleComplete() {
    if (!transientDrawn) {
        forceRelayOff(RELAY_HEATER);
        forceRelayOff(RELAY_FAN);
        stopDrumMotor();
        tempSensor.setWaitForConversion(true);
        clearStateInEEPROM();
        dryingInitialized = false; resumeRequested = false;
        lcdSafeClear();
        lcdRowWrite(0, " *** COMPLETE! ***  ");
        lcdRowWrite(1, "Drying finished.    ");
        lcdRowWrite(2, "                    ");
        lcdRowWrite(3, "                    ");
        buzzComplete();
        transientDrawn = true;
        LOG_I("COMPLETE","Done");
    }
    esp_task_wdt_reset();
    processRelayQueue();
    if ((millis() - completeEntryMs) >= COMPLETE_DISPLAY_MS) {
        faultHistory = FAULT_NONE;
        enterState(STATE_MAIN_MENU);
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);

    // Log reset reason to help diagnose restarts
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasons[] = {
        "UNKNOWN","POWERON","EXT","SW","PANIC",
        "INT_WDT","TASK_WDT","WDT","DEEPSLEEP","BROWNOUT","SDIO"
    };
    if ((int)reason < 11) {
        char buf[LOG_BUF_SIZE];
        snprintf(buf, LOG_BUF_SIZE, "Reset reason: %s", reasons[(int)reason]);
        LOG_I("BOOT", buf);
    }

    // WDT
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = WDT_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(NULL);

    EEPROM.begin(EEPROM_SIZE);

    // ---- Relay init: OUTPUT, LOW, populate RelayChannel array ----
    relays[0] = {RELAY_HEATER,       LOW, LOW, 0, true};
    relays[1] = {RELAY_FAN,          LOW, LOW, 0, true};
    relays[2] = {RELAY_DRUM_FORWARD,  LOW, LOW, 0, true};
    relays[3] = {RELAY_DRUM_REVERSE,  LOW, LOW, 0, true};
    for (int i = 0; i < 4; i++) {
        pinMode(relays[i].pin, OUTPUT);
        digitalWrite(relays[i].pin, LOW);
    }

    // ---- Buttons ----
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(buttonPins[i], INPUT_PULLUP);
        buttons[i] = {false, false, 0UL, false};
    }

    // ---- Safety inputs ----
    pinMode(PRESSURE_SWITCH, INPUT);
    pinMode(DOOR_SWITCH,     INPUT);
    bool dr = (digitalRead(DOOR_SWITCH)     == LOW);
    bool ar = (digitalRead(PRESSURE_SWITCH) == LOW);
    doorInput    = {dr, dr, 0UL, DOOR_DEBOUNCE_READS};
    airflowInput = {ar, ar, 0UL, AIRFLOW_DEBOUNCE_READS};

    pinMode(BUZZER_PIN, OUTPUT);

    // ---- I2C at 50kHz (EMI resilient) ----
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(50000);
    Wire.setTimeOut(I2C_WRITE_TIMEOUT_MS);
    delay(100);

    // ---- LCD init ----
    lcd.init(); delay(80);
    lcd.init(); delay(50);  // Double init for reliable startup
    lcd.backlight();
    isBacklightOn = true;
    resetActivityTimer();
    invalidateLCDCache();
    initCustomChars();
    lcd.clear(); delayMicroseconds(1700);
    lastLCDClearMs   = millis();
    lastLCDRefreshMs = millis();
    lastLCDRecoverMs = millis();
    lastLCDReinitMs  = 0;

    // ---- Temp sensor begin (no blocking reads yet) ----
    tempSensor.begin();
    tempSensor.setWaitForConversion(false);

    // ---- EEPROM: load temperature offset ----
    EEPROM.get(EEPROM_TEMP_OFFSET_ADDRESS, temperatureOffset);
    if (isnan(temperatureOffset)
        || temperatureOffset < -10.0f
        || temperatureOffset >  10.0f) {
        temperatureOffset = 0.0f;
        EEPROM.put(EEPROM_TEMP_OFFSET_ADDRESS, temperatureOffset);
        EEPROM.commit();
        LOG_W("BOOT","Temp offset reset");
    }

    validatePrograms();
    esp_task_wdt_reset();

    // Start in sensor validation state — non-blocking
    lcdRowWrite(0, "Yohannes Automation");
    lcdRowWrite(1, "& Electromechanical");
    lcdRowWrite(2, "      Service      ");
    lcdRowWrite(3, " ***0944227623*** ");
    delay(600);
    esp_task_wdt_reset();

    enterState(STATE_SENSOR_INIT);
    LOG_I("BOOT","Setup complete — entering sensor init");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    esp_task_wdt_reset();
    updateButtons();
    updateSafetyInputs();
    updateBacklight();
    processRelayQueue();   // Non-blocking relay settle processing
    lcdRecover();
    lcdPeriodicRefresh();

    if (lcdNeedsFullReinit) {
        fullLCDReinit();
        forceRedrawCurrentScreen();
    }

    switch (currentState) {

        // ---- Sensor Init (non-blocking) ----
        case STATE_SENSOR_INIT:
            handleSensorInit();
            break;

        // ---- Main Menu ----
        case STATE_MAIN_MENU:
            if (isButtonPressed(BUTTON_UP)) {
                menuIndex = (menuIndex + 2) % 3;
                displayMainMenu(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_DOWN)) {
                menuIndex = (menuIndex + 1) % 3;
                displayMainMenu(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_SELECT)) {
                buzzSelect();
                if (menuIndex == 0) {
                    if (!sensorValidated) {
                        lcdSafeClear();
                        lcdRowWrite(1, "No temp sensor!     ");
                        lcdRowWrite(2, "Cannot start cycle  ");
                        buzzError(BUZZ_DUR_LONG);
                        delay(2500); esp_task_wdt_reset();
                        displayMainMenu();
                    } else {
                        enterState(STATE_SELECT_PROGRAM);
                    }
                } else if (menuIndex == 1) {
                    enterState(STATE_SETTINGS);
                } else {
                    enterState(STATE_SYSTEM_STATUS);
                }
            }
            break;

        // ---- Select Program ----
        case STATE_SELECT_PROGRAM:
            if (isButtonPressed(BUTTON_UP)) {
                selectedProgram = (selectedProgram + NUM_PROGRAMS - 1) % NUM_PROGRAMS;
                displaySelectProgram(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_DOWN)) {
                selectedProgram = (selectedProgram + 1) % NUM_PROGRAMS;
                displaySelectProgram(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_SELECT)) {
                buzzSelect();
                bool uncfg = (strcmp(programs[selectedProgram].name,"Custom")==0
                              && programs[selectedProgram].temperature==0);
                if (uncfg) { enterState(STATE_CUSTOM_PROGRAM); }
                else {
                    elapsedDryingTime = 0; clearStateInEEPROM();
                    resumeRequested = false; enterState(STATE_DRYING);
                }
            }
            if (isButtonPressed(BUTTON_BACK)) {
                buzzBack(); enterState(STATE_MAIN_MENU);
            }
            break;

        // ---- Settings ----
        case STATE_SETTINGS:
            if (settingsCtx.needsRedraw) displaySettingsPage();
            if (isButtonPressed(BUTTON_UP) || isButtonPressed(BUTTON_DOWN)) {
                settingsCtx.idx = 1 - settingsCtx.idx;
                settingsCtx.needsRedraw = true; buzzNavigate();
            }
            if (isButtonPressed(BUTTON_SELECT)) {
                buzzSelect();
                if (settingsCtx.idx == 0) enterState(STATE_CALIBRATE_TEMP);
                else                      enterState(STATE_MAIN_MENU);
            }
            if (isButtonPressed(BUTTON_BACK)) { buzzBack(); enterState(STATE_MAIN_MENU); }
            break;

        // ---- Calibrate ----
        case STATE_CALIBRATE_TEMP:
            if (isButtonPressed(BUTTON_UP)) {
                temperatureOffset = min(temperatureOffset + 0.1f, 5.0f);
                displayCalibratePage(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_DOWN)) {
                temperatureOffset = max(temperatureOffset - 0.1f, -5.0f);
                displayCalibratePage(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_SELECT)) {
                EEPROM.put(EEPROM_TEMP_OFFSET_ADDRESS, temperatureOffset);
                EEPROM.commit();
                lcdRowWrite(3, "Saved!              ");
                buzzSelect();
                delay(1000); esp_task_wdt_reset();
                enterState(STATE_SETTINGS);
            }
            if (isButtonPressed(BUTTON_BACK)) {
                temperatureOffset = calibrateCtx.savedOffset;
                buzzBack(); enterState(STATE_SETTINGS);
            }
            break;

        // ---- Custom Program ----
        case STATE_CUSTOM_PROGRAM:
            if (isButtonPressed(BUTTON_UP)) {
                if (customCtx.stage == 0)
                    customCtx.tempVal = min(customCtx.tempVal+1, PROGRAM_MAX_TEMPERATURE);
                else
                    customCtx.durationVal = min(customCtx.durationVal+5, 995);
                displayCustomPage(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_DOWN)) {
                if (customCtx.stage == 0)
                    customCtx.tempVal = max(customCtx.tempVal-1, 30);
                else
                    customCtx.durationVal = max(customCtx.durationVal-5, 5);
                displayCustomPage(); buzzNavigate();
            }
            if (isButtonPressed(BUTTON_SELECT)) {
                buzzSelect(); customCtx.stage++;
                if (customCtx.stage > 1) {
                    programs[NUM_PROGRAMS-1].temperature = customCtx.tempVal;
                    programs[NUM_PROGRAMS-1].duration    = customCtx.durationVal;
                    selectedProgram   = NUM_PROGRAMS - 1;
                    elapsedDryingTime = 0; clearStateInEEPROM();
                    resumeRequested   = false; enterState(STATE_DRYING);
                } else { displayCustomPage(); }
            }
            if (isButtonPressed(BUTTON_BACK)) { buzzBack(); enterState(STATE_SELECT_PROGRAM); }
            break;

        // ---- Drying ----
        case STATE_DRYING:
            if (!dryingInitialized) {
                initDrying(resumeRequested);
                resumeRequested = false;
            }
            if (currentState == STATE_DRYING) updateDrying();
            break;

        case STATE_PAUSED:        handlePaused();        break;
        case STATE_COOL_DOWN:     handleCoolDown();      break;
        case STATE_SYSTEM_STATUS: updateSystemStatus();  break;
        case STATE_RESUME_PROMPT: handleResumePrompt();  break;
        case STATE_ABORT:         handleAbort();         break;
        case STATE_COMPLETE:      handleComplete();      break;
    }

    delay(10);
}