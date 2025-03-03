// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include "BLE_Common.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>

TaskHandle_t BLENotifyTask;

//BLE Server Settings
bool _BLEClientConnected = false;
bool updateConnParametersFlag = false;
bool GlobalBLEClientConnected = false; //needs to be moved to BLE_Server

NimBLEServer *pServer = nullptr;
int bleConnDesc = 1;

BLECharacteristic *heartRateMeasurementCharacteristic;
BLECharacteristic *cyclingPowerMeasurementCharacteristic;
BLECharacteristic *fitnessMachineFeature;
BLECharacteristic *fitnessMachineIndoorBikeData;

/********************************Bit field Flag Example***********************************/
// 00000000000000000001 - 1   - 0x001 - Pedal Power Balance Present
// 00000000000000000010 - 2   - 0x002 - Pedal Power Balance Reference
// 00000000000000000100 - 4   - 0x004 - Accumulated Torque Present
// 00000000000000001000 - 8   - 0x008 - Accumulated Torque Source
// 00000000000000010000 - 16  - 0x010 - Wheel Revolution Data Present
// 00000000000000100000 - 32  - 0x020 - Crank Revolution Data Present
// 00000000000001000000 - 64  - 0x040 - Extreme Force Magnitudes Present
// 00000000000010000000 - 128 - 0x080 - Extreme Torque Magnitudes Present
// 00000000000100000000 - Extreme Angles Present (bit8)
// 00000000001000000000 - Top Dead Spot Angle Present (bit 9)
// 00000000010000000000 - Bottom Dead Spot Angle Present (bit 10)
// 00000000100000000000 - Accumulated Energy Present (bit 11)
// 00000001000000000000 - Offset Compensation Indicator (bit 12)
// 98765432109876543210 - bit placement helper :)
// 00001110000000001100
// 00000101000010000110
// 00000000100001010100
//               100000
byte heartRateMeasurement[5] = {0b00000, 60, 0, 0, 0};
byte cyclingPowerMeasurement[9] = {0b0000000100011, 0, 200, 0, 0, 0, 0, 0, 0};
byte cpsLocation[1] = {0b000};    //sensor location 5 == left crank
byte cpFeature[1] = {0b00100000}; //crank information present                                         // 3rd & 2nd byte is reported power

byte ftmsService[6] = {0x00, 0x00, 0x00, 0b01, 0b0100000, 0x00};
byte ftmsControlPoint[8] = {0, 0, 0, 0, 0, 0, 0, 0}; //0x08 we need to return a value of 1 for any sucessful change
byte ftmsMachineStatus[8] = {0, 0, 0, 0, 0, 0, 0, 0};

uint8_t ftmsFeature[8] = {0x86, 0x50, 0x00, 0x00, 0x0C, 0xE0, 0x00, 0x00};                            //101000010000110 1110000000001100
uint8_t ftmsIndoorBikeData[14] = {0x54, 0x0A, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}; //00000000100001010100 ISpeed, ICAD, TDistance, IPower, ETime
uint8_t ftmsResistanceLevelRange[6] = {0x00, 0x00, 0x3A, 0x98, 0xC5, 0x68};                           //+-15000 not sure what units
uint8_t ftmsPowerRange[6] = {0x00, 0x00, 0xA0, 0x0F, 0x01, 0x00};                                     //1-4000 watts

