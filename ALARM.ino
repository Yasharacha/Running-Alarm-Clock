/* 
 * This project uses the following third-party libraries:
 * - WiFi.h, HTTPClient.h, BLEDevice.h, esp_now.h, esp_sleep.h
 *   from the ESP32 Arduino core by Espressif Systems.
 * - Arduino_JSON by Arduino (JSON parsing).
 * - ESP32Time by fbiego for RTC system.
 * - Goldelox_Serial_4DLib and Goldelox_Const4D from 4D Systems for uLCD control.
 *
 * Libraries are used under their respective licenses.
 */
#include <WiFi.h> // WiFi library for Hotspot connection
#include <HTTPClient.h> // HTTPClient library for HTTP requests
#include <Arduino_JSON.h> // JSON library for JSON parsing
#include <ESP32Time.h> // ESp32Time library for RTC system
#include <Goldelox_Serial_4DLib.h> // Goldelox Serial library for uLCD communication
#include <Goldelox_Const4D.h> // Goldelox Serial library for uLCD communication
#include <BLEDevice.h> // BLEDevice library for BLE connections
#include <esp_now.h> // esp_now library for ESP-NOW communication
#include "esp_sleep.h" // esp_sleep library for sleep mode functions

// ==== I/O PINS & Variables
#define INC_HOUR 21                             // pushbutton GPIO
#define INC_MINUTE 22                           // pushbutton GPIO
#define RESET_PIN 23                            // uLCD UART
#define ESP_TX 5                                // uLCD UART
#define ESP_RX 6                                // uLCD UART
#define SPEAKER_PIN 19                          // output pin for speaker
#define DISABLE_PIN 20
#define RED_TONE_HZ 800                         // speaker tone
#define DisplaySerial Serial1                   // uLCD
Goldelox_Serial_4DLib Display(&DisplaySerial);  // uLCD
String inputTime = "14:51";                     // user input initialized to 00:00 in the format of HH:MM

#define ON_OFF_PIN 7  // for sleep mode ---- need to implement


// ==== WiFi + RTC VARIABLES
#define SSID "ESP32-5672"
#define PASSWORD "asdf1234"
#define SERVER_URL "http://worldtimeapi.org/api/timezone/America/New_York"
#define WAIT_INTERVAL 100  // every second
unsigned long lastTime;    // check time every second and for button debouncing
WiFiServer server(80);     //set web server port number to 80
ESP32Time rtc(0);          // timer with no offset

// ==== BLE VARIABLES
#define BLE_LED 4                        // for debugging, is ON when connected to BLE
const char* target_name = "Alarm_User";  // name of user BLE advertising device
bool doConnect = false;                  // true when advertising device is found and program should try and connect
bool isConnected = false;                // true when successfully connected
bool scanning = false;                   // whether currently scanning or not
BLEAdvertisedDevice* foundDevice;        // connected device
BLEScan* pBLEScan;                       // scanner object
BLEClient* pClient;                      // client object

// ==== ESP-NOW VARIABLES
typedef struct message {  // Structure to send data - must match the sender structure
  int rssi;
} message;
message rssi_message;  // create an instance of the message to send

uint8_t alarmAddress[] = { 0xA0, 0x85, 0xE3, 0xDA, 0xBF, 0x04 };  // mac address of motor ESP is A0:85:E3:DA:BF:04
//uint8_t alarmAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // universal broadcast for testing
esp_now_peer_info_t peerInfo;
unsigned long lastSent = 0;

volatile bool toggleSleepRequested = false;  // set by ISR, handled in loop
bool isSleeping = false;                     // current soft-sleep state





/* ===== CALLBACKS & HELPER FUNCTIONS ===== */

/* BLE Scanner Callback: Scan for BLE servers and find the one with the target name */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {                 // Called for each advertising BLE server
    if ((advertisedDevice.getName() == target_name) && !isConnected) {  // Found advertising device
      foundDevice = new BLEAdvertisedDevice(advertisedDevice);          // create object to store found device
      scanning = false;
      doConnect = true;  // turn on signal to connect
      pBLEScan->stop();  // stop the scan
    }
  }  // onResult
};   // MyAdvertisedDeviceCallbacks

/* BLE Client helper function to connect to device */
bool connectToDevice() {
  pClient = BLEDevice::createClient();                 // create client
  if (foundDevice && pClient->connect(foundDevice)) {  // connect to found device
    isConnected = true;                                // set connected flag
    return true;
  }
  return false;
}

