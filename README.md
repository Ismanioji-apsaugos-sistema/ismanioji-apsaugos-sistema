# Smart Security System

An Arduino Nano 33 IoT security system prototype with RFID access control, ultrasonic intrusion detection, an audible alarm, and a web dashboard for monitoring and control over a local network.

## Features

- Arm and disarm the system with an authorized RFID card.
- Detect objects closer than 100 cm after a 10-second exit delay.
- Display the system state on a 20×4 LCD and sound a buzzer when an alarm is triggered.
- View the distance reading and the 10 most recent events in a web dashboard.
- Arm and disarm remotely using a PIN. By default, an active alarm can only be reset with an authorized card.

## Hardware

- Arduino Nano 33 IoT
- PN532 RFID reader and RFID card or tag
- HC-SR04 ultrasonic distance sensor
- 20×4 LCD with an I2C adapter
- Buzzer, breadboard, and jumper wires

An ESP32 variant is included for Wokwi simulation, where a push button replaces the RFID reader.

## Setup

1. Open `firmware/firmware.ino` in the Arduino IDE and select Arduino Nano 33 IoT.
2. Install the **LiquidCrystal I2C**, **WiFiNINA**, and **Adafruit PN532** libraries and their dependencies.
3. Set `WIFI_SSID`, `WIFI_PASS`, `WEB_PIN`, and `AUTHORIZED_CARDS` in the sketch. Adjust the distance threshold and exit delay if needed.
4. Connect the components using the pin assignments in the sketch and the wiring details in the project description.
5. Upload the sketch. Open Serial Monitor at **115200 baud** or read the LCD to find the device's IP address, then open that address in a browser on the same network.

For Wokwi, create an ESP32 project, copy `firmware/firmware.ino` into `sketch.ino`, use `firmware/diagram.json` as the circuit diagram, and add **LiquidCrystal I2C** to `libraries.txt`.

## Project Files

- `firmware/firmware.ino` — firmware and embedded web dashboard.
- `firmware/diagram.json` — Wokwi simulation circuit.
- `firmware/libraries.txt` — library list.
- `Projekto_aprasas_Apsaugos_sistema.docx` — original project description in Lithuanian.

This is an educational prototype. The dashboard uses HTTP and is intended for a trusted local network. Replace the example PIN before use and keep real Wi-Fi credentials out of the public repository. Firmware operation has not yet been verified on hardware or in simulation.
