#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <MPU9250_asukiaaa.h>
#include <Adafruit_BMP280.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Battery18650Stats.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// Battery DEFINE
#define ADC_PIN 36
// I2C DEFINE
#define SDA_PIN 23
#define SCL_PIN 19

// SPI DEFINE SD CARD
#define MISO_PIN 27
#define MOSI_PIN 25
#define CS_PIN 5
#define SCK_PIN 18

// BATTERY OVERRIDING and BMP280
#ifdef DEFAULT_PIN
#undef DEFAULT_PIN
#define DEFAULT_PIN 36
#endif

#ifdef DEFAULT_CONVERSION_FACTOR
#undef DEFAULT_CONVERSION_FACTOR
#define DEFAULT_CONVERSION_FACTOR 1.832
#endif

// GPS DEFINE
TinyGPSPlus gps;
HardwareSerial gps_serial(1);
double GPSLat, GPSLng, GPSAlt, GPSSpeed, GPSCourse;
uint32_t GPSSatCount;
uint16_t Year;
uint8_t Month, Day, Hour, Minute, Second, CentiSecound;
char GPSErr[] = "Not Connected";
char DateString [10];
char TimeString [15];

//BLE DEFINE
#define serviceID BLEUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
BLECharacteristic customCharacteristic(
        BLEUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8"),
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
);

bool deviceConnected = false;
bool sdCardConnected = false;
//SDCARD
File myFile;
//BATTERY
Battery18650Stats battery(ADC_PIN);
//I2C device found at address 0x68 - mpu9250
MPU9250_asukiaaa mySensor;
float aX, aY, aZ, aSqrt, gX, gY, gZ, mDirection, mX, mY, mZ, temp, press, latt, batt;
//I2C device found at address 0x76 - bmp280
Adafruit_BMP280 bmp;

char value[1024] = "Default";
float firstPress = 0;


//Sending Data
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *MyServer) {
        deviceConnected = true;
    };

    void onDisconnect(BLEServer *MyServer) {
        deviceConnected = false;
    }
};

//Receiving data
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *customCharacteristic) {
        std::string rcvString = customCharacteristic->getValue();
        if (rcvString.length() > 0) {
            for (int i = 0; i < rcvString.length(); ++i) {
                Serial.print(rcvString[i]);
                value[i] = rcvString[i];
            }
            for (int i = rcvString.length(); i < 50; ++i) {
                value[i] = 0;
            }
            customCharacteristic->setValue((char *) &value);
        } else {
            Serial.println("Empty Value Received!");
        }
    }
};

