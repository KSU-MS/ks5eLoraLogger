
/*
 * Teensy 4.1 crude logger
 yoinked from hytech racing "telemetry control unit" repo
 removed all features except can logging to SD
 */
#include <Arduino.h>
#include <SD.h>
#include <Wire.h>
#include <TimeLib.h>
#include <Metro.h>
#include <FlexCAN_T4.h>
#include <RadioLib.h>
#define pin_cs 10
#define pin_dio0 6
#define pin_nrst 7
#define pin_dio1 5
SX1276 radio = new Module(pin_cs, pin_dio0, pin_nrst, pin_dio1);
/*
 * CAN Variables
 */
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> CAN;
static CAN_message_t msg_rx;
static CAN_message_t msg_tx;
// static CAN_message_t xb_msg;
File logger;
/*
 * Variables to help with time calculation
 */
uint64_t global_ms_offset = 0;
uint64_t last_sec_epoch;
Metro timer_debug_RTC = Metro(1000);
Metro timer_flush = Metro(500);
void parse_can_message();
void write_to_SD(CAN_message_t *msg);
time_t getTeensy3Time();
void sd_date_time(uint16_t* date, uint16_t* time);
void setup() {
  delay(500); //Wait for ESP32 to be able to print

  Serial.print(F("[SX1276] Initializing ... "));
  //int state = radio.begin(); //-121dBm
  //int state = radio.begin(868.0); //-20dBm
  int state = radio.beginFSK(915.0,300); //-23dBm
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("init success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }

  // set output power to 10 dBm (accepted range is -3 - 17 dBm)
  // NOTE: 20 dBm value allows high power operation, but transmission
  //       duty cycle MUST NOT exceed 1%
//  if (radio.setOutputPower(20) == ERR_INVALID_OUTPUT_POWER) {
//    Serial.println(F("Selected output power is invalid for this module!"));
//    while (true);
//  }

  // some modules have an external RF switch
  // controlled via two pins (RX enable, TX enable)
  // to enable automatic control of the switch,
  // call the following method
  int pin_rx_enable = 5;
  int pin_tx_enable = 6;
  radio.setRfSwitchPins(pin_rx_enable, pin_tx_enable);
    //SD logging init stuff
    delay(5000); // Prevents suprious text files when turning the car on and off rapidly
    pinMode(LED_BUILTIN,OUTPUT);
    /* Set up Serial, CAN */
    //Serial.begin(115200);

    /* Set up real-time clock */
    //Teensy3Clock.set(1656732300); // set time (epoch) at powerup  (COMMENT OUT THIS LINE AND PUSH ONCE RTC HAS BEEN SET!!!!)
    setSyncProvider(getTeensy3Time); // registers Teensy RTC as system time
    if (timeStatus() != timeSet) {
        Serial.println("RTC not set up - uncomment the Teensy3Clock.set() function call to set the time");
    } else {
        Serial.println("System time set to RTC");
    }
    last_sec_epoch = Teensy3Clock.get();
    
    //FLEXCAN0_MCR &= 0xFFFDFFFF; // Enables CAN message self-reception
    CAN.begin();
    CAN.setBaudRate(1000000);
    /* Set up SD card */
    Serial.println("Initializing SD card...");
    SdFile::dateTimeCallback(sd_date_time); // Set date/time callback function
    if (!SD.begin(BUILTIN_SDCARD)) { // Begin Arduino SD API (Teensy 3.5)
        Serial.println("SD card failed or not present");
    }
    char filename[] = "data0000.CSV";
    for (uint8_t i = 0; i < 10000; i++) {
        filename[4] = i / 1000     + '0';
        filename[5] = i / 100 % 10 + '0';
        filename[6] = i / 10  % 10 + '0';
        filename[7] = i       % 10 + '0';
        if (!SD.exists(filename)) {
            logger = SD.open(filename, (uint8_t) O_WRITE | (uint8_t) O_CREAT); // Open file for writing
            break;
        }
        if (i == 9999) { // If all possible filenames are in use, print error
            Serial.println("All possible SD card log filenames are in use - please clean up the SD card");
        }
    }
    
    if (logger) {
        Serial.print("Successfully opened SD file: ");
        Serial.println(filename);
    } else {
        Serial.println("Failed to open SD file");
    }
    
    logger.println("time,msg.id,msg.len,data"); // Print CSV heading to the logfile
    logger.flush();
}
void loop() {
  digitalWrite(LED_BUILTIN,LOW);
    /* Process and log incoming CAN messages */
    parse_can_message();
    /* Flush data to SD card occasionally */
    if (timer_flush.check()) {
        logger.flush(); // Flush data to disk (data is also flushed whenever the 512 Byte buffer fills up, but this call ensures we don't lose more than a second of data when the car turns off)
    }
    /* Print timestamp to serial occasionally */
    if (timer_debug_RTC.check()) {
        Serial.println(Teensy3Clock.get());
        CAN_message_t msg_tx;
        msg_tx.id=0x3FF;
        CAN.write(msg_tx);
    }
}
void parse_can_message() {
    while (CAN.read(msg_rx)) {

        write_to_SD(&msg_rx); // Write to SD card buffer (if the buffer fills up, triggering a flush to disk, this will take 8ms)
        
    }
}
void write_to_SD(CAN_message_t *msg) { // Note: This function does not flush data to disk! It will happen when the buffer fills or when the above flush timer fires
    // Calculate Time
    //This block is verified to loop through
    digitalWrite(LED_BUILTIN,HIGH);

    uint64_t sec_epoch = Teensy3Clock.get();
    if (sec_epoch != last_sec_epoch) {
        global_ms_offset = millis() % 1000;
        last_sec_epoch = sec_epoch;
    }
    uint64_t current_time = sec_epoch * 1000 + (millis() - global_ms_offset) % 1000;

    // Log to SD
    logger.print(current_time);
    logger.print(",");
    logger.print(msg->id, HEX);
    logger.print(",");
    logger.print(msg->len);
    logger.print(",");
    for (int i = 0; i < msg->len; i++) {
        if (msg->buf[i] < 16) {
            logger.print("0");
        }
        logger.print(msg->buf[i], HEX);
    }
    logger.println();
}
time_t getTeensy3Time() {
    return Teensy3Clock.get();
}
void sd_date_time(uint16_t* date, uint16_t* time) {
    // return date using FAT_DATE macro to format fields
    *date = FAT_DATE(year(), month(), day());
    // return time using FAT_TIME macro to format fields
    *time = FAT_TIME(hour(), minute(), second());
}