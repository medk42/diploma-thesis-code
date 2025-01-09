#include <Adafruit_SPIFlash.h>
#include <LSM6DS3.h>
#include <Wire.h>
#include <bluefruit.h>
#include <nrf52840.h>
#include <nrf_power.h>

#define LEDR LED_RED
#define LEDG LED_GREEN
#define LEDB LED_BLUE

#define BUT_PRIMARY_PIN D1
#define BUT_SECONDARY_PIN D2

#define FLAG_VALID 1
#define FLAG_BUT_PRIM_PRESSED 2
#define FLAG_BUT_SEC_PRESSED 4

const unsigned long delayMs = 7;
const unsigned long rate = 1000/delayMs;
const unsigned long stayAwakeTimeMs = 1000*60;

struct IMUDataPacket {
  int16_t accel[3];
  int16_t gyro[3];
  uint16_t flags; // 1 = valid, 2 = button primary, 4 = button secondary
};

const char* DEVICE_NAME = "3D Pen";

// 2bfae565-df4e-45b6-b1fa-a6f75c1be2b3
const uint8_t UUID_SERVICE[] =
{
  0xb3, 0xe2, 0x1b, 0x5c, 0xf7, 0xa6, 0xfa, 0xb1, 0xb6, 0x45, 0x4e, 0xdf, 0x65, 0xe5, 0xfa, 0x2b
};

// e76d106d-a549-4b3a-afbd-8879582943fe
const uint8_t UUID_CHARACTERISTIC[] =
{
  0xfe, 0x43, 0x29, 0x58, 0x79, 0x88, 0xbd, 0xaf, 0x3a, 0x4b, 0x49, 0xa5, 0x6d, 0x10, 0x6d, 0xe7
};

BLEService bleService(UUID_SERVICE);
BLECharacteristic imuCharacteristic(UUID_CHARACTERISTIC);

Adafruit_FlashTransport_QSPI flashTransport;
LSM6DS3 imu(I2C_MODE, 0x6A); //I2C device address 0x6A

// Disable QSPI flash to save power
void QSPIF_sleep(void)
{
  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  flashTransport.end();
}

void imuISR() {
  // Interrupt triggers for both single and double taps, so we need to check which one it was.
  uint8_t tapSrc;
  status_t status = imu.readRegister(&tapSrc, LSM6DS3_ACC_GYRO_TAP_SRC);
  bool wasDoubleTap = (tapSrc & LSM6DS3_ACC_GYRO_DOUBLE_TAP_EV_STATUS_DETECTED) > 0;
  if (!wasDoubleTap) {
    nrf_power_system_off(NRF_POWER);
  }
}

void setupWakeUpInterrupt()
{
  // Tap interrupt code is based on code by daCoder
  // https://forum.seeedstudio.com/t/xiao-sense-accelerometer-examples-and-low-power/270801
  imu.settings.gyroEnabled = 0;
  imu.settings.accelEnabled = 1;
  imu.settings.accelSampleRate = 104;
  imu.settings.accelRange = 2;
  imu.begin();

  //https://www.st.com/resource/en/datasheet/lsm6ds3tr-c.pdf
  // imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1, 0b10001000); // Enable interrupts and tap detection on X-axis
  imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1, 0b10001110); // Enable interrupts and tap detection on all axes
  // imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_THS_6D, 0b10001000); // Set tap threshold
  imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_THS_6D, 0b10000100); // Set tap threshold
  const int duration = 0b0010 << 4; // 1LSB corresponds to 32*ODR_XL time
  const int quietTime = 0b10 << 2; // 1LSB corresponds to 4*ODR_XL time
  const int shockTime = 0b01 << 0; // 1LSB corresponds to 8*ODR_XL time
  imu.writeRegister(LSM6DS3_ACC_GYRO_INT_DUR2, duration | quietTime | shockTime); // Set Duration, Quiet and Shock time windows
  imu.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS, 0x80); // Single & double-tap enabled (SINGLE_DOUBLE_TAP = 1)
  imu.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG, 0x08); // Double-tap interrupt driven to INT1 pin
  imu.writeRegister(LSM6DS3_ACC_GYRO_CTRL6_G, 0x10); // High-performance operating mode disabled for accelerometer

  // Set up the sense mechanism to generate the DETECT signal to wake from system_off
  pinMode(PIN_LSM6DS3TR_C_INT1, INPUT_PULLDOWN_SENSE);
  attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), imuISR, CHANGE);

  return;
}

void setupButtons() {
  pinMode(BUT_PRIMARY_PIN, INPUT_PULLUP);
  pinMode(BUT_SECONDARY_PIN, INPUT_PULLUP);
}

void setupImu() {
  imu.settings.accelRange = 4; // Can be: 2, 4, 8, 16
  imu.settings.gyroRange = 500; // Can be: 125, 245, 500, 1000, 2000
  imu.settings.accelSampleRate = 416; //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666, 3332, 6664, 13330
  imu.settings.gyroSampleRate = 416; //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666
  imu.settings.accelBandWidth = 200;
  imu.settings.gyroBandWidth = 200;
  imu.begin();
}

