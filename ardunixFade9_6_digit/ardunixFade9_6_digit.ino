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
//*  - Automatic dimming, using a Light Dependent Resistor                         *
//*  - RGB back light management                                                   *
//*  - Highly modular code                                                         *
//*                                                                                *
//*  isparkes@protonmail.ch                                                        *
//*                                                                                *
//**********************************************************************************
//**********************************************************************************
#include <avr/io.h> 
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
const int EE_PULSE_LO = 11;        // The pulse on width for the PWM mode
const int EE_PULSE_HI = 12;        // The pulse on width for the PWM mode
const int EE_SCROLL_STEPS = 13;    // The steps in a scrollback
const int EE_BACKLIGHT_MODE= 14;   // The back light node
const int EE_DIM_BRIGHT_LO = 15;   // Dimming bright value
const int EE_DIM_BRIGHT_HI = 16;   // Dimming bright value
const int EE_DIM_SMOOTH_SPEED = 17;// Dimming adaptation speed
const int EE_RED_INTENSITY = 18;   // Red channel backlight max intensity
const int EE_GRN_INTENSITY = 19;   // Green channel backlight max intensity
const int EE_BLU_INTENSITY = 20;   // Blue channel backlight max intensity
const int EE_HV_VOLTAGE = 21;      // The HV voltage we want to use

// Software version shown in config menu
const int softwareVersion = 29;

// Display handling
const int DIGIT_DISPLAY_COUNT = 1000;                 // The number of times to traverse inner fade loop per digit
const int DIGIT_DISPLAY_ON = 0;                       // Switch on the digit at the beginning by default
const int DIGIT_DISPLAY_OFF = DIGIT_DISPLAY_COUNT-1;  // Switch off the digit at the end by default
const int DIGIT_DISPLAY_NEVER = -1;                   // When we don't want to switch on or off (i.e. blanking)
const int DISPLAY_COUNT_MAX = 2000;                   // Maximum value we can set to
const int DISPLAY_COUNT_MIN = 500;                    // Minimum value we can set to
const int DIGIT_DISPLAY_MIN_DIM = 100;                // The minimum viable dim count

const int SENSOR_LOW_MIN = 0;
const int SENSOR_LOW_MAX = 900;
const int SENSOR_LOW_DEFAULT = 100;  // Dark
const int SENSOR_HIGH_MIN = 0;
const int SENSOR_HIGH_MAX = 900;
const int SENSOR_HIGH_DEFAULT = 700; // Bright

const int SENSOR_SMOOTH_READINGS_MIN = 1;
const int SENSOR_SMOOTH_READINGS_MAX = 255;
const int SENSOR_SMOOTH_READINGS_DEFAULT = 100;       // Speed at which the brighness adapts to changes

const int BLINK_COUNT_MAX = 25;                       // The number of impressions between blink state toggle

// The target voltage we want to achieve
const int HVGEN_TARGET_VOLTAGE_DEFAULT=180;
const int HVGEN_TARGET_VOLTAGE_MIN=150;
const int HVGEN_TARGET_VOLTAGE_MAX=200;
int hvTargetVoltage = HVGEN_TARGET_VOLTAGE_DEFAULT;

// The PWM parameters
const int PWM_TOP_DEFAULT = 1000;
const int PWM_TOP_MIN = 300;
const int PWM_TOP_MAX = 10000;
int pwmTop = PWM_TOP_DEFAULT;
const int PWM_PULSE_DEFAULT = 150;
const int PWM_PULSE_MIN = 50;
const int PWM_PULSE_MAX = 500;
int pulseWidth = PWM_PULSE_DEFAULT;

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

const byte SECS_MAX = 60;
const byte MINS_MAX = 60;
const byte HOURS_MAX = 24;

const byte COLOUR_CNL_MAX = 15;
const byte COLOUR_CNL_DEFAULT = COLOUR_CNL_MAX;
const byte COLOUR_CNL_MIN = 0;

// Clock modes - normal running is MODE_TIME, other modes accessed by a middle length ( 1S < press < 2S ) button press
const int MODE_MIN = 0;
const int MODE_TIME = 0;

// Time setting, need all six digits, so no flashing mode indicator
const int MODE_MINS_SET = MODE_TIME + 1;
const int MODE_HOURS_SET = MODE_MINS_SET + 1;
const int MODE_DAYS_SET = MODE_HOURS_SET + 1;
const int MODE_MONTHS_SET = MODE_DAYS_SET + 1;
const int MODE_YEARS_SET = MODE_MONTHS_SET + 1;

// Basic settings
const int MODE_12_24 = MODE_YEARS_SET + 1;                                    // Mode "00" 0 = 24, 1 = 12
const int MODE_LEAD_BLANK = MODE_12_24 + 1;                                   // Mode "01" 1 = blanked
const int MODE_SCROLLBACK = MODE_LEAD_BLANK + 1;                              // Mode "02" 1 = use scrollback
const int MODE_DATE_FORMAT = MODE_SCROLLBACK + 1;                             // Mode "03"
const int MODE_DAY_BLANKING = MODE_DATE_FORMAT + 1;                           // Mode "04"

// Display tricks
const int MODE_FADE_STEPS_UP = MODE_DAY_BLANKING + 1;                         // Mode "05"
const int MODE_FADE_STEPS_DOWN = MODE_FADE_STEPS_UP + 1;                      // Mode "06"
const int MODE_DISPLAY_SCROLL_STEPS_UP = MODE_FADE_STEPS_DOWN + 1;            // Mode "07"
const int MODE_DISPLAY_SCROLL_STEPS_DOWN = MODE_DISPLAY_SCROLL_STEPS_UP + 1;  // Mode "08"

