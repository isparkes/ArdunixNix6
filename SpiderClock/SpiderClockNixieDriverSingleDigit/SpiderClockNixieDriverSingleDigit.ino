//**********************************************************************************
//**********************************************************************************
//* Single digit driver, for open frame "spiderclock", not because of Spiderman,   *
//* because it looks like a spider web.                                            *
//*                                                                                *
//* Can use the internal RC oscillator, to save on component count.                *
//* Regulated voltage generation                                                   *
//* Two button design, one for setting hours, one for minutes                      *
//*                                                                                *
//*                                                                                *
//**********************************************************************************
//**********************************************************************************
#include <avr/io.h> 
#include <avr/interrupt.h> 

#include <EEPROM.h>
#include <DS3231.h>
#include <Wire.h>

// How hard we drive the HV Generator, too low burns the MOSFET, too high does not give the coil time to load 
const int HVGEN_DEFAULT=200;

// The divider value determines the frequency of the DC-DC converter
// The output voltage depends on the input voltage and the frequency:
// For 5V input:
// at a divider of 200, the frequency is 16MHz / 200 / 2 (final /2 because of toggle mode)
// Ranges for this are around 800 - 100, with 400 (20KHz) giving about 180V
// Drive up to 100 (80KHz) giving around 200V
int divider = HVGEN_DEFAULT;

const int SENSOR_LOW_VALUE = 100;                // Dark threshold value
const int SENSOR_HIGH_VALUE = 700;               // Bright threshold value
const int SENSOR_SMOOTH_READINGS_DEFAULT = 3;    // Speed at which the brighness adapts to changes
const double SENSOR_RANGE = (double)(SENSOR_HIGH_VALUE - SENSOR_LOW_VALUE);

// Define the digital value we expect when the button is *pressed* 
const int BUTTON_ACTIVE = 0;                     // 1 = with pull down, 0 = with pull up

//**********************************************************************************
//**********************************************************************************
//*                               Variables                                        *
//**********************************************************************************
//**********************************************************************************
// Controls the digit drive
const int anode1 = 10;

// pin used to drive the DC-DC converter
const int hvDriverPin = 9;

// Internal (inside the box) tick led 
const int tickLed = 5; // PWM capable

// SN74141
const int ledPin_0_a = 2;
const int ledPin_0_b = 4;
const int ledPin_0_c = 7;
const int ledPin_0_d = 8;

// HV sense analogue input
const int sensorPin = A0;

// LDR sense analogue input
const int LDRPin = A3;
// const int LDRPin = A1;

// button input
//const int inputPin1 = 5; // mins
//const int inputPin2 = 6; // hours
const int inputPin1 = A1; // mins
const int inputPin2 = A2; // hours

//**********************************************************************************

// Defines
const long MINS_MAX = 60;
const long HOURS_MAX = 24;

boolean blanked;
int tccrOff;
int tccrOn;

// The times that we use for display and blanking
const int displayDelay = 1400;
const int interDigitDelay = 400;
const int interReadingDelay = 2000;

const int digit1Start = 0;
const int digit1End = digit1Start + displayDelay;
const int digit2Start = digit1End + interDigitDelay;
const int digit2End = digit2Start + displayDelay;
const int digit3Start = digit2End + interDigitDelay;
const int digit3End = digit3Start + displayDelay;
const int digit4Start = digit3End + interDigitDelay;
const int digit4End = digit4Start + displayDelay;
const int displayEnd = digit4End + interReadingDelay;

// used for dimming
int loopCounter = 0;
double sensorSmoothed = 0;
int dimStep = 0;      // Dimming PWM counter, cycles from 1 to 10
int dimFactor = 0;    // Current dimming factor, update once per loop

// Used for special mappings of the 74141 -> digit (wiring aid)
const int decodeDigit[16] = {0,1,2,3,4,5,6,7,8,9,10,10,10,10,10,10};
const int PWMDimFunction[10] = {0,2,10,20,30,40,50,80,120,255};

