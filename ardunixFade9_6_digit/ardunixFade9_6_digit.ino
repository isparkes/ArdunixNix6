//**********************************************************************************
//**********************************************************************************
//* Main code for an Arduino based Nixie clock. Features:                          *
//*  - Real Time Clock interface for DS3231                                        *
//*  - Digit fading with configurable fade length                                  *
//*  - Digit scrollback with configurable scroll speed                             *
//*  - Configuration stored in EEPROM                                              *
//*  - Low hardware component count (as much as possible done in code)             *
//*  - Single button operation with software debounce                              *
//*  - Single 74141 for digit display (other versions use 2 or even 6!)            *
//*  - Automatic dimming, using a Light Dependent Resistor (Needs finishing)       *
//*  - Highly modular code                                                         *
//*                                                                                *
//*  isparkes@protonmail.ch                                                        *
//*                                                                                *
//* TODO: Make target HVGen voltage configurable, currently hardcoded              *
//*                                                                                *
//**********************************************************************************
//**********************************************************************************
#include <avr/io.h> 
#include <avr/interrupt.h> 
#include <EEPROM.h>

#include <DS3231.h>
#include <Wire.h>

const int EE_12_24 = 1;            // 12 or 24 hour mode
const int EE_FADE_STEPS = 2;       // How quickly we fade, higher = slower
const int EE_DATE_FORMAT = 3;      // Date format to display
const int EE_DAY_BLANKING = 4;     // Blanking setting
const int EE_DIM_DARK_LO = 5;      // Dimming dark value
const int EE_DIM_DARK_HI = 6;      // Dimming dark value
const int EE_BLANK_LEAD_ZERO = 7;  // If we blank leading zero on hours
const int EE_DIGIT_COUNT_HI = 8;   // The number of times we go round the main loop
const int EE_DIGIT_COUNT_LO = 9;   // The number of times we go round the main loop
const int EE_SCROLLBACK = 10;      // if we use scollback or not
const int EE_HVGEN_HI = 11;        // The HV generator value
const int EE_HVGEN_LO = 12;        // The HV generator value
const int EE_SCROLL_STEPS = 13;    // The steps in a scrollback
const int EE_SECS_MIDNIGHT = 14;   // Display mode secs since midnight
const int EE_DIM_BRIGHT_LO = 15;   // Dimming bright value
const int EE_DIM_BRIGHT_HI = 16;   // Dimming bright value

// Software version shown in config menu
const int softwareVersion = 0005;

// Display handling
const int DIGIT_DISPLAY_COUNT = 1000;                 // The number of times to traverse inner fade loop per digit
const int DIGIT_DISPLAY_ON = 0;                       // Switch on the digit at the beginning by default
const int DIGIT_DISPLAY_OFF = DIGIT_DISPLAY_COUNT-1;  // Switch off the digit at the end by default
const int DIGIT_DISPLAY_NEVER = -1;                   // When we don't want to switch on or off (i.e. blanking)
const int DISPLAY_COUNT_MAX = 2000;
const int DISPLAY_COUNT_MIN = 500;

const int SENSOR_LOW_THRESHOLD = 100;  // Dark
const int SENSOR_HIGH_THRESHOLD = 700; // Bright  
const int SENSOR_SMOOTH_READINGS = 100;// Speed at which the brighness adapts to changes

const int BLINK_COUNT_MAX = 25;                       // The number of impressions between blink state toggle

// How hard we drive the HV Generator, too low burns the MOSFET, too high does not give the coil time to load 
const int HVGEN_DEFAULT=200;
const int HVGEN_MIN=100;
const int HVGEN_MAX=400;
const int HVGEN_TARGET_VOLTAGE=180;

// How quickly the scroll works
const int SCROLL_STEPS_DEFAULT=4;
const int SCROLL_STEPS_MIN=1;
const int SCROLL_STEPS_MAX=40;

// The number of dispay impessions we need to fade by default
// 100 is about 1 second
const int FADE_STEPS_DEFAULT=50;
const int FADE_STEPS_MAX=200;
const int FADE_STEPS_MIN=20;

// Display mode, set per digit
const int BLANKED = 0;
const int DIMMED  = 1;
const int FADE    = 2;
const int NORMAL  = 3;
const int BLINK   = 4;
const int SCROLL  = 5;
const int BRIGHT  = 6;

const byte SECS_MAX = 60;   // 60 Seconds in a Min.
const byte MINS_MAX = 60;   //60 Mins in an hour.
const byte HOURS_MAX = 24;  // 24 Hours in a day. > Note: change the 24 to a 12 for non millitary time.

// Clock modes - normal running is MODE_TIME, other modes accessed by a middle length ( 1S < press < 2S ) button press
const int MODE_MIN = 0;
const int MODE_TIME = 0;
const int MODE_MINS_SET = MODE_TIME + 1;
const int MODE_HOURS_SET = MODE_MINS_SET + 1;
const int MODE_DAYS_SET = MODE_HOURS_SET + 1;
const int MODE_MONTHS_SET = MODE_DAYS_SET + 1;
const int MODE_YEARS_SET = MODE_MONTHS_SET + 1;
const int MODE_FADE_STEPS_UP = MODE_YEARS_SET + 1;                            // Mode "00"
const int MODE_FADE_STEPS_DOWN = MODE_FADE_STEPS_UP + 1;                      // Mode "01"
const int MODE_DISPLAY_HVGEN_UP = MODE_FADE_STEPS_DOWN + 1;                   // Mode "02"
const int MODE_DISPLAY_HVGEN_DOWN = MODE_DISPLAY_HVGEN_UP + 1;                // Mode "03"
const int MODE_DISPLAY_SCROLL_STEPS_UP = MODE_DISPLAY_HVGEN_DOWN + 1;         // Mode "04"
const int MODE_DISPLAY_SCROLL_STEPS_DOWN = MODE_DISPLAY_SCROLL_STEPS_UP + 1;  // Mode "05"
const int MODE_12_24 = MODE_DISPLAY_SCROLL_STEPS_DOWN + 1;                    // Mode "06" 0 = 24, 1 = 12
const int MODE_LEAD_BLANK = MODE_12_24 + 1;                                   // Mode "07" 1 = blanked
const int MODE_SCROLLBACK = MODE_LEAD_BLANK + 1;                              // Mode "08" 1 = use scrollback
const int MODE_SECS_SINCE_MIDNIGHT = MODE_SCROLLBACK + 1;                     // Mode "09" 1 = use secs
const int MODE_TEMP = MODE_SECS_SINCE_MIDNIGHT + 1;                           // Mode "10"
const int MODE_VERSION = MODE_TEMP + 1;                                       // Mode "11"
const int MODE_TUBE_TEST = MODE_VERSION + 1;                                  // Mode "12" - not displayed
const int MODE_MAX= MODE_TUBE_TEST + 1;