void startBLEServer()
{

  //Server Setup
  debugDirector("Starting BLE Server");
  pServer = BLEDevice::createServer();

  //HEART RATE MONITOR SERVICE SETUP
  BLEService *pHeartService = pServer->createService(HEARTSERVICE_UUID);
  heartRateMeasurementCharacteristic = pHeartService->createCharacteristic(
      HEARTCHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  //Power Meter MONITOR SERVICE SETUP
  BLEService *pPowerMonitor = pServer->createService(CYCLINGPOWERSERVICE_UUID);

  cyclingPowerMeasurementCharacteristic = pPowerMonitor->createCharacteristic(
      CYCLINGPOWERMEASUREMENT_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  BLECharacteristic *cyclingPowerFeatureCharacteristic = pPowerMonitor->createCharacteristic(
      CYCLINGPOWERFEATURE_UUID,
      NIMBLE_PROPERTY::READ);

  BLECharacteristic *sensorLocationCharacteristic = pPowerMonitor->createCharacteristic(
      SENSORLOCATION_UUID,
      NIMBLE_PROPERTY::READ);

  //Fitness Machine service setup
  BLEService *pFitnessMachineService = pServer->createService(FITNESSMACHINESERVICE_UUID);

  fitnessMachineFeature = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEFEATURE_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineControlPoint = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINECONTROLPOINT_UUID,
      NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineStatus = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINESTATUS_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY);

  fitnessMachineIndoorBikeData = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEINDOORBIKEDATA_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  BLECharacteristic *fitnessMachineResistanceLevelRange = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINERESISTANCELEVELRANGE_UUID,
      NIMBLE_PROPERTY::READ);

  BLECharacteristic *fitnessMachinePowerRange = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEPOWERRANGE_UUID,
      NIMBLE_PROPERTY::READ);

  pServer->setCallbacks(new MyServerCallbacks());

  //Creating Characteristics
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);

  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);
  cyclingPowerFeatureCharacteristic->setValue(cpFeature, 1);
  sensorLocationCharacteristic->setValue(cpsLocation, 1);

  fitnessMachineFeature->setValue(ftmsFeature, 8);
  fitnessMachineControlPoint->setValue(ftmsControlPoint, 8);

  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 14);

  fitnessMachineStatus->setValue(ftmsMachineStatus, 8);
  fitnessMachineResistanceLevelRange->setValue(ftmsResistanceLevelRange, 6);
  fitnessMachinePowerRange->setValue(ftmsPowerRange, 6);

  fitnessMachineControlPoint->setCallbacks(new MyCallbacks());

  pHeartService->start();          //heart rate service
  pPowerMonitor->start();          //Power Meter Service
  pFitnessMachineService->start(); //Fitness Machine Service

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  pAdvertising->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  pAdvertising->addServiceUUID(HEARTSERVICE_UUID);
  pAdvertising->setMaxInterval(250);
  pAdvertising->setMinInterval(160);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  debugDirector("Bluetooth Characteristic defined!");
  xTaskCreatePinnedToCore(
      BLENotify,       /* Task function. */
      "BLENotifyTask", /* name of task. */
      3000,            /* Stack size of task*/
      NULL,            /* parameter of the task */
      1,               /* priority of the task*/
      &BLENotifyTask,  /* Task handle to keep track of created task */
      1);              /* pin task to core 0 */

  debugDirector("BLE Notify Task Started");
}

void BLENotify(void *pvParameters)
{
  for (;;)
  {
    if (spinBLEClient.connectedHR && !spinBLEClient.connectedPM && (userConfig.getSimulatedHr() > 0) && userPWC.hr2Pwr)
    {
      calculateInstPwrFromHR();
    }
    if (!spinBLEClient.connectedPM && !userPWC.hr2Pwr)
    {
      userConfig.setSimulatedCad(0);
      userConfig.setSimulatedWatts(0);
    }
    if (!spinBLEClient.connectedHR)
    {
      userConfig.setSimulatedHr(0);
    }

    if (_BLEClientConnected)
    {
      //update the BLE information on the server
      heartRateMeasurement[1] = userConfig.getSimulatedHr();
      heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);
      computeCSC();
      updateIndoorBikeDataChar();
      updateCyclingPowerMesurementChar();
      cyclingPowerMeasurementCharacteristic->notify();
      fitnessMachineFeature->notify();
      fitnessMachineIndoorBikeData->notify();
      heartRateMeasurementCharacteristic->notify();
      GlobalBLEClientConnected = true;
      
      if (updateConnParametersFlag)
      {
        vTaskDelay(100/portTICK_PERIOD_MS);
        pServer->updateConnParams(bleConnDesc, 40, 50, 0, 100);
        updateConnParametersFlag = false;
      }
    }
    else
    {
      GlobalBLEClientConnected = false;
    }
    if (!_BLEClientConnected)
    {
      digitalWrite(LED_PIN, LOW); //blink if no client connected
    }
    vTaskDelay((BLE_NOTIFY_DELAY / 2) / portTICK_PERIOD_MS);
    digitalWrite(LED_PIN, HIGH);
    vTaskDelay((BLE_NOTIFY_DELAY / 2) / portTICK_PERIOD_MS);
    //debugDirector("BLEServer High Water Mark: " + String(uxTaskGetStackHighWaterMark(BLENotifyTask)));
  }
}