// Time initial values, overwritten on startup if an RTC is there
byte hours = 12;
byte mins = 0;
byte secs = 0;
byte days = 1;
byte months = 1;
byte years = 14;
long lastCheckTimeMs = 0;

// ********************** Input switch management **********************
int  button1PressedCount = 0;
long button1PressStartMillis = 0;
int  debounceCounter1 = 5; // Number of successive reads before we say the switch is down
boolean button1WasReleased = false;
boolean button1Press2S = false;
boolean button1Press1S = false;
boolean button1Press = false;
boolean button1PressRelease2S = false;
boolean button1PressRelease1S = false;
boolean button1PressRelease = false;

int  button2PressedCount = 0;
long button2PressStartMillis = 0;
int  debounceCounter2 = 5; // Number of successive reads before we say the switch is down
boolean button2WasReleased = false;
boolean button2Press2S = false;
boolean button2Press1S = false;
boolean button2Press = false;
boolean button2PressRelease2S = false;
boolean button2PressRelease1S = false;
boolean button2PressRelease = false;

// RTC, uses Analogue pins A4 (SDA) and A5 (SCL)
DS3231 Clock;

//**********************************************************************************
//**********************************************************************************
//*                                    Setup                                       *
//**********************************************************************************
//**********************************************************************************
void setup() 
{
  // **************************** Set up pins ****************************
  pinMode(ledPin_0_a, OUTPUT);      
  pinMode(ledPin_0_b, OUTPUT);      
  pinMode(ledPin_0_c, OUTPUT);      
  pinMode(ledPin_0_d, OUTPUT);    

  // Set the driver pin to putput
  pinMode(hvDriverPin, OUTPUT);

  // Set up the anode pins
  pinMode(anode1, OUTPUT);

  pinMode(inputPin1, INPUT ); // set the input pin 1
  pinMode(inputPin2, INPUT ); // set the input pin 1
  digitalWrite(inputPin1, HIGH );
  digitalWrite(inputPin2, HIGH );
  
  pinMode(tickLed, OUTPUT);     
 
  /* disable global interrupts while we set up them up */
  cli();
  
  // **************************** HV generator ****************************
  
  // Enable timer 1 Compare Output channel A in reset mode: TCCR1A.COM1A1 = 1, TCCR1A.COM1A0 = 0
  TCCR1A = bit(COM1A1);

  // Get thecontrol register 1B with the Timer on
  tccrOff = TCCR1A;

  // Enable timer 1 Compare Output channel A in toggle mode: TCCR1A.COM1A1 = 0, TCCR1A.COM1A0 = 1
  TCCR1A = bit(COM1A0);

  // Get thecontrol register 1B with the Timer on
  tccrOn = TCCR1A;

  // Configure timer 1 for CTC mode: TCCR1B.WGM13 = 0, TCCR1B.WGM12 = 1, TCCR1A.WGM11 = 0, TCCR1A.WGM10 = 0
  TCCR1B = bit(WGM12); 

  // Set up prescaler to x1: TCCR1B.CS12 = 0, TCCR1B.CS11 = 0, TCCR1B.CS10 = 1
  TCCR1B |= bit(CS10); 

  // Set the divider to the value we have chosen
  OCR1A   = divider;  

  // **************************************************************************

  // Set up the oscillator calibration to give us MORE SPEED
 OSCCAL = 240;
 
  /* enable global interrupts */
  sei();
  
  // Start the RTC communication
  Wire.begin();
}