// ESP_NOW SENDER CALLBACK: print status of sent data
void OnDataSent(const wifi_tx_info_t* mac_addr, esp_now_send_status_t status) {
  lastSent = millis();
  Serial.print("Last Packet Send Status at ");
  Serial.print(lastSent);
  Serial.print(": ");
  Serial.print(pClient->getRssi());
  Serial.print(" ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

/* uLCD Helper handles user input and display */
void displayTime(bool adjust, bool adjustHour, bool hit) {
  if (adjust) {  // if input time needs to be adjusted
    int hour = inputTime.substring(0, 2).toInt();
    int minute = inputTime.substring(3, 5).toInt();
    if (adjustHour) {  // increment the hour by 1 and wrap around if necessary
      hour = hour + 1 > 23 ? 0 : hour + 1;
    } else {  // increment the minute by 1, wrap and update hour if necessary
      minute = minute + 1 > 59 ? 0 : minute + 1;
      if (minute == 0) {
        hour = hour + 1 % 24;  // update hour if necessary
      }
    }
    // put back into string
    char buf[6];  // "HH:MM" + null byte
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    inputTime = String(buf);
  }

  // Display current time with big font
  char currTimeToDisplay[rtc.getTime("%H:%M:%S").length() + 1];  // current time
  rtc.getTime("%H:%M:%S").toCharArray(currTimeToDisplay, sizeof(currTimeToDisplay));

  Display.txt_FontID(0);
  Display.txt_Height(2);
  Display.txt_Width(2);
  Display.txt_FGcolour(hit ? RED : YELLOW);  // display red if time hit, yellow otherwise
  Display.txt_MoveCursor(3, 1);
  Display.putstr(currTimeToDisplay);

  // Display other values in small font
  char inputToDisplay[inputTime.length() + 1];  // alarm input
  inputTime.toCharArray(inputToDisplay, sizeof(inputToDisplay));

  String day = rtc.getTime("%A");
  char dayToDisplay[day.length() + 1];
  day.toCharArray(dayToDisplay, sizeof(dayToDisplay));

  String date = rtc.getTime("%B %d %Y");
  char dateToDisplay[date.length() + 1];
  date.toCharArray(dateToDisplay, sizeof(dateToDisplay));

  Display.txt_FontID(0);
  Display.txt_Height(1);
  Display.txt_Width(1);
  Display.txt_Opacity(OPAQUE);
  Display.txt_FGcolour(WHITE);
  Display.txt_MoveCursor(1, 1);
  Display.putstr("Set Alarm: ");
  Display.txt_MoveCursor(1, 12);
  Display.putstr(inputToDisplay);
  Display.txt_MoveCursor(9, 2);
  Display.putstr(dayToDisplay);
  Display.txt_MoveCursor(10, 2);
  Display.putstr(dateToDisplay);
}

void IRAM_ATTR onOffISR() {
  toggleSleepRequested = true;  // just signal the main loop
}

void initialize_uLCD() {
  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, 0);
  delay(100);
  digitalWrite(RESET_PIN, 1);  // reset uLCD
  delay(5000);

  DisplaySerial.begin(9600, SERIAL_8N1, ESP_RX, ESP_TX);
  Display.TimeLimit4D = 5000;
  Display.gfx_Cls(); // clear screen
  return;
}

String getDateTime() {      // get date and time through HotSpot and HTTP request
  // uLCD display
  Display.txt_FontID(0);
  Display.txt_Height(1);
  Display.txt_Width(1);
  Display.txt_Opacity(OPAQUE);
  Display.txt_FGcolour(WHITE);
  Display.txt_MoveCursor(1, 1);
  Display.putstr("Connecting\nto WiFi...");  // print to display

  // Implementation and serial debug
  Serial.print("\nSetting AP (Access Point)...");
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nConnected! localIP address: ");
  Serial.println(WiFi.localIP());
  Display.txt_MoveCursor(4, 1);
  Display.putstr("WiFi connected!\nGetting time...");  // print to display

  // 2. RTC Instantation: continuously wait for HTTP connection, set RTC and close WiFi
  lastTime = 0;     // used to for repeating HTTP requests
  String dateTime;  // parsed date and time from HTTP response
  Serial.print("Searching");
  while (true) {
    if ((millis() - lastTime) > 4000) {  // Request HTTP response every 4 seconds until date and time received
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(SERVER_URL);

        // Get payload from HTTP GET request
        int httpCode = http.GET();
        if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
          JSONVar obj = JSON.parse(http.getString());  // Create JSONVar object to parse the payload and print out date and time
          if (JSON.typeOf(obj) != "undefined") {       // Parse JSON object
            // VALID TIME FOUND - get date & time string and disconnect wireless communication
            dateTime = (const char*)obj["datetime"];
            WiFi.disconnect(true);
            Serial.print("\nFound date time: ");
            Serial.println(dateTime);
            http.end();
            return dateTime;
          } else {  // JSON parse error
            Serial.println("Failed to parse JSON");
          }
        } else {  // HTTP code error
          Serial.print("..");
        }
        http.end();  // close HTTP connection
      } else {       // WiFi disconnection
        Serial.println("WiFi disconnected");
      }
      lastTime = millis();  // update last time
    }
  }
  return "";
}

