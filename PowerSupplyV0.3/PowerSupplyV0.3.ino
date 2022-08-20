//Synth Power Supply by Tucker Macor
//Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
//https://creativecommons.org/licenses/by-nc-sa/4.0/
//
//
//

#include <EEPROM.h>                       //include eeprom to save settings
#include <Wire.h>                         //oled stuff
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(128, 64, &Wire);
#include <OneWire.h>                       //temperature sensor stuff
#include <DallasTemperature.h>
#define ONE_WIRE_BUS A0
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
#include <Encoder.h>                        //encoder stuff
Encoder myEnc(2, 3);
long oldPosition  = -999;

#define referanceVoltage 5                  //max voltage of the ADC
const int voltagePins[4] = {A1, A2, A6, A3};//where the voltage dividers are connected
#define buttonPin 4                         //where the button is connected
#define offButtonPin A7                     //where the power off button is connected
unsigned long lastDebounceTime = 0;         //last time the button was pressed
#define debounceWait 300                    //how long to wait until repeating the button action
byte oledPage = 0;                          //which page should be displayed
float adjustmentAmount[4] = {6.045, 6.045, 7.045, 6.045};//how much to multiply the read voltages by
float dispVoltages[4];                      //the calculated voltage that we display and use for fail safe
float tempC;                                //the temperature in *C
float operationHours;                       //how long the PSU has run
bool okay[5];                               //is everything Okay? which things are okay?
bool activeFault = true;                    //is somthing currently going wrong?
#define inputRelay 13                       //where is the relay that connects the incoming power
#define ledBlinkDelayTime 800               //how fast blinking leds blink
#define numberOfLeds 8                      //how many leds are there
const int ledPins[numberOfLeds] =   {11, 12, 9, 10, 7, 8, 5, 6}; //where are the leds connected
bool blinkLed[numberOfLeds] =       {LOW, LOW, LOW, LOW, LOW, LOW, LOW, LOW}; //should the leds be blinking
bool lastblinkState = LOW;                                                    //state of the blinking leds
unsigned long lastBlink = 0;                                                  //last time an led blinked
bool InrushIgnore = false;                                                    //should the supply ignore the voltage levels for 500ms after starting.
byte safeShutdown = 6;

void setup() {                              //setup runs once when the arduino is first powerd on
  pinMode(inputRelay, OUTPUT);
  digitalWrite(inputRelay, HIGH);           //Activate the relay to keep the arduino on
  for (byte i = 0; i < numberOfLeds; i++) { //set led pins as outputs
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);          //make sure all led and relay pins are off
  }
  setLed(1, 0, 1);                          //turn on all red leds
  setLed(2, 0, 1);
  setLed(3, 0, 1);
  setLed(4, 0, 1);
  loadDataFromEeprom();  //get the saved settings out of eeprom
  pinMode(buttonPin, INPUT_PULLUP);         //set the button pin to input 
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);//initialize the display
  display.setTextSize(1);                   //small text size
  display.setTextColor(SSD1306_WHITE);      //white text
  display.setCursor(0, 0);
  display.println(F("Power ON"));
  display.display();
  setLed(4, 0, 0);
  getTempFromSensor();                      //update the tempC variable
  delay(300);                               //wait for voltages to stabilize
  getVoltageValues();                       // read the voltages
  setLed(3, 0, 0);
  activeFault = false;
  checkForFaults();                         //check for problems
  setLed(2, 0, 0);
  EEPROM.get(28, safeShutdown);             //check if the epprom got properly saved last shutdown
  if (safeShutdown == 6) {
    display.setCursor(0, 9);
    display.print(F("Improper Shutdown!"));
    display.display();
    for (byte i = 0; i < 8; i++) {
      display.invertDisplay(true);
      delay(500);
      display.invertDisplay(false);
      delay(500);
    }
  }
  safeShutdown = 6;
  EEPROM.put(28, safeShutdown);
  if (activeFault == false) {              //if everything is okay
    setLed(2, 1, 1);                        //turn on the outputs and the green leds
    setLed(3, 1, 1);
    setLed(4, 1, 1);
    if (InrushIgnore == true) {
      display.setCursor(0, 18);
      display.println(F("Wait For InRush"));
      display.display();
      delay(600);                           //wait for inrush to settle down to prevent the supply imediatly turning off
    }
  }
  setLed(1, 0, 0);
}