// Temporary display modes - accessed by a short press ( < 1S ) on the button when in MODE_TIME 
const int TEMP_MODE_MIN  = 0;
const int TEMP_MODE_DATE = 0; // Display the date for 5 S
const int TEMP_MODE_TEMP = 1; // Display the temperature for 5 S
const int TEMP_MODE_LDR  = 2; // Display the normalised LDR reading for 5S, returns a value from 100 (dark) to 999 (bright)
const int TEMP_MODE_MAX  = 2;

const int MODE_DIGIT_BURN = 99; // Digit burn mode

// RTC, uses Analogue pins A4 (SDA) and A5 (SCL)
DS3231 Clock;

//**********************************************************************************
//**********************************************************************************
//*                               Variables                                        *
//**********************************************************************************
//**********************************************************************************
// The divider value determines the frequency of the DC-DC converter
// The output voltage depends on the input voltage and the frequency:
// For 5V input:
// at a divider of 200, the frequency is 16MHz / 200 / 2 (final /2 because of toggle mode)
// Ranges for this are around 800 - 100, with 400 (20KHz) giving about 180V
// Drive up to 100 (80KHz) giving around 200V
int divider = HVGEN_DEFAULT;

// ***** Pin Defintions ****** Pin Defintions ****** Pin Defintions ******

// SN74141
int ledPin_0_a = 8;
int ledPin_0_b = 10;
int ledPin_0_c = 11;
int ledPin_0_d = 12;

// anode pins
int ledPin_a_6 = 0; // low  - Secs  units
int ledPin_a_5 = 1; //      - Secs  tens
int ledPin_a_4 = 2; //      - Mins  units
int ledPin_a_3 = 3; //      - Mins  tens
int ledPin_a_2 = 4; //      - Hours units
int ledPin_a_1 = 5; // high - Hours tens 

// button input
int inputPin1 = 7;

// Used for special mappings of the 74141 -> digit (wiring aid)
int decodeDigit[16] = {0,1,2,3,4,5,6,7,8,9,10,10,10,10,10,10};

// PWM pin used to drive the DC-DC converter
int hvDriverPin = 9;

// Internal (inside the box) tick led 
int tickLed = 13;

// PWM capable output for colons dimming
int colonsLed = 6;

//**********************************************************************************

// Driver pins for the anodes
int anodePins[6] = {ledPin_a_1,ledPin_a_2,ledPin_a_3,ledPin_a_4,ledPin_a_5,ledPin_a_6};

// precalculated values for turning on and off the HV generator
// Put these in TCCR1B to turn off and on
int tccrOff;
int tccrOn;

int sensorPin = A0; // Analog input pin for HV sense: HV divided through 390k and 4k7 divider, using 5V reference
int LDRPin = A1;    // Analog input for Light dependent resistor. 

// read from the milliseconds of the system clock
// does not need to be accurate, but it does have to be
// synchonised
long lastMillis = 0;
int intTick = 0;

// ************************ Display management ************************
int NumberArray[6]    = {0,0,0,0,0,0};
int currNumberArray[6]= {0,0,0,0,0,0};
int displayType[6]    = {FADE,FADE,FADE,FADE,FADE,FADE};
int fadeState[6]      = {0,0,0,0,0,0};

// how many fade steps to increment (out of DIGIT_DISPLAY_COUNT) each impression
// 100 is about 1 second
int dispCount = DIGIT_DISPLAY_COUNT;
int fadeSteps = FADE_STEPS_DEFAULT;
float fadeStep = dispCount / fadeSteps;
int digitOffCount = DIGIT_DISPLAY_OFF;
int scrollSteps = SCROLL_STEPS_DEFAULT;
boolean scrollback = true;

// For software blinking
int blinkCounter = 0;
boolean blinkState = true;

// leading digit blanking
boolean blankLeading = false;

// show time as secs since midnight
boolean secsSinceMidnight = false;

// Dimming value
const int DIM_VALUE = DIGIT_DISPLAY_COUNT/5;

long secsDisplayEnd;      // time for the end of the MMSS display
int  tempDisplayMode;

int acpOffset = 0;        // Used to provide one arm bandit scolling
int acpTick = 0;          // The number of counts before we scroll

int currentMode = MODE_TIME;   // Initial cold start mode 
int nextMode = currentMode; 

// ************************ Night time dimming ************************
int dimDark = SENSOR_LOW_THRESHOLD;
int dimBright = SENSOR_HIGH_THRESHOLD;
double sensorSmoothed = 0;
double sensorFactor = (double)(DIGIT_DISPLAY_OFF)/(double)(dimBright-dimDark);

// Time initial values, overwritten on startup if an RTC is there
byte hours = 12;
byte mins = 0;
byte secs = 0;
byte days = 1;
byte months = 1;
byte years = 14;
boolean mode12or24 = false;

// State variables for detecting changes
byte lastSec;

// **************************** LED management ***************************
int ledPWMVal;
boolean upOrDown;

// Blinking colons led in settings modes
int ledBlinkCtr = 0;
int ledBlinkNumber = 0;

// ********************** Input switch management **********************
// button debounce
int  button1PressedCount = 0;
long button1PressStartMillis = 0;
int  debounceCounter = 5; // Number of successive reads before we say the switch is down
boolean buttonWasReleased = false;
boolean buttonPress8S = false;
boolean buttonPress2S = false;
boolean buttonPress1S = false;
boolean buttonPress = false;
boolean buttonPressRelease8S = false;
boolean buttonPressRelease2S = false;
boolean buttonPressRelease1S = false;
boolean buttonPressRelease = false;

// **************************** digit healing ****************************
// This is a special mode which repairs cathode poisoning by driving a
// single element at full power. To be used with care!
// In theory, this should not be necessary because we have ACP every 10mins,
// but some tubes just want to be poisoned
int digitBurnDigit = 0;
int digitBurnValue = 0;

