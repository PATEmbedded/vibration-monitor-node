# Changelog

## 1.0.0 (2026-10-18)

First release.

- Portable C11 core: signal model, radix-2 FFT, 15 vibration features, int8 classifier, Modbus RTU server
- Application layer with the register map and the analysis step, independent of the RTOS
- STM32F407 firmware on FreeRTOS: Modbus task with frame-gap detection, analysis task, watchdog supervisor, RS-485 direction control
- Python reference, numpy training with int8 quantisation, and C header export
- Tests: Unity unit tests with sanitizers, C against the Python reference (bit-exact int8 scores), libFuzzer, cppcheck, and system tests of the firmware on an emulated STM32F407 in Renode
