/* 
 * This project uses the following third-party libraries:
 * - WiFi.h, BLEDevice.h, esp_now.h
 *   from the ESP32 Arduino core by Espressif Systems.
 *
 * Libraries are used under their respective licenses.
 */
#include <BLEDevice.h>
#include <WiFi.h>
#include <esp_now.h>

/*
 * RECEIVER/MOTORS ESP32-C6
 * For Motor ESP 32 logic
 * 1. Connects to User-advertised BLE and reads RSSI
 * 2. Receives RSSI value from ALARM ESP32 over ESP-now
 * 3. Compares RSSI value to determine relative direction to User
 * 4. Controls motors
 */

// ---- BLE VARIABLES AND FUNCTIONS ----
const char* target_name = "Alarm_User"; // name of user BLE advertising device
bool doConnect = false; // true when advertising device is found and program should try and connect
bool isConnected = false; // true when successfully connected

BLEScan* pBLEScan; // scanner object
BLEClient* pClient; // client object
BLEAdvertisedDevice* foundDevice; // connected device

bool startedRSSI = false; // whether communication has started
unsigned long lastReceivedTime; // last time an RSSI was sent

// === Motor control tuning ===
const int BASE_SPEED = 90;      // 0–255, forward drive speed
const int TURN_SPEED = 50;      // turning speed
const int RSSI_DELTA_THRES = 3;  // dB threshold to consider "better/worse"

int lastRssi = -1000;  // sentinel for "no reading yet"
bool haveRssi = false;

const int B_IN1 = 21;  // H-bridge input 2  (direction)
const int B_IN2 = 20;  // H-bridge input 2  (direction)
const int B_PWM = 19;   // H-bridge enable / PWM (speed) -> MUST be a PWM-capable pin

const int A_IN1 = 22;  // H-bridge input 1  (direction)
const int A_IN2 = 23;  // H-bridge input 2  (direction)
const int A_PWM = 15;  // H-bridge enable / PWM (speed) -> MUST be a PWM-capable pin

// BLE Scanner Callbacks: Scan for BLE servers and find the one with the target name
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) { // Called for each advertising BLE server
    if ((advertisedDevice.getName() == target_name) && !isConnected) { // Found advertising device
      foundDevice = new BLEAdvertisedDevice(advertisedDevice); // create object to store found device
      doConnect = true; // turn on signal to connect
      pBLEScan->stop(); // stop the scan
    }  
  }  // onResult
};  // MyAdvertisedDeviceCallbacks

// Helper function to connect BLE Client to found BLE advertising device
bool connectToDevice() { 
  pClient = BLEDevice::createClient(); // create client
  if (foundDevice && pClient->connect(foundDevice)) { // connect to found device
    isConnected = true; // set connected flag
  }
  return true;
}
// ^^^^ BLE VARIABLES AND FUNCTIONS ^^^^

// ---- ESP-NOW VARIABLES AND FUNCTIONS ----
typedef struct message { // Structure to send data - must match the sender structure
  int rssi;
} message;
message received_message; // create an instance of the struct message

// ESP_NOW CALLBACK: executed when data is received
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len >= sizeof(received_message)) {
    memcpy(&received_message, data, sizeof(received_message));
    startedRSSI = true;
    lastReceivedTime = millis();
  }
}
// ^^^^ ESP-NOW VARIABLES AND FUNCTIONS ^^^^

// ========= SETUP =========
void setup() {
  Serial.begin(115200);
  Serial.println("Hello");
  pinMode(A_PWM, OUTPUT);
  pinMode(A_IN1, OUTPUT);
  pinMode(A_IN2, OUTPUT);
  pinMode(B_PWM, OUTPUT);
  pinMode(B_IN1, OUTPUT);
  pinMode(B_IN2, OUTPUT);

  // ----- BLE Initialization ------
  BLEDevice::init(""); // initialize scanner
  pBLEScan = BLEDevice::getScan(); // create scanner
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks()); // set callbacks
  pBLEScan->setInterval(1349); // set scan interval
  pBLEScan->setWindow(449); // set scan window
  pBLEScan->setActiveScan(true); // set active - ask for more data, better RSSI
  pBLEScan->start(0, false); // start initial scan

  // ----- ESP-NOW Initialization ------
  WiFi.mode(WIFI_STA); // Enable WiFi station
  WiFi.disconnect(); // prevent previous WiFi connections

  if (esp_now_init() != ESP_OK) { // initialize esp-now
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the receiver callback
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println(WiFi.macAddress());
}