//**********************************************************************************
//**********************************************************************************
//*                                    Setup                                       *
//**********************************************************************************
//**********************************************************************************
void setup() 
{
  pinMode(ledPin_0_a, OUTPUT);      
  pinMode(ledPin_0_b, OUTPUT);      
  pinMode(ledPin_0_c, OUTPUT);      
  pinMode(ledPin_0_d, OUTPUT);    

  pinMode(ledPin_a_1, OUTPUT);      
  pinMode(ledPin_a_2, OUTPUT);      
  pinMode(ledPin_a_3, OUTPUT);     
  pinMode(ledPin_a_4, OUTPUT);     
  pinMode(ledPin_a_5, OUTPUT);     
  pinMode(ledPin_a_6, OUTPUT);     

  pinMode(tickLed, OUTPUT);     

  // NOTE:
  // Grounding the input pin causes it to actuate
  pinMode(inputPin1, INPUT ); // set the input pin 1
  digitalWrite(inputPin1, HIGH); // set pin 1 as a pull up resistor.

  // Set the driver pin to putput
  pinMode(hvDriverPin, OUTPUT);

  /* disable global interrupts while we set up them up */
  cli();

  // **************************** HV generator ****************************

  // Enable timer 1 Compare Output channel A in reset mode: TCCR1A.COM1A1 = 1, TCCR1A.COM1A0 = 0
  // forces the MOSFET into the off state, we cache these values in tccrOff and one, because we
  // use them very frequently
  TCCR1A = bit(COM1A1);

  // Get thecontrol register 1A with the HV generation on
  tccrOff = TCCR1A;

  // Enable timer 1 Compare Output channel A in toggle mode: TCCR1A.COM1A1 = 0, TCCR1A.COM1A0 = 1
  TCCR1A = bit(COM1A0);

  // Get thecontrol register 1A with the HV generation off
  tccrOn = TCCR1A;

  // Configure timer 1 for CTC mode: TCCR1B.WGM13 = 0, TCCR1B.WGM12 = 1, TCCR1A.WGM11 = 0, TCCR1A.WGM10 = 0
  TCCR1B = bit(WGM12); 

  // Set up prescaler to x1: TCCR1B.CS12 = 0, TCCR1B.CS11 = 0, TCCR1B.CS10 = 1
  TCCR1B |= bit(CS10); 

  // Set the divider to the value we have chosen
  OCR1A   = divider;

  /* enable global interrupts */
  sei();

  // Read EEPROM values
  readEEPROMValues();

  // Start the RTC communication
  Wire.begin();

  // Recover the time from the RTC
  getRTCTime();
  
  // temporary debugging
  //Serial.begin(9600);
  
  // Test if the button is pressed for factory reset
  checkButton1();
  if (is1Pressed()) {
    // Flash that we have accepted the factory reset
    for (int i = 0 ; i < 5 ; i++ ) {
      digitalWrite(tickLed, HIGH);
      delay(100);
      digitalWrite(tickLed, LOW);
      delay(100);
    }
  }
}

