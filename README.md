# ESP32 Industrial Dryer Controller

A professional ESP32-based industrial dryer controller designed for commercial laundry and industrial drying applications.

This project replaces conventional timer-based controllers with a modern embedded system featuring intelligent temperature regulation, safety monitoring, automatic drum control, EEPROM state recovery, and a user-friendly LCD interface.

---

## Features

### Temperature Control

* DS18B20 digital temperature sensor
* Configurable drying programs
* Intelligent hysteresis control
* Temperature calibration with EEPROM storage
* Spike filtering for unstable sensor readings

### Industrial Safety

* Door safety interlock
* Airflow/pressure switch monitoring
* Maximum temperature protection
* Overheat duration monitoring
* Relay failure detection
* Sensor fault detection
* Emergency stop support

### Drying Automation

* Automatic heater control
* Fan purge delay before heating
* Automatic cool-down cycle
* Bidirectional drum rotation
* Forward / Reverse motor interlock
* Non-blocking state machine architecture

### User Interface

* 20x4 I2C LCD display
* Five-button navigation system
* Audible buzzer feedback
* Program selection menu
* Custom drying program configuration
* System status monitoring

### Reliability

* ESP32 Watchdog Timer support
* Non-blocking firmware design
* EEPROM power-loss recovery
* Circular EEPROM wear leveling
* Relay command verification
* LCD I2C recovery mechanism
* Debounced safety inputs

---

## Hardware

### Controller

* ESP32 Development Board

### Sensors

* DS18B20 Temperature Sensor
* Door Switch
* Air Pressure Switch

### Outputs

* Heater Relay
* Fan Relay
* Drum Forward Relay
* Drum Reverse Relay
* Buzzer

### Display

* 20x4 I2C LCD

---

## Drying Process

1. Power-up self-test
2. Temperature sensor validation
3. Program selection
4. Fan purge cycle
5. Airflow verification
6. Heater activation
7. Automatic temperature regulation
8. Alternating drum rotation
9. Continuous safety monitoring
10. Automatic cool-down
11. Cycle completion

---

## Safety Logic

The heater is enabled only when:

* Temperature sensor is valid
* Door is closed
* Airflow is confirmed
* Purge delay has completed
* No active faults exist

If any safety condition fails, the heater is immediately disabled.

---

## Temperature Control Strategy

Target Temperature = Program Temperature

Example:

Target = 60°C

* Heater turns ON at 57°C
* Heater turns OFF at 60°C

This hysteresis prevents rapid relay switching while maintaining stable drying performance.

---

## EEPROM Recovery

If power is interrupted during drying:

* Current program is saved
* Elapsed time is stored
* Temperature calibration is preserved

After restart, the controller prompts the operator to resume the previous drying cycle.

---

## Technologies Used

* ESP32
* Arduino Framework
* C++
* DallasTemperature
* OneWire
* LiquidCrystal_I2C
* EEPROM
* State Machine Architecture

---

## Project Status

Current Version: **v6.2**

Implemented improvements:

* Correct heater hysteresis
* Sensor spike filtering
* Non-blocking relay queue
* LCD row caching
* Watchdog-safe sensor initialization
* Relay readback verification
* EEPROM recovery
* Industrial safety interlocks

---

## Author

**Yohannes Gelmessa**

Embedded Systems Engineer | Industrial Automation Specialist | IoT Developer

Passionate about designing reliable embedded systems and industrial automation solutions for commercial and industrial applications.

---

## License

This project is published for portfolio and educational purposes.

Commercial reuse or redistribution requires permission from the author.