//Write to the SD card
void writeFile(fs::FS &fs, const char *path, const char *message) {
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    if (file.print(message)) {
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

//Delete files from SD card
void deleteFile(fs::FS &fs, const char *path) {
    Serial.printf("Deleting file: %s\n", path);
    if (fs.remove(path)) {
        Serial.println("File deleted");
    }
    else {
        Serial.println("Delete failed");
    }
}
void displayInfo()
{
    GPSLat = gps.location.lat(), GPSLng = gps.location.lng(), GPSAlt = gps.altitude.meters(), GPSSpeed = gps.speed.mps(), GPSCourse = gps.course.deg();
    GPSSatCount = gps.satellites.value(), Year = gps.date.year(), Month = gps.date.month(), Day = gps.date.day(), Hour = gps.time.hour()+1, Minute = gps.time.minute(), Second = gps.time.second(), CentiSecound = gps.time.centisecond();

    Serial.print(F("GPS Sat Count: "));
    Serial.print(GPSSatCount);
    Serial.print(F(" Location: "));
    if (gps.location.isValid()){
        Serial.print(GPSLat, 6);
        Serial.print(F(","));
        Serial.print(GPSLng, 6);
    }
    else{
        Serial.print(GPSErr);
    }

    Serial.print(F("  Altitude: "));
    if (gps.altitude.isValid()){
        Serial.print(GPSAlt);
    }
    else{
        Serial.print(GPSErr);
    }
    Serial.print(F("  Course: "));
    if (gps.course.isValid()){
        Serial.print(GPSCourse);
    }
    else{
        Serial.print(GPSErr);
    }
    Serial.print(F("  Speed: "));
    if (gps.speed.isValid()){
        Serial.print(GPSSpeed);
    }
    else{
        Serial.print(GPSErr);
    }
    Serial.print(F("  Date: "));
    if (gps.date.isValid())
    {
        sprintf_P(DateString, PSTR("%4u-%02u-%02u"), Year, Month, Day);
        Serial.print(DateString);
    }
    else
    {
        Serial.print(GPSErr);
    }

    Serial.print(F(" Time: "));
    if (gps.time.isValid()){
        sprintf_P(TimeString, PSTR("%02u:%02u:%02u:%03u"), Hour, Minute, Second, CentiSecound);
        Serial.print(TimeString);
    }
    else{
        Serial.print(GPSErr);
    }
    Serial.println();
}

void setup() {
    //USE PIN 22 as GND
    pinMode(22, OUTPUT);
    digitalWrite(22, LOW);

    //Serial and GPS
    Serial.begin(115200);
    gps_serial.begin(9600, SERIAL_8N1, 13, 12);


    //I2C RUN
    Wire.begin(SDA_PIN, SCL_PIN);
    mySensor.setWire(&Wire);

    //SPI RUN SD CARD
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    if (!SD.begin(CS_PIN)) {
        Serial.println("initialization failed!");
        sdCardConnected = false;
    } else {
        Serial.println("initialization done.");
        sdCardConnected = true;
    }
    deleteFile(SD, "/data.txt");
    // open a new file and immediately close it:
    File file = SD.open("/data.txt");
    if (!file) {
        Serial.println("File doens't exist");
        Serial.println("Creating file...");
        writeFile(SD, "/data.txt",
                  "Time, Temperature, Pressure, height, aX, aY, aZ, aSqrt, gX, gY, gZ, battery percentage,GPSValue, GPSLat, GPSLng, GPSAlt, GPSCourse, GPSSpeed, GPSDate, GPSTime\r\n");
    } else {
        Serial.println("File already exists");
    }
    file.close();

    // MPU9250_asukiaaa RUN
    mySensor.beginAccel();
    mySensor.beginGyro();

    // BMP280 RUN
    bmp.begin(0x76,0x58);
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    firstPress = bmp.readPressure() / 100;

    // BLE CONFIGURATION
    /* Create the BLE Server */
    BLEDevice::init("ROCKET PUT"); // BLE DEV NAME
    BLEServer *MyServer = BLEDevice::createServer();  //Create the BLE Server
    MyServer->setCallbacks(new ServerCallbacks());  // Set the function that handles server callbacks
    BLEService *customService = MyServer->createService(serviceID); // Create the BLE Service
    customService->addCharacteristic(&customCharacteristic);  // Create a BLE Characteristic
    BLE2902 *basicDescriptor = new BLE2902();
    basicDescriptor->setNotifications(true);
    customCharacteristic.addDescriptor(basicDescriptor);
    customCharacteristic.setCallbacks(new MyCharacteristicCallbacks());
    MyServer->getAdvertising()->addServiceUUID(serviceID);  // Configure Advertising
    customService->start(); // Start the service
    MyServer->getAdvertising()->start();  // Start the server/advertising
}

void loop() {
    float timestamp = millis() / 1000;
    batt = battery.getBatteryChargeLevel(true);
    if (mySensor.accelUpdate() == 0) {
        aX = mySensor.accelX();
        aY = mySensor.accelY();
        aZ = mySensor.accelZ();
        aSqrt = mySensor.accelSqrt();
    } else {
        Serial.println("Cannod read accel values");
    }

    if (mySensor.gyroUpdate() == 0) {
        gX = mySensor.gyroX();
        gY = mySensor.gyroY();
        gZ = mySensor.gyroZ();
    } else {
        Serial.println("Cannot read gyro values");
    }

    if (bmp.begin(0x76) == 1) {
        temp = bmp.readTemperature();
        press = bmp.readPressure() / 100;
        latt = bmp.readAltitude(firstPress); //<-- Put here your Sea Level Pressure (hPa)

    } else {
        Serial.println("Cannot read BMP280 values");
    }
    //SENDING DATA VIA BLE

    if (deviceConnected) {
        char s[1024];
        snprintf(s, sizeof(s),
                 "{\"time\": %f, \"temp\": %f, \"pressure\": %f, \"altitude\": %f, \"aX\": %f, \"aY\": %f, \"aZ\": %f, \"aSqrt\": %f, \"gX\": %f, \"gY\": %f, \"gZ\": %f, \"battery\": %f, \"GPSSatCount\": %u, \"GPSLat\": %f, \"GPSLng\": %f, \"GPSAlt\": %f, \"GPSCourse\": %f, \"GPSSpeed\": %f, \"GPSDate\": %s, \"GPSTime\": %s}",
                 timestamp, temp, press, latt, aX, aY, aZ, aSqrt, gX, gY, gZ, batt, GPSSatCount, GPSLat, GPSLng, GPSAlt, GPSCourse, GPSSpeed, DateString, TimeString );
        customCharacteristic.setValue(s);
        customCharacteristic.notify();
        delay(1000);
    }

    if (sdCardConnected) {
        String message =
                String(timestamp) + " , " + String(temp) + " , " + String(press) + " , " + String(latt) + " , " +
                String(aX) + " , " + String(aY) +
                " , " + String(aZ) + " , " + String(aSqrt) + " , " + String(gX) + " , " + String(gY) + " , " +
                String(gZ) + " , " + String(batt)  + " , " + String(GPSSatCount) + " , " + String(GPSLat,6) + " , " + String(GPSLng,6) + " , " + String(GPSAlt) + " , " + String(GPSCourse) + " , " + String(GPSSpeed) + " , " + String(DateString) + " , " + String(TimeString);
        myFile = SD.open("/data.txt", FILE_APPEND);
        if (!myFile) {
            Serial.println("Failed to open file for appending");
            return;
        }
        if (myFile.println(message)) {
           // Serial.println("Data appended");
        } else {
            Serial.println("Append failed");
        }
        myFile.close();
        delay(1000);
    }

    while (gps_serial.available() > 0)
        if (gps.encode(gps_serial.read()))
            displayInfo();

    if (millis() > 5000 && gps.charsProcessed() < 10)
    {
        Serial.println(F("No GPS detected: check wiring."));
    }
}