//**********************************************************************************
//**********************************************************************************
//*                              Main loop                                         *
//**********************************************************************************
//**********************************************************************************
void loop()     
{
  // Get an approximate mS time for internal control. Does not have to be exact!
  intTick = (millis() - lastMillis) % 1000;

  // Get the time
  getRTCTime();

  // Check button, we evaluate below
  checkButton1();

  // ******* Preview the next display mode *******
  if (is1Pressed2S()) {
    // Just jump back to the start
    nextMode = MODE_MIN;
  } else if (is1Pressed1S()) {
    nextMode = currentMode + 1;

    if (nextMode > MODE_MAX) {
      nextMode = MODE_MIN;
    }
  }

  // ******* Set the display mode *******
  if(is1PressedRelease2S()) {
    currentMode = MODE_MIN;

    // Store the EEPROM
    saveEEPROMValues();

    // Preset the display
    allFade();

    nextMode = currentMode;
  } else if(is1PressedRelease1S()) {
    currentMode++;

    if (currentMode > MODE_MAX) {
      currentMode = MODE_MIN;

      // Store the EEPROM
      saveEEPROMValues();

      // Preset the display
      allFade();
    }

    nextMode = currentMode;
  }

  // ************* Process the modes *************
  if (nextMode != currentMode) {
    if (nextMode == MODE_TIME) {
      loadNumberArrayTime();
      allFade();
    }

    if (nextMode == MODE_HOURS_SET) {
      loadNumberArrayTime();
      highlight0and1();
    }

    if (nextMode == MODE_MINS_SET) {
      loadNumberArrayTime();
      highlight2and3();
    }

    if (nextMode == MODE_DAYS_SET) {
      loadNumberArrayDate();
      highlight0and1();
    }

    if (nextMode == MODE_MONTHS_SET) {
      loadNumberArrayDate();
      highlight2and3();
    }

    if (nextMode == MODE_YEARS_SET) {
      loadNumberArrayDate();
      highlight4and5();
    }

    if (nextMode == MODE_FADE_STEPS_UP) {
      loadNumberArrayConfInt(fadeSteps,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_FADE_STEPS_DOWN) {
      loadNumberArrayConfInt(fadeSteps,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_DISPLAY_HVGEN_UP) {
      loadNumberArrayConfInt(divider,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_DISPLAY_HVGEN_DOWN) {
      loadNumberArrayConfInt(divider,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_DISPLAY_SCROLL_STEPS_UP) {
      loadNumberArrayConfInt(scrollSteps,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_DISPLAY_SCROLL_STEPS_DOWN) {
      loadNumberArrayConfInt(scrollSteps,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_12_24) {
      loadNumberArrayConfBool(mode12or24,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_LEAD_BLANK) {
      loadNumberArrayConfBool(blankLeading,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_SCROLLBACK) {
      loadNumberArrayConfBool(scrollback,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_SECS_SINCE_MIDNIGHT) {
      loadNumberArrayConfBool(secsSinceMidnight,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_TEMP) {
      loadNumberArrayTemp(nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_VERSION) {
      loadNumberArrayConfInt(softwareVersion,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (nextMode == MODE_TUBE_TEST) {
      loadNumberArrayTestDigits();
      allNormal();
    }

    if (nextMode == MODE_DIGIT_BURN) {
      digitOn(digitBurnDigit,digitBurnValue);
    }

  } else {
    if (currentMode == MODE_TIME) {
      if(is1PressedRelease()) {
        // Always start from the first mode, or increment the temp mode if we are already in a display
        if (millis() < secsDisplayEnd) {
          tempDisplayMode++;
        } else {
          tempDisplayMode = TEMP_MODE_MIN;
        }
        if (tempDisplayMode > TEMP_MODE_MAX) {
          tempDisplayMode = TEMP_MODE_MIN;
        }
        
        secsDisplayEnd = millis() + 5000;
      }

      if (millis() < secsDisplayEnd) {
        if (tempDisplayMode == TEMP_MODE_DATE) {
          loadNumberArrayDate();
        }

        if (tempDisplayMode == TEMP_MODE_TEMP) {
          loadNumberArrayTemp(MODE_TEMP);
        }
        
        if (tempDisplayMode == TEMP_MODE_LDR) {
          loadNumberArrayLDR();
        }

        allFade();
        
      } else {
        if (acpOffset > 0) {
          loadNumberArrayACP();
          allBright();
        } else {
          loadNumberArrayTime();
          allFade();
          
          // Apply leading blanking
          applyBlanking();
        }
      }    
    }

    if (currentMode == MODE_MINS_SET) {
      if(is1PressedRelease()) {
        incMins();
        setRTC();      
      }
      loadNumberArrayTime();
      highlight2and3();
    }

    if (currentMode == MODE_HOURS_SET) {
      if(is1PressedRelease()) {
        incHours();
        setRTC();      
      }
      loadNumberArrayTime();
      highlight0and1();
    }

    if (currentMode == MODE_DAYS_SET) {
      if(is1PressedRelease()) {
        incDays();
        setRTC();
      }
      loadNumberArrayDate();
      highlight0and1();
    }

    if (currentMode == MODE_MONTHS_SET) {
      if(is1PressedRelease()) {
        incMonths();
        setRTC();      
      }
      loadNumberArrayDate();
      highlight2and3();
    }

    if (currentMode == MODE_YEARS_SET) {
      if(is1PressedRelease()) {
        incYears();
        setRTC();      
      }
      loadNumberArrayDate();
      highlight4and5();
    }

    if (currentMode == MODE_FADE_STEPS_UP) {
      if(is1PressedRelease()) {
        fadeSteps++;
        if (fadeSteps > FADE_STEPS_MAX) {
          fadeSteps = FADE_STEPS_MIN;
        }
      }
      loadNumberArrayConfInt(fadeSteps,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
      fadeStep = dispCount / fadeSteps;
    }

    if (currentMode == MODE_FADE_STEPS_DOWN) {
      if(is1PressedRelease()) {
        fadeSteps--;
        if (fadeSteps < FADE_STEPS_MIN) {
          fadeSteps = FADE_STEPS_MAX;
        }
      }
      loadNumberArrayConfInt(fadeSteps,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
      fadeStep = dispCount / fadeSteps;
    }

    if (currentMode == MODE_DISPLAY_HVGEN_DOWN) {
      if(is1PressedRelease()) {
        divider-=10;
        if (divider < HVGEN_MIN) {
          divider = HVGEN_MAX;
        }
        OCR1A   = divider;
      }
      loadNumberArrayConfInt(divider,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_DISPLAY_HVGEN_UP) {
      if(is1PressedRelease()) {
        divider+=10;
        if (divider > HVGEN_MAX) {
          divider = HVGEN_MIN;
        }
        OCR1A   = divider;
      }
      loadNumberArrayConfInt(divider,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_DISPLAY_SCROLL_STEPS_DOWN) {
      if(is1PressedRelease()) {
        scrollSteps--;
        if (scrollSteps < SCROLL_STEPS_MIN) {
          scrollSteps = SCROLL_STEPS_MAX;
        }
      }
      loadNumberArrayConfInt(scrollSteps,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_DISPLAY_SCROLL_STEPS_UP) {
      if(is1PressedRelease()) {
        scrollSteps++;
        if (scrollSteps > SCROLL_STEPS_MAX) {
          scrollSteps = SCROLL_STEPS_MIN;
        }
      }
      loadNumberArrayConfInt(scrollSteps,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_12_24) {
      if(is1PressedRelease()) {
        mode12or24 = !mode12or24;
        setRTC();
      }
      loadNumberArrayConfBool(mode12or24,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_LEAD_BLANK) {
      if(is1PressedRelease()) {
        blankLeading = !blankLeading;
      }
      loadNumberArrayConfBool(blankLeading,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_SCROLLBACK) {
      if(is1PressedRelease()) {
        scrollback = !scrollback;
      }
      loadNumberArrayConfBool(scrollback,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_SECS_SINCE_MIDNIGHT) {
      if(is1PressedRelease()) {
        secsSinceMidnight = !secsSinceMidnight;
      }
      loadNumberArrayConfBool(secsSinceMidnight,nextMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    if (currentMode == MODE_TEMP) {
      loadNumberArrayTemp(currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    // We are setting calibration
    if (currentMode == MODE_VERSION) {
      loadNumberArrayConfInt(softwareVersion,currentMode-MODE_FADE_STEPS_UP);
      displayConfig();
    }

    // We are setting calibration
    if (currentMode == MODE_TUBE_TEST) {
      allNormal();
      loadNumberArrayTestDigits();
    }

    // We arerepairing cathode poisoning
    if (currentMode == MODE_DIGIT_BURN) {
      if(is1PressedRelease()) {
        digitBurnValue += 1;
        if (digitBurnValue > 9) {
          digitBurnValue = 0;

          digitOff(digitBurnDigit);
          digitBurnDigit += 1;
          if (digitBurnDigit > 5) {
            digitBurnDigit = 0;
          }
        }
      }
      digitOn(digitBurnDigit,digitBurnValue);
    }  
  }

  // Display.
  if ((currentMode != MODE_DIGIT_BURN) && (nextMode != MODE_DIGIT_BURN)) {
    // One armed bandit trigger every 10th minute
    if (acpOffset == 0) {
      if (((mins % 10) == 9) && (secs == 15)) {
        acpOffset = 1;
      }
    }

    // One armed bandit handling
    if (acpOffset > 0) {
      if (acpTick >= acpOffset) {
        acpTick = 0;
        acpOffset++;
        if (acpOffset == 50) {
          acpOffset = 0;
        }
      } else {
        acpTick++;
      }
    }

    // Set normal output display
    outputDisplay();
  }

  // get the LDR ambient light reading
  digitOffCount = getDimmingFromLDR();
  fadeStep = digitOffCount / fadeSteps;
  
  // Set leds
  setLeds();

  // Check the voltage
  checkHVVoltage();  
}

// ************************************************************
// Set the seconds tick led(s)
// We pulse the colons LED using PWM, however, because we do
// not know for sure how many loops we will do, we have to be
// a bit careful how we do this. We reset the counter to 0
// each time we get a new (up) second, and we stop the counter
// underflowing, because this gives a disturbing flash.
// ************************************************************
void setLeds()
{
  // tick led: Only update it on a second change (not every time round the loop)
  if (secs != lastSec) {
    lastSec = secs;

    if (secs == 0) {
      lastMillis = millis();
    }

    upOrDown = (secs % 2 == 0);

    // Reset the PWM every now and again, otherwise it drifts
    if (upOrDown) {
      ledPWMVal = 0;
    }

    // Do the tick led, on 1S, off 1S
    digitalWrite(tickLed,upOrDown);
  }

  // PWM led output
  switch (nextMode) {
    case MODE_TIME:
    {
      if (upOrDown) {
        ledPWMVal+=2;
      } else {
        ledPWMVal-=2;
      }

      // Stop it underflowing: This would cause a short, bright flash
      // Which interrupts the flow of zen
      if (ledPWMVal < 0) {
        ledPWMVal = 0;
      }

        // Dim the LED like the digit brightness
        int divider = ((DIGIT_DISPLAY_COUNT - digitOffCount) / 10) + 1;
        int ledVal = ledPWMVal / divider;
        analogWrite(colonsLed,ledVal);
      break;
    }

  default:
    {
      ledBlinkCtr++;
      if (ledBlinkCtr > 40) {
        ledBlinkCtr = 0;

        ledBlinkNumber++;
        if (ledBlinkNumber > (nextMode + 2)) {
          ledBlinkNumber = 0;
        }
      }

      if (ledBlinkNumber < nextMode) {
        if (ledBlinkCtr < 3) {
          analogWrite(colonsLed,50);
        } else {
          analogWrite(colonsLed,0);
        }
      }
    }
  }
}

//**********************************************************************************
//**********************************************************************************
//*                             Utility functions                                  *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArrayTime() {
  if(secsSinceMidnight & (currentMode == MODE_TIME)) {
    long secsCount = secs + (mins*60L) + (hours*3600L);
    NumberArray[5] = (secsCount / 1L) % 10;
    NumberArray[4] = (secsCount / 10L) % 10;
    NumberArray[3] = (secsCount / 100L) % 10;
    NumberArray[2] = (secsCount / 1000L) % 10;
    NumberArray[1] = (secsCount / 10000L) % 10;
    NumberArray[0] = (secsCount / 100000L) % 10;
  } else {
    NumberArray[5] = secs % 10;
    NumberArray[4] = secs / 10;
    NumberArray[3] = mins % 10;
    NumberArray[2] = mins / 10;
    NumberArray[1] = hours % 10;
    NumberArray[0] = hours / 10;
  }    
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArrayDate() {
  NumberArray[5] = years % 10;
  NumberArray[4] = years / 10;
  NumberArray[3] = months % 10;
  NumberArray[2] = months / 10;
  NumberArray[1] = days % 10;
  NumberArray[0] = days / 10;
}

// ************************************************************
// Break the temperature into displayable digits
// ************************************************************
void loadNumberArrayTemp(int confNum) {
  NumberArray[5] = (confNum) % 10;
  NumberArray[4] = (confNum / 10) % 10;
  float temp = getRTCTemp();
  int wholeDegrees = int(temp);
  temp=(temp-float(wholeDegrees))*100.0;
  int fractDegrees = int(temp);

  NumberArray[3] = fractDegrees % 10;
  NumberArray[2] =  fractDegrees / 10;
  NumberArray[1] =  wholeDegrees% 10;
  NumberArray[0] = wholeDegrees / 10;
}

// ************************************************************
// Break the LDR reading into displayable digits
// ************************************************************
void loadNumberArrayLDR() {
  NumberArray[5] = 0;
  NumberArray[4] = 0;
  
  NumberArray[3] = (digitOffCount / 1) % 10;
  NumberArray[2] = (digitOffCount / 10) % 10;
  NumberArray[1] = (digitOffCount / 100) % 10;
  NumberArray[0] = (digitOffCount / 1000) % 10;}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArrayTestDigits() {
  NumberArray[5] = secs % 10;
  NumberArray[4] = (secs+1) % 10;
  NumberArray[3] = (secs+2) % 10;
  NumberArray[2] = (secs+3) % 10;
  NumberArray[1] = (secs+4) % 10;
  NumberArray[0] = (secs+5) % 10;
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArrayACP() {
  NumberArray[5] = (secs + acpOffset) % 10;
  NumberArray[4] = (secs / 10 + acpOffset) % 10;
  NumberArray[3] = (mins + acpOffset) % 10;
  NumberArray[2] = (mins / 10 + acpOffset) % 10;
  NumberArray[1] = (hours+ acpOffset)  % 10;
  NumberArray[0] = (hours / 10 + acpOffset) % 10;
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void loadNumberArrayConfInt(int confValue, int confNum) {
  NumberArray[5] = (confNum) % 10;
  NumberArray[4] = (confNum / 10) % 10;
  NumberArray[3] = (confValue / 1) % 10;
  NumberArray[2] = (confValue / 10) % 10;
  NumberArray[1] = (confValue / 100) % 10;
  NumberArray[0] = (confValue / 1000) % 10;
}

// ************************************************************
// Show an boolean configuration value
// ************************************************************
void loadNumberArrayConfBool(boolean confValue, int confNum) {
  int boolInt;
  if (confValue) {boolInt = 1;} else {boolInt = 0;}
  NumberArray[5] = (confNum) % 10;
  NumberArray[4] = (confNum / 10) % 10;
  NumberArray[3] = boolInt;
  NumberArray[2] = 0;
  NumberArray[1] = 0;
  NumberArray[0] = 0;
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
// Do a single complete display, including any fading and
// dimming requested. Performs the display loop
// DIGIT_DISPLAY_COUNT times for each digit, with no delays.
//
// Possibly we will split this down to be one digit per call
// if we need more reactiveness. 
// ************************************************************
void outputDisplay()
{
  int digitOnTime;
  int digitOffTime;
  int digitSwitchTime;
  float digitSwitchTimeFloat;  

  // used to blank all leading digits if 0
  boolean leadingZeros = true;

  for( int i = 0 ; i < 6 ; i ++ )
  {
    int tmpDispType = displayType[i];

    switch(tmpDispType) {
      case BLANKED:
        {
          digitOnTime = DIGIT_DISPLAY_NEVER;
          digitOffTime = DIGIT_DISPLAY_ON;
          break;
        }
        case DIMMED:
        {
          digitOnTime = DIGIT_DISPLAY_ON;
          digitOffTime = DIM_VALUE;
          break;
        }
        case BRIGHT:
        {
          digitOnTime = DIGIT_DISPLAY_ON;
          digitOffTime = DIGIT_DISPLAY_OFF;
          break;
        }
        case FADE:
        case NORMAL:
        {
          digitOnTime = DIGIT_DISPLAY_ON;
          digitOffTime = digitOffCount;
          break;
        }
        case BLINK:
        {
          if (blinkState) {
            digitOnTime = DIGIT_DISPLAY_ON;
            digitOffTime = digitOffCount;
          } else {
            digitOnTime = DIGIT_DISPLAY_NEVER;
            digitOffTime = DIGIT_DISPLAY_ON;
          }
          break;
        }
        case SCROLL:
        {
          digitOnTime = DIGIT_DISPLAY_ON;
          digitOffTime = digitOffCount;
          break;
        }
    }

    // Do scrollback when we are going to 0
    if ((NumberArray[i] != currNumberArray[i]) && 
      (NumberArray[i] == 0) &&
      scrollback) {
      tmpDispType = SCROLL;
    }

    // manage fading, each impression we show 1 fade step less of the old
    // digit and 1 fade step more of the new
    // manage fading, each impression we show 1 fade step less of the old
    // digit and 1 fade step more of the new
    if (tmpDispType == SCROLL) {
      digitSwitchTime = DIGIT_DISPLAY_OFF;
      if (NumberArray[i] != currNumberArray[i]) {
        if (fadeState[i] == 0) {
          // Start the fade
          fadeState[i] = scrollSteps;
        }

        if (fadeState[i] == 1) {
          // finish the fade
          fadeState[i] = 0;
          currNumberArray[i] = currNumberArray[i] - 1;
        } else if (fadeState[i] > 1) {
          // Continue the scroll countdown
          fadeState[i] =fadeState[i]-1;
        }
      }
    } else if (tmpDispType == FADE) {
      if (NumberArray[i] != currNumberArray[i]) {
        if (fadeState[i] == 0) {
          // Start the fade
          fadeState[i] = fadeSteps;
          digitSwitchTime = (int) fadeState[i] * fadeStep;
        }
      }

      if (fadeState[i] == 1) {
        // finish the fade
        fadeState[i] =0;
        currNumberArray[i] = NumberArray[i];
        digitSwitchTime = DIGIT_DISPLAY_COUNT;
      } else if (fadeState[i] > 1) {
        // Continue the fade
        fadeState[i] =fadeState[i]-1;
        digitSwitchTime = (int) fadeState[i] * fadeStep;
      }
    } else {
      digitSwitchTime = DIGIT_DISPLAY_COUNT;
      currNumberArray[i] = NumberArray[i];
    }

    for (int timer = 0 ; timer < DIGIT_DISPLAY_COUNT ; timer++) {
      if (timer == digitOnTime) {
        digitOn(i,currNumberArray[i]);
      }

      if  (timer == digitSwitchTime) {
        SetSN74141Chip(NumberArray[i]);
      }

      if (timer == digitOffTime ) {
        digitOff(i);
      }
    }
  }

  // Deal with blink, calculate if we are on or off
  blinkCounter++;
  if (blinkCounter == BLINK_COUNT_MAX) {
    blinkCounter = 0;
    blinkState = !blinkState;
  }
}

// ************************************************************
// Set a digit with the given value and turn the HVGen on
// ************************************************************
void digitOn(int digit, int value) {
  SetSN74141Chip(value);
  digitalWrite(anodePins[digit], HIGH);
  TCCR1A = tccrOn;
}

// ************************************************************
// Finish displaying a digit and turn the HVGen on
// ************************************************************
void digitOff(int digit) {
  TCCR1A = tccrOff;
  digitalWrite(anodePins[digit], LOW);
}

// ************************************************************
// See if the button was pressed and debounce. We perform a
// sort of preview here, then confirm by releasing. We track
// 3 lengths of button press: momentarily, 1S and 2S. 
// ************************************************************
void checkButton1() {
  if (digitalRead(inputPin1) == 0) {
    buttonWasReleased = false;
    
    long nowMillis = millis();

    // We need consecutive pressed counts to treat this is pressed    
    if (button1PressedCount < debounceCounter) {
      button1PressedCount += 1;
      // If we reach the debounce point, mark the start time
      if (button1PressedCount == debounceCounter) {
        button1PressStartMillis = nowMillis;
      }
    } else {
      // We are pressed and held, maintain the press states
      if ((nowMillis - button1PressStartMillis) > 8000) {
        buttonPress8S = true;
        buttonPress2S = true;
        buttonPress1S = true;
        buttonPress = true;
      } else if ((nowMillis - button1PressStartMillis) > 2000) {
        buttonPress8S = false;
        buttonPress2S = true;
        buttonPress1S = true;
        buttonPress = true;
      } else if ((nowMillis - button1PressStartMillis) > 1000) {
        buttonPress8S = false;
        buttonPress2S = false;
        buttonPress1S = true;
        buttonPress = true;
      } else {
        buttonPress8S = false;
        buttonPress2S = false;
        buttonPress1S = false;
        buttonPress = true;
      }
    }
  } else {
    // mark this as a press and release if we were pressed for less than a long press
    if (button1PressedCount == debounceCounter) {
      buttonWasReleased = true;

      buttonPressRelease8S = false;
      buttonPressRelease2S = false;
      buttonPressRelease1S = false;
      buttonPressRelease = false;

      if (buttonPress8S) {
        buttonPressRelease8S = true;
      } else if (buttonPress2S) {
        buttonPressRelease2S = true;
      } else if (buttonPress1S) {
        buttonPressRelease1S = true;
      } else if (buttonPress) {
        buttonPressRelease = true;
      }
    }

    // Reset the switch flags debounce counter      
    buttonPress8S = false;
    buttonPress2S = false;
    buttonPress1S = false;
    buttonPress = false;      
    button1PressedCount = 0;
  }
}

// ************************************************************
// Check if button is pressed right now (just debounce)
// ************************************************************
boolean is1PressedNow() {
  return button1PressedCount == debounceCounter;
}

// ************************************************************
// Check if button is pressed momentarily
// ************************************************************
boolean is1Pressed() {
  return buttonPress;
}

// ************************************************************
// Check if button is pressed for a long time (> 1S)
// ************************************************************
boolean is1Pressed1S() {
  return buttonPress1S;
}

// ************************************************************
// Check if button is pressed for a moderately long time (> 2S)
// ************************************************************
boolean is1Pressed2S() {
  return buttonPress2S;
}

// ************************************************************
// Check if button is pressed for a very long time (> 8S)
// ************************************************************
boolean is1Pressed8S() {
  return buttonPress8S;
}

// ************************************************************
// Check if button is pressed for a short time (> 200mS) and released
// ************************************************************
boolean is1PressedRelease() {
  if (buttonPressRelease) {
    buttonPressRelease = false;
    return true;
  } else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a long time (> 2) and released
// ************************************************************
boolean is1PressedRelease1S() {
  if (buttonPressRelease1S) {
    buttonPressRelease1S = false;
    return true;
  } else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a very moderately time (> 2) and released
// ************************************************************
boolean is1PressedRelease2S() {
  if (buttonPressRelease2S) {
    buttonPressRelease2S = false;
    return true;
  } else {
    return false;
  }
}

// ************************************************************
// Check if button is pressed for a very long time (> 8) and released
// ************************************************************
boolean is1PressedRelease8S() {
  if (buttonPressRelease8S) {
    buttonPressRelease8S = false;
    return true;
  } else {
    return false;
  }
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
// Display preset - apply leading zero blanking
// ************************************************************
void applyBlanking() {
  // If we are not blanking, just get out
  if (blankLeading == false) {
    return;
  }
  
  if (secsSinceMidnight) {
    // blank as many digits as we can
    for (int blankingIdx = 0 ; blankingIdx <6 ; blankingIdx++) {
      if (NumberArray[blankingIdx] == 0) {
        if (displayType[blankingIdx] != BLANKED) {
          displayType[blankingIdx] = BLANKED;
        }
      } else {
        return;
      }
    }
  } else {
    // We only want to blank the hours tens digit
    if (NumberArray[0] == 0) {
      if (displayType[0] != BLANKED) {
        displayType[0] = BLANKED;
      }
    }
  }
}

// ************************************************************
// Display preset
// ************************************************************
void allFade() {
  if (displayType[0] != FADE) displayType[0] = FADE;
  if (displayType[1] != FADE) displayType[1] = FADE;
  if (displayType[2] != FADE) displayType[2] = FADE;
  if (displayType[3] != FADE) displayType[3] = FADE;
  if (displayType[4] != FADE) displayType[4] = FADE;
  if (displayType[5] != FADE) displayType[5] = FADE;
}

// ************************************************************
// Display preset
// ************************************************************
void allBright() {
  if (displayType[0] != BRIGHT) displayType[0] = BRIGHT;
  if (displayType[1] != BRIGHT) displayType[1] = BRIGHT;
  if (displayType[2] != BRIGHT) displayType[2] = BRIGHT;
  if (displayType[3] != BRIGHT) displayType[3] = BRIGHT;
  if (displayType[4] != BRIGHT) displayType[4] = BRIGHT;
  if (displayType[5] != BRIGHT) displayType[5] = BRIGHT;
}

// ************************************************************
// Display preset, highlight digits 0 and 1
// ************************************************************
void highlight0and1() {
  if (displayType[0] != BRIGHT) displayType[0] = BRIGHT;
  if (displayType[1] != BRIGHT) displayType[1] = BRIGHT;
  if (displayType[2] != DIMMED) displayType[2] = DIMMED;
  if (displayType[3] != DIMMED) displayType[3] = DIMMED;
  if (displayType[4] != DIMMED) displayType[4] = DIMMED;
  if (displayType[5] != DIMMED) displayType[5] = DIMMED;
}

// ************************************************************
// Display preset, highlight digits 2 and 3
// ************************************************************
void highlight2and3() {
  if (displayType[0] != DIMMED) displayType[0] = DIMMED;
  if (displayType[1] != DIMMED) displayType[1] = DIMMED;
  if (displayType[2] != BRIGHT) displayType[2] = BRIGHT;
  if (displayType[3] != BRIGHT) displayType[3] = BRIGHT;
  if (displayType[4] != DIMMED) displayType[4] = DIMMED;
  if (displayType[5] != DIMMED) displayType[5] = DIMMED;
}

// ************************************************************
// Display preset, highlight digits 4 and 5
// ************************************************************
void highlight4and5() {
  if (displayType[0] != DIMMED) displayType[0] = DIMMED;
  if (displayType[1] != DIMMED) displayType[1] = DIMMED;
  if (displayType[2] != DIMMED) displayType[2] = DIMMED;
  if (displayType[3] != DIMMED) displayType[3] = DIMMED;
  if (displayType[4] != BRIGHT) displayType[4] = BRIGHT;
  if (displayType[5] != BRIGHT) displayType[5] = BRIGHT;
}

// ************************************************************
// Display preset
// ************************************************************
void allNormal() {
  if (displayType[0] != NORMAL) displayType[0] = NORMAL;
  if (displayType[1] != NORMAL) displayType[1] = NORMAL;
  if (displayType[2] != NORMAL) displayType[2] = NORMAL;
  if (displayType[3] != NORMAL) displayType[3] = NORMAL;
  if (displayType[4] != NORMAL) displayType[4] = NORMAL;
  if (displayType[5] != NORMAL) displayType[5] = NORMAL;
}

// ************************************************************
// Display preset
// ************************************************************
void displayConfig() {
  if (displayType[0] != NORMAL) displayType[0] = BRIGHT;
  if (displayType[1] != NORMAL) displayType[1] = BRIGHT;
  if (displayType[2] != NORMAL) displayType[2] = BRIGHT;
  if (displayType[3] != NORMAL) displayType[3] = BRIGHT;
  if (displayType[4] != BLINK)  displayType[4] = BLINK;
  if (displayType[5] != BLINK)  displayType[5] = BLINK;
}

// ************************************************************
// increment the time by 1 Sec
// ************************************************************
void incSecs() {
  secs++;
  if (secs >= SECS_MAX) {
    secs = 0;
  }
}

// ************************************************************
// increment the time by 1 min
// ************************************************************
void incMins() {
  mins++;
  secs=0;

  if (mins >= MINS_MAX) {  
    mins = 0;
  }
}

// ************************************************************
// increment the time by 1 hour
// ************************************************************
void incHours() {
  hours++;

  if (hours >= HOURS_MAX) {
    hours = 0;
  }
}

// ************************************************************
// increment the date by 1 day
// ************************************************************
void incDays() {
  days++;

  int maxDays;
  switch (months)
  {
  case 4:
  case 6:
  case 9:
  case 11:
    {
      maxDays = 31;
      break;
    }
  case 2:
    {
      maxDays = 28;
      break;
    }
  default:
    {
      maxDays = 31;
    }
  }

  if (days > maxDays) {
    days = 1;
  }
}

// ************************************************************
// increment the month by 1 month
// ************************************************************
void incMonths() {
  months++;

  if (months > 12) {
    months = 1;
  }
}

// ************************************************************
// increment the year by 1 year
// ************************************************************
void incYears() {
  years++;

  if (years > 50) {
    years = 14;
  }
}

// ************************************************************
// Set the date/time in the RTC
// ************************************************************
void setRTC() {
  Clock.setClockMode(mode12or24); // false = 24h

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

// ************************************************************
// Save current values back to EEPROM 
// ************************************************************
void saveEEPROMValues() {
  EEPROM.write(EE_FADE_STEPS,fadeSteps);
  EEPROM.write(EE_12_24,mode12or24);
  EEPROM.write(EE_BLANK_LEAD_ZERO,blankLeading);
  EEPROM.write(EE_SCROLLBACK,scrollback);
  EEPROM.write(EE_HVGEN_HI,divider / 256);
  EEPROM.write(EE_HVGEN_LO,divider % 256);
  EEPROM.write(EE_SCROLL_STEPS,scrollSteps);
  EEPROM.write(EE_SECS_MIDNIGHT,secsSinceMidnight);
}

// ************************************************************
// read EEPROM values
// ************************************************************
void readEEPROMValues() {
  fadeSteps = EEPROM.read(EE_FADE_STEPS);
  if ((fadeSteps < FADE_STEPS_MIN) || (fadeSteps > FADE_STEPS_MAX)) {
    fadeSteps = FADE_STEPS_DEFAULT;
  }

  mode12or24 = EEPROM.read(EE_12_24);
  blankLeading = EEPROM.read(EE_BLANK_LEAD_ZERO);
  scrollback = EEPROM.read(EE_SCROLLBACK);
  secsSinceMidnight = EEPROM.read(EE_SECS_MIDNIGHT);

  divider = EEPROM.read(EE_HVGEN_HI)*256 + EEPROM.read(EE_HVGEN_LO);
  if ((divider < HVGEN_MIN) || (divider > HVGEN_MAX)) {
    divider = HVGEN_DEFAULT;
  }

  scrollSteps = EEPROM.read(EE_SCROLL_STEPS);
  if ((scrollSteps < SCROLL_STEPS_MIN) || (scrollSteps > SCROLL_STEPS_MAX)) {
    scrollSteps = SCROLL_STEPS_DEFAULT;
  }
}

// ************************************************************
// If we are currently dimmed
// ************************************************************
//boolean getDimmed() {
//  if (dimStart > dimEnd) {
//    // dim before midnight
//    return ((hours >= dimStart) || (hours < dimEnd));
//  } else if (dimStart < dimEnd) {
//    // dim at or after midnight
//    return ((hours >= dimStart) && (hours < dimEnd));
//  } else {
//    // no dimming if dimStart = dimEnd
//    return false;
//  }
//}

// ************************************************************
// Adjust the HV gen to achieve the voltage we require
// ************************************************************
double checkHVVoltage() {
  int rawSensorVal = analogRead(sensorPin);  
  double sensorVoltage = rawSensorVal * 5.0  / 1024.0;
  double externalVoltage = sensorVoltage * 394.7 / 4.7;

  if (externalVoltage > HVGEN_TARGET_VOLTAGE) {
  //Serial.println(externalVoltage);
    TCCR1A = tccrOff;
  } 
  else {
    TCCR1A = tccrOn;
  }

  return externalVoltage;
}  

// ******************************************************************
// Check the ambient light through the LDR (Light Dependent Resistor)
// Smooths the reading over several reads.
//
// The LDR in bright light gives reading of around 50, the reading in
// total darkness is around 900.
// 
// The return value is the dimming count we are using. 999 is full
// brightness, 100 is very dim.
// 
// Because the floating point calculation may return more than the
// maximzm value, we have to clamp it as the final step
// ******************************************************************
int getDimmingFromLDR() {
  int rawSensorVal = 1023-analogRead(LDRPin);
  double sensorDiff = rawSensorVal - sensorSmoothed;
  sensorSmoothed += (sensorDiff/SENSOR_SMOOTH_READINGS);
  
  double sensorSmoothedResult = sensorSmoothed - dimDark;
  if (sensorSmoothedResult < dimDark) sensorSmoothedResult = dimDark;
  if (sensorSmoothedResult > dimBright) sensorSmoothedResult = dimBright;
  sensorSmoothedResult = (sensorSmoothedResult-dimDark)*sensorFactor;
  
  int returnValue = sensorSmoothedResult+dimDark;
  if (returnValue > DIGIT_DISPLAY_OFF) returnValue = DIGIT_DISPLAY_OFF;
  
  return returnValue;
}  