void loop() {                               //main loop repeats forever
  getVoltageValues();                       //read voltages
  getTempFromSensor();                      //get temperature
  checkForFaults();                         //check for problems
  // Blink Leds
  if (millis() - lastBlink > ledBlinkDelayTime) {
    lastblinkState = !lastblinkState;
    for (byte i = 0; i < numberOfLeds; i++) {
      if (blinkLed[i] == HIGH) {
        digitalWrite(ledPins[i], lastblinkState);
      }
    }
    lastBlink = millis();
  }
  //check if we need to shutdown
  if (analogRead(offButtonPin) > 900) {
    saveAndShutdown();
  }
  changeOledPage();  //check if the button on the encoder got pressed and if it did then change the page the oled displays
  //display stuff
  if (oledPage == 0) {
    mainDisplay();
  }
  else if (oledPage == 1) {
    AdjustDisplay(0);
    readEncoder(0);
  }
  else if (oledPage == 2) {
    AdjustDisplay(1);
    readEncoder(1);
  }
  else if (oledPage == 3) {
    AdjustDisplay(2);
    readEncoder(2);
  }
  else if (oledPage == 4) {
    AdjustDisplay(3);
    readEncoder(3);
  }
  else if (oledPage == 5) {
    setInRushDisplay();
    readEncoder(4);
  }
}

void saveAndShutdown() {               //turn off the power supply
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Shutting Down"));
  display.display();
  setLed(2, 1, 0);                    //dissable the 12v output relays
  setLed(3, 1, 0);
  display.setCursor(0, 9);
  display.println(F("+/- 12V off"));
  display.display();
  saveDataToEeprom();                 //save settings
  display.setCursor(0, 18);
  display.println(F("eeprom saved!"));
  display.display();
  delay(2000);                 //wait with only 5V on to let other modules save eeprom
  setLed(4, 1, 0);
  display.setCursor(0, 27);
  display.println(F("5V Off"));
  display.display();  
  delay(350);
  digitalWrite(inputRelay, LOW);      //THE END - turn off the relay that is powering the arduino
  delay(9999);
}

void getVoltageValues() {             //read voltages from the analog pins
  for (byte i = 0; i < 4; i++) {
    dispVoltages[i] = (analogRead(voltagePins[i]) * referanceVoltage) / 1024.0;
    dispVoltages[i] = dispVoltages[i] * adjustmentAmount[i];
  }
}

void getTempFromSensor() {            //update the tempC variable
  sensors.requestTemperatures();
  tempC = sensors.getTempCByIndex(0);
}

void checkForFaults() {                               //check for problems
  if (dispVoltages[0] > 14 && dispVoltages[0] < 30) { //check for correct input range
    okay[0] = true;
    setLed(1, 1, 1);
  }
  else {
    okay[0] = false;
    activeFault = true;
    setLed(1, 0, 2);
    setLed(1, 1, 0);
  }
  if (dispVoltages[1] > 11.5 && dispVoltages[1] < 13) { //check for correct 12v range
    okay[1] = true;
  }
  else {
    okay[1] = false;
    activeFault = true;
    setLed(2, 0, 2);
  }
  if (dispVoltages[2] > 11.5 && dispVoltages[2] < 13) { //check for correct -12v range
    okay[2] = true;
  }
  else {
    okay[2] = false;
    activeFault = true;
    setLed(3, 0, 2);
  }
  if (dispVoltages[3] > 4.5 && dispVoltages[3] < 5.5) { //check for correct 5v range
    okay[3] = true;
  }
  else {
    okay[3] = false;
    activeFault = true;
    setLed(4, 0, 2);
  }
  if (tempC < 45) {                        //check for correct Temperature range
    okay[4] = true;
  }
  else {
    okay[4] = false;
    activeFault = true;
  }
  if (activeFault == true) {
    setLed(2, 1, 0);
    setLed(3, 1, 0);
    setLed(4, 1, 0);
  }
}

void changeOledPage() {//read the button
  if (digitalRead(buttonPin) == LOW && millis() - lastDebounceTime > debounceWait) {
    oledPage++;
    if (oledPage > 5) {
      oledPage = 0;
    }
    lastDebounceTime = millis();
  }
}

void setInRushDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("Ignore InRush?"));
  display.setCursor(0, 9);
  if (InrushIgnore == true) {
    display.print(F("True"));
  }
  else {
    display.print(F("False"));
  }
  display.display();
}

void readEncoder(byte whichValueToAdjust) {//read the encoder
  long newPosition = myEnc.read();
  if (newPosition != oldPosition) {
    if (whichValueToAdjust < 4) {
      if (newPosition > oldPosition) {
        adjustmentAmount[whichValueToAdjust] = adjustmentAmount[whichValueToAdjust] + 0.01;
      }
      if (newPosition < oldPosition) {
        adjustmentAmount[whichValueToAdjust] = adjustmentAmount[whichValueToAdjust] - 0.01;
      }
    }
    if (whichValueToAdjust == 4 && millis() - lastDebounceTime > debounceWait) {
      InrushIgnore = !InrushIgnore;
      lastDebounceTime = millis();
    }
    oldPosition = newPosition;
  }
}

