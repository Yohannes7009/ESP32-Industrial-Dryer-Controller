# Changelog

All notable changes to this project will be documented in this file.

The format follows the principles of "Keep a Changelog".

---

## Version 6.2 - Initial Public Release

### Added

* ESP32-based industrial dryer controller
* 20x4 I2C LCD user interface
* Configurable drying programs
* EEPROM parameter storage
* Resume after power failure
* Non-blocking finite state machine architecture
* Automatic cool-down cycle
* Fan purge delay before heater activation
* Door safety monitoring
* Airflow verification using pressure switch
* Heater hysteresis temperature control
* Audible buzzer notifications
* Automatic drum forward/reverse rotation

### Safety

* Over-temperature protection
* Sensor failure detection
* Relay interlock protection
* Emergency stop handling
* Watchdog timer recovery
* EEPROM recovery after unexpected reset

### Improved

* Temperature stability
* Relay switching reliability
* LCD update performance
* User navigation responsiveness
* State transition handling

### Fixed

* Rapid heater cycling
* LCD refresh flickering
* Temperature spike readings
* Startup initialization timing
* EEPROM save consistency

---

## Future Development

Planned features include:

* Wi-Fi remote monitoring
* MQTT cloud integration
* Web dashboard
* Historical drying logs
* OTA firmware updates
* SD card logging
* Energy consumption monitoring
* Multi-language interface