// Back light
const int MODE_BACKLIGHT_MODE = MODE_DISPLAY_SCROLL_STEPS_DOWN + 1;           // Mode "09"
const int MODE_RED_CNL = MODE_BACKLIGHT_MODE + 1;                             // Mode "10"
const int MODE_GRN_CNL = MODE_RED_CNL + 1;                                    // Mode "11"
const int MODE_BLU_CNL = MODE_GRN_CNL + 1;                                    // Mode "12"

// HV generation
const int MODE_TARGET_HV_UP = MODE_BLU_CNL + 1;                               // Mode "13"
const int MODE_TARGET_HV_DOWN = MODE_TARGET_HV_UP + 1;                        // Mode "14"
const int MODE_PULSE_UP = MODE_TARGET_HV_DOWN + 1;                            // Mode "15"
const int MODE_PULSE_DOWN = MODE_PULSE_UP + 1;                                // Mode "16"

// Temperature
const int MODE_TEMP =  MODE_PULSE_DOWN + 1;                                   // Mode "17"

// Software Version
const int MODE_VERSION = MODE_TEMP + 1;                                       // Mode "18"

// Tube test - all six digits, so no flashing mode indicator
const int MODE_TUBE_TEST = MODE_VERSION + 1;

const int MODE_MAX = MODE_TUBE_TEST + 1;

// Pseudo mode - burn the tubes and nothing else
const int MODE_DIGIT_BURN = 99;                                              // Digit burn mode - accesible by super long press

// Temporary display modes - accessed by a short press ( < 1S ) on the button when in MODE_TIME 
const int TEMP_MODE_MIN  = 0;
const int TEMP_MODE_DATE = 0; // Display the date for 5 S
const int TEMP_MODE_TEMP = 1; // Display the temperature for 5 S
const int TEMP_MODE_LDR  = 2; // Display the normalised LDR reading for 5S, returns a value from 100 (dark) to 999 (bright)
const int TEMP_MODE_MAX  = 2;

const int DATE_FORMAT_MIN = 0;
const int DATE_FORMAT_YYMMDD = 0;
const int DATE_FORMAT_MMDDYY = 1;
const int DATE_FORMAT_DDMMYY = 2;
const int DATE_FORMAT_MAX = 2;
const int DATE_FORMAT_DEFAULT = DATE_FORMAT_DDMMYY;

const int DAY_BLANKING_MIN = 0;
const int DAY_BLANKING_NEVER = 0;
const int DAY_BLANKING_WEEKEND = 1;
const int DAY_BLANKING_WEEKDAY = 2;
const int DAY_BLANKING_ALWAYS = 3;
const int DAY_BLANKING_MAX = 3;
const int DAY_BLANKING_DEFAULT = DAY_BLANKING_NEVER;

const int BACKLIGHT_MIN = 0;
const int BACKLIGHT_FIXED = 0;
const int BACKLIGHT_PULSE = 1;
const int BACKLIGHT_CYCLE = 2;
const int BACKLIGHT_MAX = 2;
const int BACKLIGHT_DEFAULT = BACKLIGHT_MIN;

// RTC, uses Analogue pins A4 (SDA) and A5 (SCL)
DS3231 Clock;

//**********************************************************************************
//**********************************************************************************
//*                               Variables                                        *
//**********************************************************************************
//**********************************************************************************

// ***** Pin Defintions ****** Pin Defintions ****** Pin Defintions ******

// SN74141 
int ledPin_0_a = 13;  // package pin 19
int ledPin_0_b = 10;  // package pin 16
int ledPin_0_c = 8;   // package pin 14
int ledPin_0_d = 12;  // package pin 18 

// anode pins
int ledPin_a_6 = 0;  // low  - Secs  units // package pin 2
int ledPin_a_5 = 1;  //      - Secs  tens  // package pin 3
int ledPin_a_4 = 2;  //      - Mins  units // package pin 4
int ledPin_a_3 = 4;  //      - Mins  tens  // package pin 6
int ledPin_a_2 = A2; //      - Hours units // package pin 25
int ledPin_a_1 = A3; // high - Hours tens  // package pin 26

// button input
int inputPin1 = 7;   // package pin 13

// PWM pin used to drive the DC-DC converter
int hvDriverPin = 9; // package pin 15

// Tick led - PWM capable 
int tickLed = 11;    // package pin 17

// PWM capable output for backlight
int RLed = 6;        // package pin 12
int GLed = 5;        // package pin 11
int BLed = 3;        // package pin 5

int sensorPin = A0; // Analog input pin for HV sense: HV divided through 390k and 4k7 divider, using 5V reference
int LDRPin = A1;    // Analog input for Light dependent resistor. 

//**********************************************************************************

// Used for special mappings of the 74141 -> digit (wiring aid)
// allows the board wiring to be much simpler<
int decodeDigit[16] = {2,3,7,6,4,5,1,0,9,8,10,10,10,10,10,10};

// Driver pins for the anodes
int anodePins[6] = {ledPin_a_1,ledPin_a_2,ledPin_a_3,ledPin_a_4,ledPin_a_5,ledPin_a_6};

// precalculated values for turning on and off the HV generator
// Put these in TCCR1B to turn off and on
int tccrOff;
int tccrOn;
int rawHVADCThreshold;

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

// Dimming value
const int DIM_VALUE = DIGIT_DISPLAY_COUNT/5;

long secsDisplayEnd;      // time for the end of the MMSS display
int  tempDisplayMode;

int acpOffset = 0;        // Used to provide one arm bandit scolling
int acpTick = 0;          // The number of counts before we scroll

int currentMode = MODE_TIME;   // Initial cold start mode 
int nextMode = currentMode; 

// ************************ Ambient light dimming ************************
int dimDark = SENSOR_LOW_DEFAULT;
int dimBright = SENSOR_HIGH_DEFAULT;
double sensorSmoothed = 0;
double sensorFactor = (double)(DIGIT_DISPLAY_OFF)/(double)(dimBright-dimDark);
int sensorSmoothCount = SENSOR_SMOOTH_READINGS_DEFAULT;