void initialize_BLE() {       // initialize BLE scanner
  doConnect = false;                  // true when advertising device is found and program should try and connect
  isConnected = false;                // true when successfully connected
  scanning = false; 
  foundDevice = NULL;

  BLEDevice::init("");                                                        // initialize scanner
  pBLEScan = BLEDevice::getScan();                                            // create scanner
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());  // set callbacks
  pBLEScan->setInterval(1349);                                                // set scan interval
  pBLEScan->setWindow(449);                                                   // set scan window
  pBLEScan->setActiveScan(true);                                              // set active - ask for more data, better RSSI
}

void disconnect_BLE() {
  doConnect = false;
  isConnected = false;
  scanning = false;
  foundDevice = NULL;
}

int initialize_ESP_NOW() {    // returns 0 on success, -1 on failure
  WiFi.mode(WIFI_STA);  // Enable WiFi station
  WiFi.disconnect();    // prevent previous WiFi connections

  if (esp_now_init() != ESP_OK) {  // initialize esp-now
    Serial.println("Error initializing ESP-NOW");
    return -1;
  }

  // Register for Send CB to get the status of transmitted packet
  esp_now_register_send_cb(OnDataSent);

  // Register and add peer
  memset(&peerInfo, 0, sizeof(peerInfo));  // clear everything
  memcpy(peerInfo.peer_addr, alarmAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return -1;
  }
  return 0; // success
}


// SETUP =======================================================
void setup() {
  Serial.begin(115200);

  // Input Initialization
  pinMode(INC_HOUR, INPUT_PULLUP);
  pinMode(INC_MINUTE, INPUT_PULLUP);
  pinMode(DISABLE_PIN, INPUT_PULLUP);
  pinMode(SPEAKER_PIN, OUTPUT);
  pinMode(BLE_LED, OUTPUT);
  pinMode(ON_OFF_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ON_OFF_PIN), onOffISR, FALLING);

  initialize_uLCD();
  String dateTime = getDateTime(); // get the date and time through WiFi Hotspot and HTTP request
  initialize_BLE();                // initialize BLE scanner
  if (initialize_ESP_NOW() != 0) { // initialize ESP-NOW connection
    return;
  }

  // Instantiate uLCD
  Display.gfx_Cls();  // clear screen
  // Set internal RTC timer: Format of dateTime is 2025-11-21T16:18:32.355336-05:00
  int tIndex = dateTime.indexOf('T');
  int second = dateTime.substring(tIndex + 7, tIndex + 9).toInt();
  int minute = dateTime.substring(tIndex + 4, tIndex + 6).toInt();
  int hour = dateTime.substring(tIndex + 1, tIndex + 3).toInt();
  int day = dateTime.substring(tIndex - 2, tIndex).toInt();
  int month = dateTime.substring(tIndex - 5, tIndex - 3).toInt();
  int year = dateTime.substring(0, 4).toInt();
  rtc.setTime(second + 1, minute, hour, day, month, year);  // RTC instantiation
}