//**********************************************************************************
//**********************************************************************************
//*                              Main loop                                         *
//**********************************************************************************
//**********************************************************************************
void loop() 
{
  // Check button, we evaluate below
  checkButton1();
  checkButton2();
  
  long timeMs = millis() - lastCheckTimeMs;
  if (timeMs % 2000 >= 1000) {
    int tickLedPWMVal = PWMDimFunction[dimFactor];
    analogWrite(tickLed, tickLedPWMVal);
  } else {
    analogWrite(tickLed, 0);
  }
  
  // Update the time buffer when we are starting a new readout
  if (loopCounter ==  0) {
    
    getRTCTime();
    
    dimFactor = getDimmingFromLDR();
    
    lastCheckTimeMs = millis();
    
    if (button1Press) {
      mins = mins + 1;
      if (mins >= MINS_MAX) {
        mins = 0;
      }
      secs = 0;
      setRTC();
    }

    if (button2Press) {
      hours = hours + 1;
      if (hours >= HOURS_MAX) {
        hours = 0;
      }
      setRTC();
    }
  }
  
  checkHVVoltage();

  if (blanked) {
    digitalWrite(anode1, 0);
  } 
  else {
    // perform dimming via 10 step PWM
    dimStep++;
    if (dimStep > 10) dimStep = 1;
    if (dimFactor >= dimStep) {
      digitalWrite(anode1, 1);
    } else {
      digitalWrite(anode1, 0);
    }
  }

  // decide which digit to display
  if (loopCounter == digit1Start) {
    blanked = false;
    SetSN74141Chip(hours / 10);
  } 

  if (loopCounter == digit1End) {
    blanked = true;
  } 
  
  if (loopCounter == digit2Start) {
    blanked = false;
    SetSN74141Chip(hours % 10);
  } 

  if (loopCounter == digit2End) {
    blanked = true;
  } 
  
  if (loopCounter == digit3Start) {
    blanked = false;
    SetSN74141Chip(mins / 10);
  } 

  if (loopCounter == digit3End) {
    blanked = true;
  } 
  
  if (loopCounter == digit4Start) {
    blanked = false;
    SetSN74141Chip(mins % 10);
  } 
  
  if (loopCounter == digit4End) {
    blanked = true;
  } 

  loopCounter = loopCounter + 1;

  // reset the loop counter
  if (loopCounter > displayEnd) {
    loopCounter = 0;
  }
}

// ************************************************************
// Decode the value to send to the 74141 and send it
// We do this via the decoder to allow easy adaptation to
// other pin layouts
// ************************************************************
void SetSN74141Chip(int num1)
{
  int a,b,c,d;
  
  // Load the a,b,c,d.. to send to the SN74141 IC
  int decodedDigit = decodeDigit[num1];
  
  switch( decodedDigit )
  {
    case 0: a=0;b=0;c=0;d=0;break;
    case 1: a=1;b=0;c=0;d=0;break;
    case 2: a=0;b=1;c=0;d=0;break;
    case 3: a=1;b=1;c=0;d=0;break;
    case 4: a=0;b=0;c=1;d=0;break;
    case 5: a=1;b=0;c=1;d=0;break;
    case 6: a=0;b=1;c=1;d=0;break;
    case 7: a=1;b=1;c=1;d=0;break;
    case 8: a=0;b=0;c=0;d=1;break;
    case 9: a=1;b=0;c=0;d=1;break;
    default: a=1;b=1;c=1;d=1;break;
  }  
  
  // Write to output pins.
  setDigit(d,c,b,a);
}

// ************************************************************
// Output the digit to the 74141
// ************************************************************
void setDigit(int segD, int segC, int segB, int segA) {
    digitalWrite(ledPin_0_a, segA);
    digitalWrite(ledPin_0_b, segB);
    digitalWrite(ledPin_0_c, segC);
    digitalWrite(ledPin_0_d, segD);  
}

// ************************************************************
// Adjust the HV gen to achieve the voltage we require
// Voltage divider is 390k in series with 4k7
// ************************************************************
double checkHVVoltage() {
  int rawSensorVal = analogRead(sensorPin);  
  double sensorVoltage = rawSensorVal * 5.0  / 1024.0;
  double externalVoltage = sensorVoltage * 394.7 / 4.7;

  if (externalVoltage > 160) {
    TCCR1A = tccrOff;
  } 
  else {
    TCCR1A = tccrOn;
  }

  return externalVoltage;
}