// Simple helpers: assume IN1=HIGH / IN2=LOW = forward
void driveForward(int speed) {
  // Motor A (left)
  digitalWrite(A_IN1, HIGH);
  digitalWrite(A_IN2, LOW);
  analogWrite(A_PWM, speed);

  // Motor B (right)
  digitalWrite(B_IN1, HIGH);
  digitalWrite(B_IN2, LOW);
  analogWrite(B_PWM, speed);
}

void turnLeft(int speed) {
  // Left motor backwards, right motor forwards
  digitalWrite(A_IN1, LOW);
  digitalWrite(A_IN2, HIGH);
  analogWrite(A_PWM, speed);

  digitalWrite(B_IN1, HIGH);
  digitalWrite(B_IN2, LOW);
  analogWrite(B_PWM, speed);
}

void turnRight(int speed) {
  // Left motor forwards, right motor backwards
  digitalWrite(A_IN1, HIGH);
  digitalWrite(A_IN2, LOW);
  analogWrite(A_PWM, speed);

  digitalWrite(B_IN1, LOW);
  digitalWrite(B_IN2, HIGH);
  analogWrite(B_PWM, speed);
}

void stopAllMotors() {
  digitalWrite(A_IN1, LOW);
  digitalWrite(A_IN2, LOW);
  analogWrite(A_PWM, 0);

  digitalWrite(B_IN1, LOW);
  digitalWrite(B_IN2, LOW);
  analogWrite(B_PWM, 0);
}

void updateMotorsWithRSSI(int rssi) {
  Serial.print("RSSI: ");
  Serial.println(rssi);

  // First reading: start driving forward toward the user
  if (!haveRssi) {
    driveForward(BASE_SPEED);
    lastRssi = rssi;
    haveRssi = true;
    return;
  }

  int diff = rssi - lastRssi;  // >0 means RSSI got stronger (less negative)

  if (diff >= RSSI_DELTA_THRES) {
    // Signal got noticeably stronger -> keep going forward
    driveForward(BASE_SPEED);
  } else if (diff <= -RSSI_DELTA_THRES) {
    // Signal got weaker -> we’re going the wrong way / orientation
    // Try turning to search for a better direction
    static bool turnLeftFlag = true;
    if (turnLeftFlag) {
      turnLeft(TURN_SPEED);
    } else {
      turnRight(TURN_SPEED);
    }
    turnLeftFlag = !turnLeftFlag;
  } else {
    // Small change, just keep doing what we’re doing
    // (optionally maintain last command)
  }

  lastRssi = rssi;
}


// ========== LOOP ==========
void loop() {
  // Connect to BLE device (doConnect is set by advertising callback)
  if (doConnect and connectToDevice()) {
    doConnect = false; // stop trying to connect
  }
  
  //Serial.println(pClient->isConnected());
  // If connectToDevice() is successful, continuously scan RSSI and send to main ESP32 through ESP_NOW
  if (isConnected) {
    // If client is connected to BLE, check for RSSI
    if (pClient->isConnected()) {
      if (startedRSSI) { // if RSSI communication has started
        if (millis() - lastReceivedTime < 1000) { // Check if anything has been sent in the past second
          int received_rssi = received_message.rssi; // get the rssi value
          Serial.print("Alarm RSSI: "); Serial.println(pClient->getRssi()); // print for debug
          Serial.print("Other RSSI: "); Serial.println(received_rssi); // print for debug

          // ======MOTOR LOGIC HERE===============
          if (abs(received_rssi) <= abs(pClient->getRssi())) {
            // if helper ESP-32 is closer, move in direction away from helper
            Serial.println("helper is closer");
            turnLeft(TURN_SPEED);
          } else {
            // if helper ESP-32 is farther, move in direction towards helper
            Serial.println("alarm is closer");
            driveForward(BASE_SPEED);
          } // movement logic ==============
        } else {
          startedRSSI = false; // reset flag
        } // lastReceivedTime
      } // startedRSSI
    } else { // If client disconnected, reset connection flags and re-scan
        Serial.println("disconnected");
        isConnected = false;
        foundDevice = NULL;
        pBLEScan->clearResults();
        pBLEScan->start(0, false); // clear results and scan again
        stopAllMotors();

    } // if BLE connected
  }
  // delay(1000);
}
