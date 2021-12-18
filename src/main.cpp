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
    } else {
        Serial.println("Delete failed");
    }
}

void setup() {
    //USE PIN 22 as GND
    pinMode(22, OUTPUT);
    digitalWrite(22, LOW);

    //Serial RUN for TESTING
    Serial.begin(115200);

    //I2C RUN
    Wire.begin(SDA_PIN, SCL_PIN);
    mySensor.setWire(&Wire);

    //SPI RUN SD CARD
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    if (!SD.begin(CS_PIN)) {
        Serial.println("initialization failed!");
        sdCardConnected = false;
    }
    Serial.println("initialization done.");
    sdCardConnected = true;
    deleteFile(SD, "/data.txt");
    // open a new file and immediately close it:
    File file = SD.open("/data.txt");
    if (!file) {
        Serial.println("File doens't exist");
        Serial.println("Creating file...");
        writeFile(SD, "/data.txt",
                  "Time, Temperature, Pressure, height, aX, aY, aZ, aSqrt, gX, gY, gZ, battery percentage\r\n");
    } else {
        Serial.println("File already exists");
    }
    file.close();

    // MPU9250_asukiaaa RUN
    mySensor.beginAccel();
    mySensor.beginGyro();

    // BMP280 RUN
    bmp.begin(BMP280_ADDRESS_ALT);
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

    if (bmp.begin() == 1) {
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
                 "{\"time\": %f, \"temp\": %f, \"pressure\": %f, \"altitude\": %f, \"aX\": %f, \"aY\": %f, \"aZ\": %f, \"aSqrt\": %f, \"gX\": %f, \"gY\": %f, \"gZ\": %f, \"battery\": %f}",
                 timestamp, temp, press, latt, aX, aY, aZ, aSqrt, gX, gY, gZ, batt);
        customCharacteristic.setValue(s);
        customCharacteristic.notify();
        delay(1000);
    }

    if (sdCardConnected) {
        String message =
                String(timestamp) + " , " + String(temp) + " , " + String(press) + " , " + String(latt) + " , " +
                String(aX) + " , " + String(aY) +
                " , " + String(aZ) + " , " + String(aSqrt) + " , " + String(gX) + " , " + String(gY) + " , " +
                String(gZ) + " , " + String(batt);
        myFile = SD.open("/data.txt", FILE_APPEND);
        if (!myFile) {
            Serial.println("Failed to open file for appending");
            return;
        }
        if (myFile.println(message)) {
            Serial.println("Data appended");
        } else {
            Serial.println("Append failed");
        }
        myFile.close();
        delay(1000);
    }
}