// ************************************************************
// See if the button was pressed and debounce. We perform a
// sort of preview here, then confirm by releasing. We track
// 3 lengths of button press: momentarily, 1S and 2S. 
// ************************************************************
void checkButton1() {
  if (digitalRead(inputPin1) == BUTTON_ACTIVE) {
    button1WasReleased = false;

    // We need consecutive pressed counts to treat this is pressed    
    if (button1PressedCount < debounceCounter1) {
      button1PressedCount += 1;
      // If we reach the debounce point, mark the start time
      if (button1PressedCount == debounceCounter1) {
        button1PressStartMillis = millis();
      }
    } 
    else {
      // We are pressed and held, maintain the press states
      if ((millis() - button1PressStartMillis) > 2000) {
        button1Press2S = true;
        button1Press1S = true;
        button1Press = true;
      } 
      else if ((millis() - button1PressStartMillis) > 1000) {
        button1Press2S = false;
        button1Press1S = true;
        button1Press = true;
      } 
      else {
        button1Press2S = false;
        button1Press1S = false;
        button1Press = true;
      }
    }
  } 
  else {
    // mark this as a press and release if we were pressed for less than a long press
    if (button1PressedCount == debounceCounter1) {
      button1WasReleased = true;

      button1PressRelease2S = false;
      button1PressRelease1S = false;
      button1PressRelease = false;

      if (button1Press2S) {
        button1PressRelease2S = true;
      } 
      else if (button1Press1S) {
        button1PressRelease1S = true;
      } 
      else if (button1Press) {
        button1PressRelease = true;
      }
    }

    // Reset the switch flags debounce counter      
    button1Press2S = false;
    button1Press1S = false;
    button1Press = false;      
    button1PressedCount = 0;
  }
}

// ************************************************************
// Check if button is pressed right now (just debounce)
// ************************************************************
boolean is1PressedNow() {
  return button1PressedCount == debounceCounter1;
}

// ************************************************************
// Check if button is pressed momentarily
// ************************************************************
boolean is1Pressed() {
  return button1Press;
}

// ************************************************************
// Check if button is pressed for a long time (> 2S)
// ************************************************************
boolean is1Pressed1S() {
  return button1Press1S;
}

// ************************************************************
// Check if button is pressed for a very long time (> 4S)
// ************************************************************
boolean is1Pressed2S() {
  return button1Press2S;
}

