// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
//

#pragma once

//#define CONFIG_SW_COEXIST_ENABLE 1

#include <memory>
#include <NimBLEDevice.h>
#include <Arduino.h>

//Heart Service
#define HEARTSERVICE_UUID BLEUUID((uint16_t)0x180D)
#define HEARTCHARACTERISTIC_UUID BLEUUID((uint16_t)0x2A37)

//Cycling Power Service
#define CSCSERVICE_UUID BLEUUID((uint16_t)0x1816)
#define CSCMEASUREMENT_UUID BLEUUID((uint16_t)0x2A5B)
#define CYCLINGPOWERSERVICE_UUID BLEUUID((uint16_t)0x1818)
#define CYCLINGPOWERMEASUREMENT_UUID BLEUUID((uint16_t)0x2A63)
#define CYCLINGPOWERFEATURE_UUID BLEUUID((uint16_t)0x2A65)
#define SENSORLOCATION_UUID BLEUUID((uint16_t)0x2A5D)

//Fitness Machine Service
#define FITNESSMACHINESERVICE_UUID BLEUUID((uint16_t)0x1826)
#define FITNESSMACHINEFEATURE_UUID BLEUUID((uint16_t)0x2ACC)
#define FITNESSMACHINECONTROLPOINT_UUID BLEUUID((uint16_t)0x2AD9)
#define FITNESSMACHINESTATUS_UUID BLEUUID((uint16_t)0x2ADA)
#define FITNESSMACHINEINDOORBIKEDATA_UUID BLEUUID((uint16_t)0x2AD2)
#define FITNESSMACHINERESISTANCELEVELRANGE_UUID BLEUUID((uint16_t)0x2AD6)
#define FITNESSMACHINEPOWERRANGE_UUID BLEUUID((uint16_t)0x2AD8)

// GATT service/characteristic UUIDs for Flywheel Bike from ptx2/gymnasticon/
#define FLYWHEEL_UART_SERVICE_UUID BLEUUID((uint16_t)0xCA9E)
#define FLYWHEEL_UART_RX_UUID BLEUUID((uint16_t)0xCA9E)
#define FLYWHEEL_UART_TX_UUID BLEUUID((uint16_t)0xCA9E)

// macros to convert different types of bytes into int The naming here sucks and should be fixed.
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))
#define bytes_to_int(MSB, LSB) (((int)((unsigned char)MSB))) << 8 | (((unsigned char)LSB))
// Potentially, something like this is a better way of doing this ^^  data.getUint16(1, true)

//Setup
void setupBLE();

//*****************************Server*****************************
extern bool GlobalBLEClientConnected;
void startBLEServer();
void BLENotify(void *pvParameters);
void computeERG(int, int);
void computeCSC();
void updateIndoorBikeDataChar();
void updateCyclingPowerMesurementChar();
void calculateInstPwrFromHR();

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *, ble_gap_conn_desc* desc);
    void onDisconnect(BLEServer *);
};

class MyCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *);
};

//*****************************Client*****************************

//Keeping the task outside the class so we don't need a mask. 
//We're only going to run one anyway.
void bleClientTask(void *pvParameters);  

class SpinBLEClient{ 
    
    public: //Not all of these need to be public. This should be cleaned up later.
    boolean doConnectPM         = false;
    boolean doConnectHR         = false;
    boolean connectedPM         = false;
    boolean connectedHR         = false;
    boolean doScan              = false;
    float crankRev[2]           = {0, 0};
    float crankEventTime[2]     = {0, 0};
    int noReadingIn             = 0;
    int cscCumulativeCrankRev   = 0;
    int cscLastCrankEvtTime     = 0;
    int lastConnectedPMID       = 0;

    BLERemoteCharacteristic *pRemoteCharacteristic  = nullptr;
    BLEAdvertisedDevice     *myPowerMeter           = nullptr;
    BLEAdvertisedDevice     *myHeartMonitor         = nullptr;

    void start();
    void serverScan(bool connectRequest);   
    bool connectToServer();
    void scanProcess();
    void disconnect();

private:
    
    class MyAdvertisedDeviceCallback : public NimBLEAdvertisedDeviceCallbacks
    {
       public:
       void onResult(NimBLEAdvertisedDevice *);
    };

    class MyClientCallback : public NimBLEClientCallbacks
    {
        public:
        void        onConnect(BLEClient *);
        void        onDisconnect(BLEClient *);
        uint32_t    onPassKeyRequest();
        bool        onConfirmPIN(uint32_t);
        void        onAuthenticationComplete(ble_gap_conn_desc);
    };
};

extern SpinBLEClient spinBLEClient;

class SensorData {
public:
    SensorData(String id, BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) : id(id), characteristic(characteristic), data(data), length(length) {};

    String getId();
    virtual bool hasHeartRate() = 0;
    virtual bool hasCadence() = 0;
    virtual bool hasPower() = 0;
    virtual int getHeartRate() = 0;
    virtual float getCadence() = 0;
    virtual int getPower() = 0;

protected:
    String id;
    BLERemoteCharacteristic *characteristic;
    uint8_t *data;
    size_t length;
};

class SensorDataFactory {
public:
    static std::unique_ptr<SensorData> getSensorData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length);

private:
    SensorDataFactory() {};
};

class NullData : public SensorData {
public:
    NullData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) : SensorData("Null", characteristic, data, length) {};

    virtual bool  hasHeartRate();
    virtual bool  hasCadence();
    virtual bool  hasPower();
    virtual int   getHeartRate();
    virtual float getCadence();
    virtual int   getPower();
};

class HeartRateData : public SensorData {
public:
    HeartRateData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) : SensorData("HRM", characteristic, data, length) {};

    virtual bool  hasHeartRate();
    virtual bool  hasCadence();
    virtual bool  hasPower();
    virtual int   getHeartRate();
    virtual float getCadence();
    virtual int   getPower();
};

class FlywheelData : public SensorData {
public:
    FlywheelData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) : SensorData("FLYW", characteristic, data, length) {};

    virtual bool  hasHeartRate();
    virtual bool  hasCadence();
    virtual bool  hasPower();
    virtual int   getHeartRate();
    virtual float getCadence();
    virtual int   getPower();
};

class FitnessMachineIndoorBikeData : public SensorData {
public:
    FitnessMachineIndoorBikeData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length);
    ~FitnessMachineIndoorBikeData();

    enum Types : uint8_t {
        InstantaneousSpeed   = 0,
        AverageSpeed         = 1,
        InstantaneousCadence = 2,
        AverageCadence       = 3,
        TotalDistance        = 4,
        ResistanceLevel      = 5,
        InstantaneousPower   = 6,
        AveragePower         = 7,
        TotalEnergy          = 8,
        EnergyPerHour        = 9,
        EnergyPerMinute      = 10,
        HeartRate            = 11,
        MetabolicEquivalent  = 12,
        ElapsedTime          = 13,
        RemainingTime        = 14
    };

    static constexpr uint8_t FieldCount = Types::RemainingTime + 1;

    virtual bool  hasHeartRate();
    virtual bool  hasCadence();
    virtual bool  hasPower();
    virtual int   getHeartRate();
    virtual float getCadence();
    virtual int   getPower();

private:
    int flags;
    double_t *values;

    static uint8_t const flagBitIndices[];
    static uint8_t const flagEnabledValues[];
    static size_t const byteSizes[];
    static uint8_t const signedFlags[];
    static double_t const resolutions[];
    static int convert(int value, size_t length, uint8_t isSigned);
};