void computeERG(int currentWatts, int setPoint)
{
  //cooldownTimer--;

  float incline = userConfig.getIncline();
  int cad = userConfig.getSimulatedCad();
  int newIncline = incline;
  int amountToChangeIncline = 0;

  if (cad > 20)
  {
    if (abs(currentWatts - setPoint) < 50)
    {
      amountToChangeIncline = (currentWatts - setPoint) * .5;
    }
    if (abs(currentWatts - setPoint) > 50)
    {
      amountToChangeIncline = amountToChangeIncline + ((currentWatts - setPoint)) * 1;
    }
    amountToChangeIncline = amountToChangeIncline / ((currentWatts / 100) + 1);
  }

  if (abs(amountToChangeIncline) > userConfig.getShiftStep() * 3)
  {
    if (amountToChangeIncline > 0)
    {
      amountToChangeIncline = userConfig.getShiftStep() * 3;
    }
    if (amountToChangeIncline < 0)
    {
      amountToChangeIncline = -(userConfig.getShiftStep() * 3);
    }
  }

  newIncline = incline - amountToChangeIncline; //  }
  userConfig.setIncline(newIncline);
}

void computeCSC() //What was SIG smoking when they came up with the Cycling Speed and Cadence Characteristic?
{
  if (userConfig.getSimulatedCad() > 0)
  {
    float crankRevPeriod = (60 * 1024) / userConfig.getSimulatedCad();
    spinBLEClient.cscCumulativeCrankRev++;
    spinBLEClient.cscLastCrankEvtTime += crankRevPeriod;
    int remainder, quotient;
    quotient = spinBLEClient.cscCumulativeCrankRev / 256;
    remainder = spinBLEClient.cscCumulativeCrankRev % 256;
    cyclingPowerMeasurement[5] = remainder;
    cyclingPowerMeasurement[6] = quotient;
    quotient = spinBLEClient.cscLastCrankEvtTime / 256;
    remainder = spinBLEClient.cscLastCrankEvtTime % 256;
    cyclingPowerMeasurement[7] = remainder;
    cyclingPowerMeasurement[8] = quotient;
  } //^^Using the old way of setting bytes because I like it and it makes more sense to me looking at it.
}

void updateIndoorBikeDataChar()
{
  int cad = userConfig.getSimulatedCad();
  int watts = userConfig.getSimulatedWatts();
  int hr = userConfig.getSimulatedHr();
  float gearRatio = 1;
  int speed = ((cad * 2.75 * 2.08 * 60 * gearRatio) / 10);
  ftmsIndoorBikeData[2] = (uint8_t)(speed & 0xff);
  ftmsIndoorBikeData[3] = (uint8_t)(speed >> 8);
  ftmsIndoorBikeData[4] = (uint8_t)((cad * 2) & 0xff);
  ftmsIndoorBikeData[5] = (uint8_t)((cad * 2) >> 8); // cadence value
  ftmsIndoorBikeData[6] = 0;                         //distance <
  ftmsIndoorBikeData[7] = 0;                         //distance <-- uint24 with 1m resolution
  ftmsIndoorBikeData[8] = 0;                         //distance <
  ftmsIndoorBikeData[9] = (uint8_t)((watts)&0xff);
  ftmsIndoorBikeData[10] = (uint8_t)((watts) >> 8); // power value, constrained to avoid negative values, although the specification allows for a sint16
  ftmsIndoorBikeData[11] = (uint8_t) hr;
  ftmsIndoorBikeData[12] = 0;                       // Elapsed Time uint16 in seconds
  ftmsIndoorBikeData[13] = 0;                       // Elapsed Time
  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 14);
} //^^Using the New Way of setting Bytes.

