# 4180 Final Project - Running Alarm Clock

## Overview:

The Running Alarm Clock is an alarm clock that runs away from the user when triggered. This is implemented through RTC, WiFi+Hotspot, BLE, ESP-NOW, and peripherals including pushbuttons, a uLCD display, and motor drivers for movement. 

Traditional embedded-system alarm clocks often include:
- Touchscreen or physical button interfaces
- Math problems or pattern puzzles to dismiss alarms
- Smartphone-connected alarm apps
- “Clocky”-style moving alarms (mechanical movement, no wireless guidance)

**Key difference:**
None of these systems (to the best of our knowledge) use BLE-based distance estimation to deliberately move the alarm away from the user in real time.
Most rely purely on cognitive tasks (math problems, memory tasks). Ours adds a physical chase mechanic, combining BLE RSSI sensing with mobile locomotion to gamify waking up.

## Design:

The physical components include 2 ESP32-C6 Microcontrollers. The Alarm ESP32 is the main alarm clock that controls the alarm functionalities and trigger logic. The Motor ESP32 communicates with the Alarm ESP32 to drive its motor control logic.

Peripherals for the Alarm ESP32 include a uLCD display that displays the user interface, 4 pushbuttons for user input, a speaker for the alarm noise, and a debug LED to signal a valid BLE connection. 2 pushbuttons allow the user to adjust the target hour and time on the display, 1 pushbutton disables the alarm when on, and 1 pushbutton sends the device into sleep mode. 

The only peripheral for the Motor ESP32 is a motor driver that controls both motors of the device. The control logic is driven by the relative distance of both chips to the user, obtained through RSSI values of their BLE connections.

Both ESP32's are each powered by a 3000mAh battery connected through a DC-DC Voltage Regulator, and the motors are powered by a 9V battery.

**Libraries and APIs Used**

- ESP32 Arduino core (WiFi, HTTPClient, BLEDevice, esp_now, esp_sleep)
- Arduino_JSON by Arduino – JSON parsing
- ESP32Time by fbiego - RTC handling
- 4D Systems Goldelox libraries – uLCD communication (Goldelox_Serial_4DLib, Goldelox_Const4D)

**Circuit Diagram:**

![Circuit Diagram](CircuitDiagram.png)

## Usage Instructions:

- When the alarm first powers on, it needs to grab real-world date and time through WiFi internet. The user must provide a valid WiFi access point such as Hotspot. Once connected, the alarm posts HTTP requests to get date/time from the internet. This process may take a minute or two.
- Once the data is obtained, the alarm stores the information into the internal RTC modules. Now that the device is initialized with the correct time, the uLCD is will display the alarm interface with the time, date, and input.
- To set an alarm, the user adjusts the target time through pushbuttons. One pushbutton increments the hour, the other increments the minute. Changes to the input are displayed on the uLCD.
- Prior to alarm trigger, the user broadcasts a BLE advertisement. When the alarm is triggered, the speakers will start and the the device will move away from the user.
- To disable the alarm, the user catches the device and presses a pushbutton.
- At any point in time, the user can turn off the clock and send it into sleep mode by pressing a pushbutton. Another press to the same pushbutton will wake up the device. Data will persist throughout sleep mode so upon wake, the clock will continue running as normal.


**Device States**

**1. IDLE:**
  - In sleep mode
  - Pushbutton triggers wake (and similarly reenters sleep mode)

**2. Program Initialization:** WiFi Hotspot + RTC
  - ALARM ESP32 connects to WiFi Hotspot and sends HTTP request to grab real time data from internet
  - After receiving data, parses HTTP response and stores time in RTC
  - Finally, disconnects from WiFi

**2. Clock Running:** RTC + GPIO Inputs
  - System continuously displays current date, time, and user input
  - Pushbuttons allow user to adjust the target hour and time
     
**3. Alarm On:** BLE + ESP-NOW, Speaker, Motors
  - When target time is hit, speakers turn on, and both ALARM and MOTOR ESP32s scan for and connect to user's advertising BLE
  - Both ALARM and HELPER ESP32s read individual RSSI values, and ALARM ESP32 sends RSSI value to MOTOR through ESP-NOW
  - MOTOR compares RSSI to calculate relative direction from user
  - Motors move in opposite direction of detected user's position

**3. Alarm Off:** 
  - Waits for pushbutton press to disable alarm
  - Program returns to normal functionality, and alarm is disabled for the current target

**_*Additional Details:_**
- ESP32's powered by a 3000mAh battery with a buck converter for each (battery connected to one of either buck converter)
- Motors powered by 9V battery connected to Vm pin, receives PWM value from ESP32 GPIO pins for speed
- Occasionally, issues may arise with the BLE connection. If the robot is unable to connect to via BLE, restart the MCU and try again. 

## Future Improvements:
- Obstacle avoidance using IR or ultrasonic sensors.
- Add Wi-Fi app or web dashboard for setting alarms instead of physical buttons.
- Add an OLED or TFT touchscreen for more intuitive UI.