void runBle() {
  Serial.println("Running BLE");
  while (Bluefruit.connected(0)) {
    unsigned long startTime = millis();

    IMUDataPacket packet;
    // This could be optimised by reading all values as a block.
    packet.accel[0] = imu.readRawAccelX();
    packet.accel[1] = imu.readRawAccelY();
    packet.accel[2] = imu.readRawAccelZ();
    packet.gyro[0] = imu.readRawGyroX();
    packet.gyro[1] = imu.readRawGyroY();
    packet.gyro[2] = imu.readRawGyroZ();

    packet.flags = FLAG_VALID;
    if (digitalRead(BUT_PRIMARY_PIN) == LOW) { // primary button pressed
      packet.flags |= FLAG_BUT_PRIM_PRESSED;
    }
    if (digitalRead(BUT_SECONDARY_PIN) == LOW) { // secondary button pressed
      packet.flags |= FLAG_BUT_SEC_PRESSED;
    }
    
    imuCharacteristic.notify(&packet, sizeof(packet));

    // Inaccurate but usable way to throttle the rate of measurements.
    unsigned long time = millis() - startTime;
    unsigned long waitPeriod = delayMs - time;
    if (waitPeriod > 0 && waitPeriod < 500) { // protection against overflow issues
      delay(waitPeriod);
    }
  }
  Serial.println("Stopped BLE");
}

void startAdvertising()
{
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);  // supposedly "makes the device visible to all BLE devices in general discoverable mode"
  Bluefruit.Advertising.addTxPower();               // add transmission power to advertisment (helps determine signal range)
  Bluefruit.Advertising.addService(bleService);     // advertise the "bleService"

  Bluefruit.ScanResponse.addName();                 // add device name to advertising response
  
  Bluefruit.Advertising.restartOnDisconnect(true);  // start advertising again automatically if connected device disconnects
  Bluefruit.Advertising.setInterval(128, 488);      // in unit of 0.625 ms, fast/slow mode advertising period
  Bluefruit.Advertising.setFastTimeout(30);         // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                   // 0 = Don't stop advertising after n seconds
}

void sleepUntilDoubleTap() {
  digitalWrite(LEDR, HIGH);
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDB, HIGH);

  Serial.println("Setting up interrupt");
  // Setup up double tap interrupt to wake back up
  setupWakeUpInterrupt();

  Serial.println("Entering sleep");
  Serial.flush();

  // Execution should not go beyond this
  nrf_power_system_off(NRF_POWER);
}

void setup() {
  Serial.begin(9600);
  // TODO DISABLE THIS
  // while (!Serial && millis() < 1000); // Timeout in case serial disconnected. 
  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDR, LOW);
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDB, HIGH);

  QSPIF_sleep();

  // Setup the BLE LED to be enabled on CONNECT, by false we disable it
  Bluefruit.autoConnLed(false);
  Serial.println("Initialise the Bluefruit nRF52 module");
  // TODO BANDWIDTH_LOW should be enough (20B), but causes 15ms frame time instead of 7ms (NORMAL too)
  Bluefruit.configPrphBandwidth(BANDWIDTH_HIGH); // max is BANDWIDTH_MAX = 4, allows for 247B; but BANDWIDTH_LOW (20B) should suffice
  Serial.print("Begin Bluefruit: ");
  Serial.println(Bluefruit.begin(1, 0));   // initialize device as 1 peripheral and 0 central devices
  Bluefruit.Periph.setConnInterval(6, 6);  // in unit of 1.25ms, so 6*1.25=7.5ms
  Bluefruit.setName(DEVICE_NAME);             // set name for device, to be advertised in startAdvertising
  Serial.println("Begin bleService");
  bleService.begin();                      // make the service active and ready to offer its characteristics

  imuCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);  // make the characteristic READable and able to NOTIFY central device of changes (central device can subscribe)
  imuCharacteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);    // security + access permissions, here anyone can access it and no special permissions are set
  imuCharacteristic.setFixedLen(sizeof(IMUDataPacket));                // fix the length of data for this characteristic
  Serial.println("Begin imuCharacteristic");
  imuCharacteristic.begin();
  // IMUDataPacket initialPacket = { 0 };
  // imuCharacteristic.write(&initialPacket, sizeof(initialPacket));

  Serial.println("Setup finished");
}

void loop() {
  Serial.print("Starting advertising...");
  startAdvertising();               // start advertising the device, advertising will automatically restart on device disconnect (we only need to start it once)
  digitalWrite(LEDR, HIGH);
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDB, LOW);

  Serial.print("Starting IMU...");
  setupImu();

  unsigned long wakeUpTime = millis();

  while (millis() - wakeUpTime < stayAwakeTimeMs) {
    if (Bluefruit.connected(0)) {    // checks if any device is currently connected
      Serial.println("Connected");
      setupButtons();
      digitalWrite(LEDR, HIGH);
      digitalWrite(LEDG, LOW);
      digitalWrite(LEDB, HIGH);
      runBle();
      digitalWrite(LEDR, HIGH);
      digitalWrite(LEDG, HIGH);
      digitalWrite(LEDB, LOW);
      wakeUpTime = millis();
    }
    // Don't sleep if USB connected, to make code upload easier.
    if (Serial) wakeUpTime = millis();
    delay(100);
  }
  Serial.println("Stopping advertising");
  Bluefruit.Advertising.stop();     // stop advertising before going to deep sleep
  sleepUntilDoubleTap();
}