void updateCyclingPowerMesurementChar()
{
  int remainder, quotient;
  quotient = userConfig.getSimulatedWatts() / 256;
  remainder = userConfig.getSimulatedWatts() % 256;
  cyclingPowerMeasurement[2] = remainder;
  cyclingPowerMeasurement[3] = quotient;
  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);
  debugDirector("");
  for (const auto &text : cyclingPowerMeasurement)
  { // Range-for!
    debugDirector(String(text, HEX) + " ", false);
  }

  debugDirector("<-- CPMC sent ", false);
  debugDirector("");
}

//Creating Server Connection Callbacks

void MyServerCallbacks::onConnect(BLEServer *pServer, ble_gap_conn_desc *desc)
{
  _BLEClientConnected = true;
  debugDirector("Bluetooth Client Connected! " + String(desc->conn_handle));
  updateConnParametersFlag = true;
  bleConnDesc = desc->conn_handle;
};

void MyServerCallbacks::onDisconnect(BLEServer *pServer)
{
  _BLEClientConnected = false;
  debugDirector("Bluetooth Client Disconnected!");
}

void MyCallbacks::onWrite(BLECharacteristic *pCharacteristic)
{
  std::string rxValue = pCharacteristic->getValue();

  if (rxValue.length() > 1)
  {
    for (const auto &text : rxValue)
    { // Range-for!
      debugDirector(String(text, HEX) + " ", false);
    }
    debugDirector("<-- From APP ", false);
    /* 17 means FTMS Incline Control Mode  (aka SIM mode)*/

    if ((int)rxValue[0] == 17)
    {
      signed char buf[2];
      buf[0] = rxValue[3]; // (Least significant byte)
      buf[1] = rxValue[4]; // (Most significant byte)

      int port = bytes_to_u16(buf[1], buf[0]);
      userConfig.setIncline(port);
      if (userConfig.getERGMode())
      {
        userConfig.setERGMode(false);
      }
      debugDirector(" Target Incline: " + String((userConfig.getIncline() / 100)), false);
    }
    debugDirector("");

    /* 5 means FTMS Watts Control Mode (aka ERG mode) */
    if (((int)rxValue[0] == 5) && (spinBLEClient.connectedPM))
    {
      int targetWatts = bytes_to_int(rxValue[2], rxValue[1]);
      if (!userConfig.getERGMode())
      {
        userConfig.setERGMode(true);
      }
      computeERG(userConfig.getSimulatedWatts(), targetWatts);
      debugDirector("ERG MODE", false);
      debugDirector(" Target: " + String(targetWatts), false);
      debugDirector(" Current: " + String(userConfig.getSimulatedWatts()), false); //not displaying numbers less than 256 correctly but they do get sent to Zwift correctly.
      debugDirector(" Incline: " + String(userConfig.getIncline() / 100), false);
      debugDirector("");
    }
  }
}

void calculateInstPwrFromHR()
{

  //userConfig.setSimulatedWatts((s1Pwr*s2HR)-(s2Pwr*S1HR))/(S2HR-s1HR)+(userConfig.getSimulatedHr(*((s1Pwr-s2Pwr)/(s1HR-s2HR)));
  int avgP = ((userPWC.session1Pwr * userPWC.session2HR) - (userPWC.session2Pwr * userPWC.session1HR)) / (userPWC.session2HR - userPWC.session1HR) + (userConfig.getSimulatedHr() * ((userPWC.session1Pwr - userPWC.session2Pwr) / (userPWC.session1HR - userPWC.session2HR)));

  if (avgP < 50)
  {
    avgP = 50;
  }

  if (userConfig.getSimulatedHr() < 90)
  {
    //magic math here for inst power
  }

  if (userConfig.getSimulatedHr() > 170)
  {
    //magic math here for inst power
  }

  userConfig.setSimulatedWatts(avgP);
  userConfig.setSimulatedCad(90);

  debugDirector("Power From HR: " + String(avgP));
}