// Time initial values, overwritten on startup if an RTC is there
byte hours = 12;
byte mins = 0;
byte secs = 0;
byte days = 1;
byte months = 1;
byte years = 14;
byte dow = 0; // 0 = Sun
boolean mode12or24 = false;

// State variables for detecting changes
byte lastSec;

int dateFormat = DATE_FORMAT_DEFAULT;
int dayBlanking = DAY_BLANKING_DEFAULT;
boolean blanked = false;

// **************************** LED management ***************************
int ledPWMVal;
boolean upOrDown;

// Blinking colons led in settings modes
int ledBlinkCtr = 0;
int ledBlinkNumber = 0;

int backlightMode = BACKLIGHT_DEFAULT;

// Back light intensities
int redCnl = COLOUR_CNL_DEFAULT;
int grnCnl = COLOUR_CNL_DEFAULT;
int bluCnl = COLOUR_CNL_DEFAULT;
int cycleCount = 0;
const int CYCLE_COUNT_MAX = 10;
byte ledCycleCount[3] = {0,0,0};
double ledCycleValue[3] = {0,0,0};
double ledCycleIncrement[3] = {0,0,0};

// ********************** Input switch management **********************
// button debounce
int  button1PressedCount = 0;
long button1PressStartMillis = 0;
const int  debounceCounter = 5; // Number of successive reads before we say the switch is down
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
  pinMode(RLed, OUTPUT);     
  pinMode(GLed, OUTPUT);     
  pinMode(BLed, OUTPUT);     

  // NOTE:
  // Grounding the input pin causes it to actuate
  pinMode(inputPin1, INPUT ); // set the input pin 1
  digitalWrite(inputPin1, HIGH); // set pin 1 as a pull up resistor.

  // Set the driver pin to putput
  pinMode(hvDriverPin, OUTPUT);

  // Read EEPROM values
  readEEPROMValues();

  /* disable global interrupts while we set up them up */
  cli();

  // **************************** HV generator ****************************

  TCCR1A = 0;    // disable all PWM on Timer1 whilst we set it up
  TCCR1B = 0;    // disable all PWM on Timer1 whilst we set it up
  ICR1 = pwmTop; // Our starting point for the period	

  // Configure timer 1 for Fast PWM mode via ICR1, with prescaling=1
  TCCR1A = (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1<<WGM12) | (1 << CS10);

  tccrOff = TCCR1A;
  
  TCCR1A |= (1 <<  COM1A1);  // enable PWM on port PD4 in non-inverted compare mode 2
 
  tccrOn = TCCR1A;
  
  OCR1A = pulseWidth;  // Pulse width of the on time

  // Set up timer 2 like timer 0 (for RGB leds)
  TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << CS22);
  
  /* enable global interrupts */
  sei();

  // Start the RTC communication
  Wire.begin();

  // Recover the time from the RTC
  getRTCTime();
  
  // Test if the button is pressed for factory reset
  for (int i = 0 ; i < debounceCounter*2 ; i++ ) {
    checkButton1(); 
  }
  
  // Detect factory reset: button pressed on start
  if (is1PressedNow()) {
    // Flash 10 x to signal that we have accepted the factory reset
    for (int i = 0 ; i < 10 ; i++ ) {
      digitalWrite(tickLed, HIGH);
      delay(100);
      digitalWrite(tickLed, LOW);
      delay(100);
      
      factoryReset();
    }
  }
  
  // Pre-calculate the ADC threshold reading, this saves all
  // of the floating point business
  rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);
}