void AdjustDisplay(byte whichValueToAdjust) {//draw the display where we adjust voltage multiplyers
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("Adjusting Value: "));
  display.print(whichValueToAdjust);
  display.setCursor(0, 9);
  if (whichValueToAdjust == 0) {
    display.print(F("(18-30V INPUT)"));
  }
  if (whichValueToAdjust == 1) {
    display.print(F("(12V OUTPUT)"));
  }
  if (whichValueToAdjust == 2) {
    display.print(F("(-12V OUTPUT)"));
  }
  if (whichValueToAdjust == 3) {
    display.print(F("(5V OUTPUT)"));
  }
  display.setCursor(0, 18);
  display.print(adjustmentAmount[whichValueToAdjust]);
  display.print(F("X"));
  display.setCursor(0, 27);
  display.print(dispVoltages[whichValueToAdjust]);
  display.display();
}

void mainDisplay() {          //draw the main display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("Power Supply"));
  display.setCursor(0, 9);
  display.print(F("Input: +"));
  display.print(dispVoltages[0]);
  display.print(F(" V"));
  if (okay[0] == true) {
    display.print(F(" OKAY!"));
  }
  else {
    display.print(F(" Fault"));
  }
  display.setCursor(0, 18);
  display.print(F("12v  : +"));
  display.print(dispVoltages[1]);
  display.print(F(" V"));
  if (okay[1] == true) {
    display.print(F(" OKAY!"));
  }
  else {
    display.print(F(" Fault"));
  }
  display.setCursor(0, 27);
  display.print(F("-12V : -"));
  display.print(dispVoltages[2]);
  display.print(F(" V"));
  if (okay[2] == true) {
    display.print(F(" OKAY!"));
  }
  else {
    display.print(F(" Fault"));
  }
  display.setCursor(0, 36);
  display.print(F("5V   : +"));
  if (dispVoltages[3] < 10) {
    display.print(0);
  }
  display.print(dispVoltages[3]);
  display.print(F(" V"));
  if (okay[3] == true) {
    display.print(F(" OKAY!"));
  }
  else {
    display.print(F(" Fault"));
  }
  display.setCursor(0, 45);
  display.print(F("Temp : "));
  if (tempC > 0) {
    display.print(F("+"));
  }
  display.print(tempC);
  display.print(F(" C"));
  if (okay[4] == true) {
    display.print(F(" OKAY!"));
  }
  else {
    display.print(F(" Fault"));
  }
  display.setCursor(0, 54);
  display.print(F("Hours:  "));
  if (operationHours < 1000) {
    display.print(0);
  }
  if (operationHours < 100) {
    display.print(0);
  }
  if (operationHours < 10) {
    display.print(0);
  }
  display.print((operationHours + millis() / (float)3600000), 8);
  display.display();
}

void loadDataFromEeprom() {              //load settings from eeprom
  EEPROM.get(0, adjustmentAmount[0]);
  EEPROM.get(4, adjustmentAmount[1]);
  EEPROM.get(8, adjustmentAmount[2]);
  EEPROM.get(12, adjustmentAmount[3]);
  EEPROM.get(16, operationHours);
  EEPROM.get(24, InrushIgnore);
  byte validEeprom = 0;
  EEPROM.get(20, validEeprom);
  if (validEeprom != 7) {
    adjustmentAmount[0] = 6.045;
    adjustmentAmount[1] = 6.045;
    adjustmentAmount[2] = 7.045;
    adjustmentAmount[3] = 6.045;
    operationHours = 0;
    InrushIgnore = false;
    safeShutdown = 7;
  }
  
}

void saveDataToEeprom() {                //save settings to eeprom
  EEPROM.put(0, adjustmentAmount[0]);
  EEPROM.put(4, adjustmentAmount[1]);
  EEPROM.put(8, adjustmentAmount[2]);
  EEPROM.put(12, adjustmentAmount[3]);
  float tempOperationHours = operationHours + millis() / (float)3600000;
  EEPROM.put(16, tempOperationHours);
  delay(5);
  byte validEeprom = 7;
  EEPROM.put(20, validEeprom);
  EEPROM.put(24, InrushIgnore);
  byte safeShutdown = 7;
  EEPROM.put(28, safeShutdown);
}

// whichLedToSet 1-4 , whichColor 0-1 (red-green) , whichState 0 == off 1 == on 2 == blinking
void setLed(int whichLedToSet, int whichColor, int whichState) {
  int whichLedPin = ((whichLedToSet * 2) + whichColor) - 2;
  if (whichState == 0) {
    digitalWrite(ledPins[whichLedPin], LOW);
    blinkLed[whichLedPin] = LOW;
  }
  else if (whichState == 1) {
    digitalWrite(ledPins[whichLedPin], HIGH);
    blinkLed[whichLedPin] = LOW;
  }
  else if (whichState == 2) {
    blinkLed[whichLedPin] = HIGH;
  }
}