// LOOP =======================================================
bool alarm_triggered = false; // whether alarm is ON or OFF
bool disabled = false;
void loop() {
  if (toggleSleepRequested) {           // Sleep mode initiated
    toggleSleepRequested = false;

    // Stop sound
    tone(SPEAKER_PIN, 0);               // turn off speaker

    // Draw "Sleeping" screen
    Display.gfx_Cls();
    Display.txt_FontID(0);
    Display.txt_Height(1);
    Display.txt_Width(1);
    Display.txt_FGcolour(WHITE);
    Display.txt_MoveCursor(3, 1);
    Display.putstr("Sleeping...\nPress button to wake");

    // --- Disable the ON/OFF interrupt while we sleep ---
    detachInterrupt(digitalPinToInterrupt(ON_OFF_PIN));

    // --- Configure GPIO wake-up source for light sleep ---
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);  // clear any old config

    // Ensure pin is input with pull-up
    pinMode(ON_OFF_PIN, INPUT_PULLUP);

    // Keep RTC peripheral domain on so the pull-up works in sleep
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Wake when ON_OFF_PIN is LOW (button pressed)
    gpio_wakeup_enable((gpio_num_t)ON_OFF_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Make sure the button is released before actually sleeping
    while (digitalRead(ON_OFF_PIN) == LOW) {
      delay(10);
    }

    Serial.println("Entering light sleep...");
    Serial.flush();
    delay(50);  // small settle delay

    disconnect_BLE();
    // === ENTER LIGHT SLEEP ===
    esp_light_sleep_start();

    // === WOKE UP HERE ===
    delay(500);

    // Optional: wait until the wake button is released again to avoid instant re-trigger
    while (digitalRead(ON_OFF_PIN) == LOW) {
      delay(10);
    }
    delay(50);

    // Clear any stale flag just in case
    toggleSleepRequested = false;

    // Re-enable the ON/OFF interrupt for the next sleep request
    attachInterrupt(digitalPinToInterrupt(ON_OFF_PIN), onOffISR, FALLING);

    Display.gfx_Cls(); // clear screen
    // Then we fall through and the rest of loop() continues as normal
} 




  displayTime(false, false, false);  // display current time

  // check RTC time and button inputs every second
  unsigned long now = millis();
  if (now - lastTime > WAIT_INTERVAL) {                                         // Button debounce
    if ((digitalRead(INC_HOUR) == LOW)) { displayTime(true, true, false); }     // true = adjust time, true = increment hour, false = no hit
    if ((digitalRead(INC_MINUTE) == LOW)) { displayTime(true, false, false); }  // true = adjust time, false = increment minute, false = no hit
    if (rtc.getTime("%H:%M") == inputTime) {
      alarm_triggered = true; // set flag for alarm
    } else {
      disabled = false;
      alarm_triggered = false;
    }
    lastTime = now;
  }

  displayTime(false, false, false);  // continuously display current time

  // USER INPUT TO TURN OFF ALARM
  if (alarm_triggered && digitalRead(DISABLE_PIN) == LOW) {
    Serial.println("Alarm off");
    disabled = true;
    digitalWrite(BLE_LED, LOW);
    tone(SPEAKER_PIN, 0);
    disconnect_BLE();
  }

  // IF ALARM TRIGGERED, start speakers, BLE connections, and motors
  if (alarm_triggered && !disabled) {              // Alarm ON if trigger flag on and NOT disabled
    displayTime(false, false, true);  // false = no adjustment, false = N/A, true = hit (flash red)
    tone(SPEAKER_PIN, RED_TONE_HZ);   // output speaker

    // BLE connection
    if (!scanning && foundDevice == NULL) {   // start scanning for user device, non-blocking
      scanning = true;                        // is currently scanning
      Serial.println("scanning");
      pBLEScan->start(1, false);              // upon found device, foundDevice!=NULL, doConnect=true
    } else if (doConnect and connectToDevice()) {  // found and connected to device
      doConnect = false;                           // stop trying to connect
      Serial.println("connected");
    }

    displayTime(false, false, true);  // display again to prevent glitching

    // If connectToDevice() is successful, continuously scan RSSI and send to main ESP32 through ESP_NOW
    if (isConnected) {                // At this point, flags should be scanning=false, foundDevice!=NULL, doConnect=false
      digitalWrite(BLE_LED, HIGH);    // turn on LED
      // If client is connected, read and send RSSI
      if (pClient->isConnected()) {           // If CLIENT CONNECTED, Send RSSI to motor ESP through ESP-NOW
        digitalWrite(BLE_LED, HIGH);          // Turn on BLE LED to signal connection
        if (millis() - lastSent >= 1000) {
          rssi_message.rssi = pClient->getRssi();                                                         // set the message
          esp_err_t result = esp_now_send(alarmAddress, (uint8_t*) &rssi_message, sizeof(rssi_message));  // send packet
        }

      } else {                           // If client disconnected, reset everything to allow for re-scan
        Serial.println("Disconnected");  // debug print statement
        digitalWrite(BLE_LED, LOW);      // turn off LED for disconnect
        isConnected = false;             // reset flag for connection status
        doConnect = false;               // reset flag for BLE connect
        scanning = false;                // reset flag for scanning
        foundDevice = NULL;              // clears device and allows BLE to scan again
        pBLEScan->clearResults();        // clear results
      }
    } else {                      // BLE not connected
      digitalWrite(BLE_LED, LOW); // turn off LED
      scanning = false;           // start scanning again
    }

  } else {                        // alarm OFF
    digitalWrite(BLE_LED, LOW);   // turn off LED
    tone(SPEAKER_PIN, 0);         // turn off speaker
  }
}