//**********************************************************************************
//**********************************************************************************
//*                              Main loop                                         *
//**********************************************************************************
//**********************************************************************************
void loop()     
{
  // get the time 
  getRTCTime();

  // Check button, we evaluate below
  checkButton1();

  // ******* Preview the next display mode *******
  // What is previewed here will get actioned when 
  // the button is released
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
  if(is1PressedRelease8S()) {
    // 8 Sec press toggles burn mode
    if (currentMode == MODE_DIGIT_BURN) {
      currentMode = MODE_MIN;
    } else {
      currentMode = MODE_DIGIT_BURN;
    }

    nextMode = currentMode;
  } else if(is1PressedRelease2S()) {
    currentMode = MODE_MIN;

    // Store the EEPROM if we exit the config mode
    saveEEPROMValues();

    // Preset the display
    allFade();

    nextMode = currentMode;
  } else if(is1PressedRelease1S()) {
    currentMode++;

    if (currentMode > MODE_MAX) {
      currentMode = MODE_MIN;

      // Store the EEPROM if we exit the config mode
      saveEEPROMValues();

      // Preset the display
      allFade();
    }

    nextMode = currentMode;
  }

  // ************* Process the modes *************
  if (nextMode != currentMode) {
    // turn off blanking
    blanked = false;
    
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
      highlightDaysDateFormat();
    }

    if (nextMode == MODE_MONTHS_SET) {
      loadNumberArrayDate();
      highlightMonthsDateFormat();
    }

    if (nextMode == MODE_YEARS_SET) {
      loadNumberArrayDate();
      highlightYearsDateFormat();
    }

    if (nextMode == MODE_12_24) {
      loadNumberArrayConfBool(mode12or24,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_LEAD_BLANK) {
      loadNumberArrayConfBool(blankLeading,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_SCROLLBACK) {
      loadNumberArrayConfBool(scrollback,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_DATE_FORMAT) {
      loadNumberArrayConfInt(dateFormat,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_DAY_BLANKING) {
      loadNumberArrayConfInt(dayBlanking,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_FADE_STEPS_UP) {
      loadNumberArrayConfInt(fadeSteps,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_FADE_STEPS_DOWN) {
      loadNumberArrayConfInt(fadeSteps,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_DISPLAY_SCROLL_STEPS_UP) {
      loadNumberArrayConfInt(scrollSteps,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_DISPLAY_SCROLL_STEPS_DOWN) {
      loadNumberArrayConfInt(scrollSteps,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_BACKLIGHT_MODE) {
      loadNumberArrayConfInt(backlightMode,nextMode-MODE_12_24);
      displayConfig();
    }
    
    if (nextMode == MODE_RED_CNL) {
      if (backlightMode == BACKLIGHT_CYCLE) {
        // Skip if we are in cycle mode
        nextMode++;
        currentMode++;
      }
      loadNumberArrayConfInt(redCnl,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_GRN_CNL) {
      if (backlightMode == BACKLIGHT_CYCLE) {
        // Skip if we are in cycle mode
        nextMode++;
        currentMode++;
      }
      loadNumberArrayConfInt(grnCnl,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_BLU_CNL) {
      if (backlightMode == BACKLIGHT_CYCLE) {
        // Skip if we are in cycle mode
        nextMode++;
        currentMode++;
      }
      loadNumberArrayConfInt(bluCnl,nextMode-MODE_12_24);
      displayConfig();
    }
    
    if (nextMode == MODE_TARGET_HV_UP) {
      loadNumberArrayConfInt(hvTargetVoltage,nextMode-MODE_12_24);
      displayConfig();
    }
    
    if (nextMode == MODE_TARGET_HV_DOWN) {
      loadNumberArrayConfInt(hvTargetVoltage,nextMode-MODE_12_24);
      displayConfig();
    }
    
    if (nextMode == MODE_PULSE_UP) {
      loadNumberArrayConfInt(pulseWidth,nextMode-MODE_12_24);
      displayConfig();
    }
    
    if (nextMode == MODE_PULSE_DOWN) {
      loadNumberArrayConfInt(pulseWidth,nextMode-MODE_12_24);
      displayConfig();
    }
    
    if (nextMode == MODE_TEMP) {
      loadNumberArrayTemp(nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_VERSION) {
      loadNumberArrayConfInt(softwareVersion,nextMode-MODE_12_24);
      displayConfig();
    }

    if (nextMode == MODE_TUBE_TEST) {
      loadNumberArrayTestDigits();
      allNormal();
    }

    if (nextMode == MODE_DIGIT_BURN) {
      // Nothing
    }

  } else {
    if (currentMode == MODE_TIME) {

      checkBlanking();
      
      if(is1PressedRelease()) {
        if (blanked) {
          // just turn off blanking - it will turn back on on it's own
          blanked = false;
        } else {
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
      highlightDaysDateFormat();
    }

    if (currentMode == MODE_MONTHS_SET) {
      if(is1PressedRelease()) {
        incMonths();
        setRTC();      
      }
      loadNumberArrayDate();
      highlightMonthsDateFormat();
    }

    if (currentMode == MODE_YEARS_SET) {
      if(is1PressedRelease()) {
        incYears();
        setRTC();      
      }
      loadNumberArrayDate();
      highlightYearsDateFormat();
    }

    if (currentMode == MODE_12_24) {
      if(is1PressedRelease()) {
        mode12or24 = !mode12or24;
        setRTC();
      }
      loadNumberArrayConfBool(mode12or24,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_LEAD_BLANK) {
      if(is1PressedRelease()) {
        blankLeading = !blankLeading;
      }
      loadNumberArrayConfBool(blankLeading,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_SCROLLBACK) {
      if(is1PressedRelease()) {
        scrollback = !scrollback;
      }
      loadNumberArrayConfBool(scrollback,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_DATE_FORMAT) {
      if(is1PressedRelease()) {
        dateFormat++;
        if (dateFormat > DATE_FORMAT_MAX) {
          dateFormat = DATE_FORMAT_MIN;
        }
      }
      loadNumberArrayConfInt(dateFormat,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_DAY_BLANKING) {
      if(is1PressedRelease()) {
        dayBlanking++;
        if (dayBlanking > DAY_BLANKING_MAX) {
          dayBlanking = DAY_BLANKING_MIN;
        }
      }
      loadNumberArrayConfInt(dayBlanking,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_FADE_STEPS_UP) {
      if(is1PressedRelease()) {
        fadeSteps++;
        if (fadeSteps > FADE_STEPS_MAX) {
          fadeSteps = FADE_STEPS_MIN;
        }
      }
      loadNumberArrayConfInt(fadeSteps,currentMode-MODE_12_24);
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
      loadNumberArrayConfInt(fadeSteps,currentMode-MODE_12_24);
      displayConfig();
      fadeStep = dispCount / fadeSteps;
    }

    if (currentMode == MODE_DISPLAY_SCROLL_STEPS_DOWN) {
      if(is1PressedRelease()) {
        scrollSteps--;
        if (scrollSteps < SCROLL_STEPS_MIN) {
          scrollSteps = SCROLL_STEPS_MAX;
        }
      }
      loadNumberArrayConfInt(scrollSteps,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_DISPLAY_SCROLL_STEPS_UP) {
      if(is1PressedRelease()) {
        scrollSteps++;
        if (scrollSteps > SCROLL_STEPS_MAX) {
          scrollSteps = SCROLL_STEPS_MIN;
        }
      }
      loadNumberArrayConfInt(scrollSteps,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_BACKLIGHT_MODE) {
      if(is1PressedRelease()) {
        backlightMode++;
        if (backlightMode > BACKLIGHT_MAX) {
          backlightMode = BACKLIGHT_MIN;
        }
      }
      loadNumberArrayConfInt(backlightMode,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_RED_CNL) {
      if (backlightMode == BACKLIGHT_CYCLE) {
        // Skip if we are in cycle mode
        nextMode++;
        currentMode++;
      }
      
      if(is1PressedRelease()) {
        redCnl++;
        if (redCnl > COLOUR_CNL_MAX) {
          redCnl = COLOUR_CNL_MIN;
        }
      }
      loadNumberArrayConfInt(redCnl,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_GRN_CNL) {
      if (backlightMode == BACKLIGHT_CYCLE) {
        // Skip if we are in cycle mode
        nextMode++;
        currentMode++;
      }
      
      if(is1PressedRelease()) {
        grnCnl++;
        if (grnCnl > COLOUR_CNL_MAX) {
          grnCnl = COLOUR_CNL_MIN;
        }
      }
      loadNumberArrayConfInt(grnCnl,currentMode-MODE_12_24);
      displayConfig();
    }

    if (currentMode == MODE_BLU_CNL) {
      if (backlightMode == BACKLIGHT_CYCLE) {
        // Skip if we are in cycle mode
        nextMode++;
        currentMode++;
      }
      
      if(is1PressedRelease()) {
        bluCnl++;
        if (bluCnl > COLOUR_CNL_MAX) {
          bluCnl = COLOUR_CNL_MIN;
        }
      }
      loadNumberArrayConfInt(bluCnl,currentMode-MODE_12_24);
      displayConfig();
    }
    
    if (currentMode == MODE_TARGET_HV_UP) {
      if(is1PressedRelease()) {
        hvTargetVoltage+=5;
        if (hvTargetVoltage > HVGEN_TARGET_VOLTAGE_MAX) {
          hvTargetVoltage = HVGEN_TARGET_VOLTAGE_MIN;
        }
      }
      loadNumberArrayConfInt(hvTargetVoltage,currentMode-MODE_12_24);
      rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);
      displayConfig();
    }
    
    if (currentMode == MODE_TARGET_HV_DOWN) {
      if(is1PressedRelease()) {
        hvTargetVoltage-=5;
        if (hvTargetVoltage < HVGEN_TARGET_VOLTAGE_MIN) {
          hvTargetVoltage = HVGEN_TARGET_VOLTAGE_MAX;
        }
      }
      loadNumberArrayConfInt(hvTargetVoltage,currentMode-MODE_12_24);
      rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);
      displayConfig();
    }
    
    if (currentMode == MODE_PULSE_UP) {
      if(is1PressedRelease()) {
        pulseWidth+=10;
        if (pulseWidth > PWM_PULSE_MAX) {
          pulseWidth = PWM_PULSE_MIN;
        }
      }
      OCR1A = pulseWidth;
      loadNumberArrayConfInt(pulseWidth,currentMode-MODE_12_24);
      displayConfig();
    }
    
    if (currentMode == MODE_PULSE_DOWN) {
      if(is1PressedRelease()) {
        pulseWidth-=10;
        if (pulseWidth < PWM_PULSE_MIN) {
          pulseWidth = PWM_PULSE_MAX;
        }
      }
      OCR1A = pulseWidth;
      loadNumberArrayConfInt(pulseWidth,currentMode-MODE_12_24);
      displayConfig();
    }
    
    if (currentMode == MODE_TEMP) {
      loadNumberArrayTemp(currentMode-MODE_12_24);
      displayConfig();
    }

    // We are setting calibration
    if (currentMode == MODE_VERSION) {
      loadNumberArrayConfInt(softwareVersion,currentMode-MODE_12_24);
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
    }  
  }

  // get the LDR ambient light reading
  digitOffCount = getDimmingFromLDR();
  fadeStep = digitOffCount / fadeSteps;
  
  // Display. Digit Burn has a different (non-multiplexed) handling
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
  } else {
    // Digit burn mode
    digitOn(digitBurnDigit,digitBurnValue);
  }

  // Regulate the voltage
  checkHVVoltage();
  
  // Set leds
  setLeds();
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
  // Pulse PWM generation - Only update it on a second change (not every time round the loop)
  if (secs != lastSec) {
    lastSec = secs;

    upOrDown = (secs % 2 == 0);

    // Reset the PWM every now and again, otherwise it drifts
    if (upOrDown) {
      ledPWMVal = 0;
    }
  }

  // calculate the PWM value
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
  if (ledPWMVal > 255) {
    ledPWMVal = 255;
  }
  
  // calculate the dimmed PWM val
  float dimFactor = (float) digitOffCount / (float) DIGIT_DISPLAY_COUNT;
  int dimmedPWMVal = (int)((float) ledPWMVal * dimFactor);
  
  // Tick led output
  analogWrite(tickLed,dimmedPWMVal);
  
  // RGB Backlight PWM led output
  if (currentMode == MODE_TIME) {
    switch (backlightMode) {
      case BACKLIGHT_FIXED:
        analogWrite(RLed,redCnl*16);
        analogWrite(GLed,grnCnl*16);
        analogWrite(BLed,bluCnl*16);
        break;
      case BACKLIGHT_PULSE:
        analogWrite(RLed,dimmedPWMVal*redCnl/16);
        analogWrite(GLed,dimmedPWMVal*grnCnl/16);
        analogWrite(BLed,dimmedPWMVal*bluCnl/16);
        break;
      case BACKLIGHT_CYCLE:
        // slow everything down
        cycleCount++;
        if (cycleCount > CYCLE_COUNT_MAX) {
          cycleCount = 0;
        
          for (int i = 0 ; i < 3 ; i++) {
            if (ledCycleCount[i] <= 0) {
              ledCycleCount[i] = random(256);
              double randomAbs = random(10);
              ledCycleIncrement[i] = (double) randomAbs-5 / (double) 1000;
            }
          
            ledCycleValue[i] += ledCycleIncrement[i];
          
            if (ledCycleValue[i] >= 255) {
              ledCycleValue[i] = 255;
              ledCycleIncrement[i] = -ledCycleIncrement[i];
            }
            if (ledCycleValue[i] <= 0) {
              ledCycleValue[i] = 0;
              ledCycleIncrement[i] = -ledCycleIncrement[i];
            }
          
            ledCycleCount[i]--;
          }
          analogWrite(RLed,ledCycleValue[0]);
          analogWrite(GLed,ledCycleValue[1]);
          analogWrite(BLed,ledCycleValue[2]);
        }
    }
  } else {
    // Settings modes
    ledBlinkCtr++;
    if (ledBlinkCtr > 40) {
      ledBlinkCtr = 0;

      ledBlinkNumber++;
      if (ledBlinkNumber > nextMode) {
        // Make a pause
        ledBlinkNumber = -2;
      }
    }

    if ((ledBlinkNumber <= nextMode) && (ledBlinkNumber > 0)){
      if (ledBlinkCtr < 3) {
        analogWrite(RLed,255);
        analogWrite(GLed,255);
        analogWrite(BLed,255);
      } else {
        analogWrite(RLed,0);
        analogWrite(GLed,0);
        analogWrite(BLed,0);
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
  NumberArray[5] = secs % 10;
  NumberArray[4] = secs / 10;
  NumberArray[3] = mins % 10;
  NumberArray[2] = mins / 10;
  NumberArray[1] = hours % 10;
  NumberArray[0] = hours / 10;
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArrayDate() {
  switch(dateFormat) {
    case DATE_FORMAT_YYMMDD:
      NumberArray[5] = days % 10;
      NumberArray[4] = days / 10;
      NumberArray[3] = months % 10;
      NumberArray[2] = months / 10;
      NumberArray[1] = years % 10;
      NumberArray[0] = years / 10;
      break;
    case DATE_FORMAT_MMDDYY:
      NumberArray[5] = years % 10;
      NumberArray[4] = years / 10;
      NumberArray[3] = days % 10;
      NumberArray[2] = days / 10;
      NumberArray[1] = months % 10;
      NumberArray[0] = months / 10;
      break;
    case DATE_FORMAT_DDMMYY:
      NumberArray[5] = years % 10;
      NumberArray[4] = years / 10;
      NumberArray[3] = months % 10;
      NumberArray[2] = months / 10;
      NumberArray[1] = days % 10;
      NumberArray[0] = days / 10;
      break;
  }
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
// Test digits
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
// Do the Anti Cathode Poisoning
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
// Show a boolean configuration value
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
  int tmpDispType;

  // used to blank all leading digits if 0
  boolean leadingZeros = true;

  for( int i = 0 ; i < 6 ; i ++ )
  {
    if (blanked) {
      tmpDispType = BLANKED;
    } else {
      tmpDispType = displayType[i];
    }

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
  TCCR1A = tccrOn;
  SetSN74141Chip(value);
  digitalWrite(anodePins[digit], HIGH);
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
  
  // We only want to blank the hours tens digit
  if (NumberArray[0] == 0) {
    if (displayType[0] != BLANKED) {
      displayType[0] = BLANKED;
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
// highlight years taking into account the date format
// ************************************************************
void highlightYearsDateFormat() {
  switch(dateFormat) {
    case DATE_FORMAT_YYMMDD:
      highlight0and1();
      break;
    case DATE_FORMAT_MMDDYY:
      highlight4and5();
      break;
    case DATE_FORMAT_DDMMYY:
      highlight4and5();
      break;
  }
}

// ************************************************************
// highlight years taking into account the date format
// ************************************************************
void highlightMonthsDateFormat() {
  switch(dateFormat) {
    case DATE_FORMAT_YYMMDD:
      highlight2and3();
      break;
    case DATE_FORMAT_MMDDYY:
      highlight0and1();
      break;
    case DATE_FORMAT_DDMMYY:
      highlight2and3();
      break;
  }
}

// ************************************************************
// highlight days taking into account the date format
// ************************************************************
void highlightDaysDateFormat() {
  switch(dateFormat) {
    case DATE_FORMAT_YYMMDD:
      highlight4and5();
      break;
    case DATE_FORMAT_MMDDYY:
      highlight2and3();
      break;
    case DATE_FORMAT_DDMMYY:
      highlight0and1();
      break;
  }
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
  if (displayType[0] != BRIGHT) displayType[0] = BRIGHT;
  if (displayType[1] != BRIGHT) displayType[1] = BRIGHT;
  if (displayType[2] != BRIGHT) displayType[2] = BRIGHT;
  if (displayType[3] != BRIGHT) displayType[3] = BRIGHT;
  if (displayType[4] != BLINK)  displayType[4] = BLINK;
  if (displayType[5] != BLINK)  displayType[5] = BLINK;
}

// ************************************************************
// Display preset
// ************************************************************
void displayConfig3() {
  if (displayType[0] != BLANKED) displayType[0] = BLANKED;
  if (displayType[1] != NORMAL) displayType[1] = BRIGHT;
  if (displayType[2] != NORMAL) displayType[2] = BRIGHT;
  if (displayType[3] != NORMAL) displayType[3] = BRIGHT;
  if (displayType[4] != BLINK)  displayType[4] = BLINK;
  if (displayType[5] != BLINK)  displayType[5] = BLINK;
}

// ************************************************************
// Display preset
// ************************************************************
void displayConfig2() {
  if (displayType[0] != BLANKED) displayType[0] = BLANKED;
  if (displayType[1] != BLANKED) displayType[1] = BLANKED;
  if (displayType[2] != NORMAL) displayType[2] = BRIGHT;
  if (displayType[3] != NORMAL) displayType[3] = BRIGHT;
  if (displayType[4] != BLINK)  displayType[4] = BLINK;
  if (displayType[5] != BLINK)  displayType[5] = BLINK;
}

// ************************************************************
// Display preset
// ************************************************************
void allBlanked() {
  if (displayType[0] != BLANKED) displayType[0] = BLANKED;
  if (displayType[1] != BLANKED) displayType[1] = BLANKED;
  if (displayType[2] != BLANKED) displayType[2] = BLANKED;
  if (displayType[3] != BLANKED) displayType[3] = BLANKED;
  if (displayType[4] != BLANKED) displayType[4] = BLANKED;
  if (displayType[5] != BLANKED) displayType[5] = BLANKED;
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
      // we won't worry about leap years!!!
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
  int dow = dayofweek(2000 + years, months, days);
  Clock.setDoW(dow);
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
  dow=Clock.getDoW();
}
  
// ************************************************************
// Check the blanking
// ************************************************************
void checkBlanking() {
  // Check day blanking, but only when we are in
  // normal time mode
  if ((secs == 0) && (currentMode == MODE_TIME)) {
    switch(dayBlanking) {
      case DAY_BLANKING_NEVER:
        blanked = false;
        break;
      case DAY_BLANKING_WEEKEND:
        blanked = ((dow == 0) || (dow == 6));
        break;
      case DAY_BLANKING_WEEKDAY:
        blanked = ((dow > 0) && (dow < 6));
        break;
      case DAY_BLANKING_ALWAYS:
        blanked = true;
        break;
    }
  }  
}

// ************************************************************
// Get the day of week from the RTC
// ************************************************************
int getRTCDoW() {
  return Clock.getDoW();
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
  EEPROM.write(EE_12_24,mode12or24);
  EEPROM.write(EE_FADE_STEPS,fadeSteps);
  EEPROM.write(EE_DATE_FORMAT, dateFormat);
  EEPROM.write(EE_DAY_BLANKING, dayBlanking);
  EEPROM.write(EE_DIM_DARK_LO, dimDark % 256);
  EEPROM.write(EE_DIM_DARK_HI, dimDark / 256);
  EEPROM.write(EE_BLANK_LEAD_ZERO,blankLeading);
  EEPROM.write(EE_SCROLLBACK,scrollback);
  EEPROM.write(EE_SCROLL_STEPS,scrollSteps);
  EEPROM.write(EE_DIM_BRIGHT_LO, dimBright % 256);
  EEPROM.write(EE_DIM_BRIGHT_HI, dimBright / 256);
  EEPROM.write(EE_DIM_SMOOTH_SPEED, sensorSmoothCount);
  EEPROM.write(EE_RED_INTENSITY,redCnl);
  EEPROM.write(EE_GRN_INTENSITY,grnCnl);
  EEPROM.write(EE_BLU_INTENSITY,bluCnl);
  EEPROM.write(EE_BACKLIGHT_MODE,backlightMode);
  EEPROM.write(EE_HV_VOLTAGE,hvTargetVoltage);
  EEPROM.write(EE_PULSE_LO, pulseWidth % 256);
  EEPROM.write(EE_PULSE_HI, pulseWidth / 256);  
}

// ************************************************************
// read EEPROM values
// ************************************************************
void readEEPROMValues() {
  mode12or24 = EEPROM.read(EE_12_24);
  
  fadeSteps = EEPROM.read(EE_FADE_STEPS);
  if ((fadeSteps < FADE_STEPS_MIN) || (fadeSteps > FADE_STEPS_MAX)) {
    fadeSteps = FADE_STEPS_DEFAULT;
  }
  
  dateFormat = EEPROM.read(EE_DATE_FORMAT);
  if ((dateFormat < DATE_FORMAT_MIN) || (dateFormat > DATE_FORMAT_MAX)) {
    dateFormat = DATE_FORMAT_DEFAULT;
  }

  dayBlanking = EEPROM.read(EE_DAY_BLANKING);
  if ((dayBlanking < DAY_BLANKING_MIN) || (dayBlanking > DAY_BLANKING_MAX)) {
    dayBlanking = DAY_BLANKING_DEFAULT;
  }

  dimDark = EEPROM.read(EE_DIM_DARK_HI)*256 + EEPROM.read(EE_DIM_DARK_LO);
  if ((dimDark < SENSOR_LOW_MIN) || (dimDark > SENSOR_LOW_MAX)) {
    dimDark = SENSOR_LOW_DEFAULT;
  }

  blankLeading = EEPROM.read(EE_BLANK_LEAD_ZERO);
  scrollback = EEPROM.read(EE_SCROLLBACK);

  scrollSteps = EEPROM.read(EE_SCROLL_STEPS);
  if ((scrollSteps < SCROLL_STEPS_MIN) || (scrollSteps > SCROLL_STEPS_MAX)) {
    scrollSteps = SCROLL_STEPS_DEFAULT;
  }
  
  dimBright = EEPROM.read(EE_DIM_BRIGHT_HI)*256 + EEPROM.read(EE_DIM_BRIGHT_LO);
  if ((dimBright < SENSOR_HIGH_MIN) || (dimBright > SENSOR_HIGH_MAX)) {
    dimBright = SENSOR_HIGH_DEFAULT;
  }  
  
  sensorSmoothCount = EEPROM.read(EE_DIM_SMOOTH_SPEED);
  if ((sensorSmoothCount < SENSOR_SMOOTH_READINGS_MIN) || (sensorSmoothCount > SENSOR_SMOOTH_READINGS_MAX)) {
    sensorSmoothCount = SENSOR_SMOOTH_READINGS_DEFAULT;
  }
  
  dateFormat = EEPROM.read(EE_DATE_FORMAT);
  if ((dateFormat < DATE_FORMAT_MIN) || (dateFormat > DATE_FORMAT_MAX)) {
    dateFormat = DATE_FORMAT_DEFAULT;
  }
  
  dayBlanking = EEPROM.read(EE_DAY_BLANKING);
  if ((dayBlanking < DAY_BLANKING_MIN) || (dayBlanking > DAY_BLANKING_MAX)) {
    dayBlanking = DAY_BLANKING_DEFAULT;
  }

  backlightMode = EEPROM.read(EE_BACKLIGHT_MODE);
  if ((backlightMode < BACKLIGHT_MIN) || (backlightMode > BACKLIGHT_MAX)) {
    backlightMode = BACKLIGHT_DEFAULT;
  }
  
  redCnl = EEPROM.read(EE_RED_INTENSITY);
  if ((redCnl < COLOUR_CNL_MIN) || (redCnl > COLOUR_CNL_MAX)) {
    redCnl = COLOUR_CNL_DEFAULT;
  }
  
  grnCnl = EEPROM.read(EE_GRN_INTENSITY);
  if ((grnCnl < COLOUR_CNL_MIN) || (grnCnl > COLOUR_CNL_MAX)) {
    grnCnl = COLOUR_CNL_DEFAULT;
  }
  
  bluCnl = EEPROM.read(EE_BLU_INTENSITY);
  if ((bluCnl < COLOUR_CNL_MIN) || (bluCnl > COLOUR_CNL_MAX)) {
    bluCnl = COLOUR_CNL_DEFAULT;
  }
  
  hvTargetVoltage = EEPROM.read(EE_HV_VOLTAGE);
  if ((hvTargetVoltage < HVGEN_TARGET_VOLTAGE_MIN) || (hvTargetVoltage > HVGEN_TARGET_VOLTAGE_MAX)) {
    hvTargetVoltage = HVGEN_TARGET_VOLTAGE_DEFAULT;
  }
  
  pulseWidth = EEPROM.read(EE_PULSE_HI)*256 + EEPROM.read(EE_PULSE_LO);
  if ((pulseWidth < PWM_PULSE_MIN) || (pulseWidth > PWM_PULSE_MAX)) {
    pulseWidth = PWM_PULSE_DEFAULT;
  }
}

// ************************************************************
// Reset EEPROM values back to what they once were
// ************************************************************
void factoryReset() {
  mode12or24 = false;
  blankLeading = false;
  scrollback = true;
  fadeSteps = FADE_STEPS_DEFAULT;
  dateFormat = DATE_FORMAT_DEFAULT;
  dayBlanking = DAY_BLANKING_DEFAULT;
  dimDark = SENSOR_LOW_DEFAULT;
  scrollSteps = SCROLL_STEPS_DEFAULT;
  dimBright = SENSOR_HIGH_DEFAULT;
  sensorSmoothCount = SENSOR_SMOOTH_READINGS_DEFAULT;
  dateFormat = DATE_FORMAT_DEFAULT;
  dayBlanking = DAY_BLANKING_DEFAULT;  
  backlightMode = BACKLIGHT_DEFAULT;
  redCnl = COLOUR_CNL_DEFAULT;
  grnCnl = COLOUR_CNL_DEFAULT;
  bluCnl = COLOUR_CNL_DEFAULT;
  hvTargetVoltage = HVGEN_TARGET_VOLTAGE_DEFAULT;
  pulseWidth = PWM_PULSE_DEFAULT;
  
  saveEEPROMValues();
}

// ************************************************************
// Adjust the HV gen to achieve the voltage we require
// Pre-calculate the threshold value of the ADC read and make
// a simple comparison against this for speed
// We control only the PWM "off" time, because the "on" time
// affects the current consumption and MOSFET heating 
// ************************************************************
void checkHVVoltage() {
  // No point in calculating the voltage if we are blanked
  if (blanked) {
    return;
  }
  
  int rawSensorVal = analogRead(sensorPin);
  
  // check the read voltage
  if (rawSensorVal > rawHVADCThreshold) {
    pwmTop += 10;
    
    if (pwmTop >= PWM_TOP_MAX) {
      pwmTop = PWM_TOP_MAX;
    }
  } else {
    pwmTop -= 10;
    
    // check that we have not got a silly reading: On time cannot be more than 50% of TOP time 
    if (pulseWidth > pwmTop) {
      pwmTop = pulseWidth + pwmTop;
    }
  }
  
  ICR1 = pwmTop; // Our starting point for the period
}

// ************************************************************
// Calculate the target value for the ADC reading to get the
// defined voltage
// ************************************************************
int getRawHVADCThreshold(double targetVoltage) {
  double externalVoltage = targetVoltage * 4.7 / 394.7 * 1023 / 5;
  int rawReading = (int) externalVoltage;
  return rawReading;
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
// maximum value, we have to clamp it as the final step
// ******************************************************************
int getDimmingFromLDR() {
  int rawSensorVal = 1023-analogRead(LDRPin);
  double sensorDiff = rawSensorVal - sensorSmoothed;
  sensorSmoothed += (sensorDiff/sensorSmoothCount);
  
  double sensorSmoothedResult = sensorSmoothed - dimDark;
  if (sensorSmoothedResult < dimDark) sensorSmoothedResult = dimDark;
  if (sensorSmoothedResult > dimBright) sensorSmoothedResult = dimBright;
  sensorSmoothedResult = (sensorSmoothedResult-dimDark)*sensorFactor;
  
  int returnValue = sensorSmoothedResult;
  
  if (returnValue < DIGIT_DISPLAY_MIN_DIM) returnValue = DIGIT_DISPLAY_MIN_DIM;
  if (returnValue > DIGIT_DISPLAY_OFF) returnValue = DIGIT_DISPLAY_OFF;
  
  return returnValue;
}

// ******************************************************************
// Work out the day of week.
// Used in day blanking 
// 1 <= m <= 12,  y > 1752 (in the U.K.)
// ******************************************************************
int dayofweek(int y, int m, int d)
{
  static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  y -= m < 3;
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}
