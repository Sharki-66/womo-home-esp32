# Walter Firmware (ESP32-S3 Sensor Hub)

This project contains the dedicated firmware for the "Walter" ESP32-S3 module.
It will host the sensor stack (BNO055, ADS1115, …) and forward measurements to
the display controller over RS-485.

## Build & Flash

```powershell
cd firmware-walter
idf.py set-target esp32s3
idf.py build flash monitor
```

Make sure to select the COM port of the Walter board when flashing or monitoring.

## Next Steps

- Add drivers for the BNO055 IMU and ADS1115 ADC.
- Implement the RS-485 transport and frame format.
- Expose configuration via Kconfig (GPIO assignments, baud rate, polling interval).
- Provide a simple command interface for calibration and diagnostics.