// ************************************************************
// Check if button is pressed for a short time (> 200mS) and released
// ************************************************************
boolean is1PressedRelease() {
  if (button1PressRelease) {
    button1PressRelease = false;
    return true;
  } 
  else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a long time (> 2) and released
// ************************************************************
boolean is1PressedRelease1S() {
  if (button1PressRelease1S) {
    button1PressRelease1S = false;
    return true;
  } 
  else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a very long time (> 2) and released
// ************************************************************
boolean is1PressedRelease2S() {
  if (button1PressRelease2S) {
    button1PressRelease2S = false;
    return true;
  } 
  else {
    return false;
  }
}

// ************************************************************
// See if the button was pressed and debounce. We perform a
// sort of preview here, then confirm by releasing. We track
// 3 lengths of button press: momentarily, 1S and 2S. 
// ************************************************************
void checkButton2() {
  if (digitalRead(inputPin2) == BUTTON_ACTIVE) {
    button2WasReleased = false;

    // We need consecutive pressed counts to treat this is pressed    
    if (button2PressedCount < debounceCounter2) {
      button2PressedCount += 1;
      // If we reach the debounce point, mark the start time
      if (button2PressedCount == debounceCounter2) {
        button2PressStartMillis = millis();
      }
    } 
    else {
      // We are pressed and held, maintain the press states
      if ((millis() - button2PressStartMillis) > 2000) {
        button2Press2S = true;
        button2Press1S = true;
        button2Press = true;
      } 
      else if ((millis() - button2PressStartMillis) > 1000) {
        button2Press2S = false;
        button2Press1S = true;
        button2Press = true;
      } 
      else {
        button2Press2S = false;
        button2Press1S = false;
        button2Press = true;
      }
    }
  } 
  else {
    // mark this as a press and release if we were pressed for less than a long press
    if (button2PressedCount == debounceCounter2) {
      button2WasReleased = true;

      button2PressRelease2S = false;
      button2PressRelease1S = false;
      button2PressRelease = false;

      if (button2Press2S) {
        button2PressRelease2S = true;
      } 
      else if (button2Press1S) {
        button2PressRelease1S = true;
      } 
      else if (button2Press) {
        button2PressRelease = true;
      }
    }

    // Reset the switch flags debounce counter      
    button2Press2S = false;
    button2Press1S = false;
    button2Press = false;      
    button2PressedCount = 0;
  }
}

// ************************************************************
// Check if button is pressed right now (just debounce)
// ************************************************************
boolean is2PressedNow() {
  return button2PressedCount == debounceCounter2;
}

// ************************************************************
// Check if button is pressed momentarily
// ************************************************************
boolean is2Pressed() {
  return button2Press;
}

// ************************************************************
// Check if button is pressed for a long time (> 2S)
// ************************************************************
boolean is2Pressed1S() {
  return button2Press1S;
}

// ************************************************************
// Check if button is pressed for a very long time (> 4S)
// ************************************************************
boolean is2Pressed2S() {
  return button2Press2S;
}

// ************************************************************
// Check if button is pressed for a short time (> 200mS) and released
// ************************************************************
boolean is2PressedRelease() {
  if (button2PressRelease) {
    button2PressRelease = false;
    return true;
  } 
  else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a long time (> 2) and released
// ************************************************************
boolean is2PressedRelease1S() {
  if (button2PressRelease1S) {
    button2PressRelease1S = false;
    return true;
  } 
  else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a very long time (> 2) and released
// ************************************************************
boolean is2PressedRelease2S() {
  if (button2PressRelease2S) {
    button2PressRelease2S = false;
    return true;
  } 
  else {
    return false;
  }
}

// ************************************************************
// Set the date/time in the RTC
// ************************************************************
void setRTC() {
  Clock.setClockMode(false); // false = 24h

  Clock.setYear(years);
  Clock.setMonth(months);
  Clock.setDate(days);
  Clock.setDoW(0);
  Clock.setHour(hours);
  Clock.setMinute(mins);
  Clock.setSecond(secs);
}

// ************************************************************
// Get the time from the RTC
// ************************************************************
void getRTCTime() {
  bool h12;
  bool PM;
  hours=Clock.getHour(h12,PM);
  mins=Clock.getMinute();
  secs=Clock.getSecond();
  years=Clock.getYear();
  bool century = false;
  months=Clock.getMonth(century);
  days=Clock.getDate();
}

// ************************************************************
// Get the temperature from the RTC
// ************************************************************
float getRTCTemp() {
  return Clock.getTemperature();
}

// ******************************************************************
// Check the ambient light through the LDR (Light Dependent Resistor)
// Smooths the reading over several reads.
//
// The LDR in bright light gives reading of around 50, the reading in
// total darkness is around 900.
// 
// The return value is the dimming count we are using.
// This is a value from 1 to 10, which controls a PWM value.
// ******************************************************************
int getDimmingFromLDR() {
  int rawSensorVal = 1023-analogRead(LDRPin);
  if (rawSensorVal < SENSOR_LOW_VALUE) rawSensorVal = SENSOR_LOW_VALUE;
  if (rawSensorVal > SENSOR_HIGH_VALUE) rawSensorVal = SENSOR_HIGH_VALUE;
  
  // Smooth the value
  double sensorDiff = rawSensorVal - sensorSmoothed;
  sensorSmoothed += (sensorDiff/SENSOR_SMOOTH_READINGS_DEFAULT);
  
  // Normalise it in the range 1-10
  double sensorSmoothedResult = ((sensorSmoothed - SENSOR_LOW_VALUE) / SENSOR_RANGE) * 9.0;  
  int returnValue = (int) (sensorSmoothedResult + 1);
  
  return returnValue;
}

