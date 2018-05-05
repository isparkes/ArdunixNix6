//**********************************************************************************
//* Main code for an Arduino based Nixie clock. Features:                          *
//*  - Real Time Clock interface for DS3231                                        *
//*  - WiFi Clock interface for the WiFiModule                                     *
//*  - Digit fading with configurable fade length                                  *
//*  - Digit scrollback with configurable scroll speed                             *
//*  - Configuration stored in EEPROM                                              *
//*  - Low hardware component count (as much as possible done in code)             *
//*  - Single button operation with software debounce                              *
//*  - Single 74141 for digit display (other versions use 2 or even 6!)            *
//*  - Automatic dimming, using a Light Dependent Resistor                         *
//*  - RGB back light management                                                   *
//*                                                                                *
//*  nixie@protonmail.ch                                                           *
//*                                                                                *
//**********************************************************************************
//**********************************************************************************
#include <avr/io.h>
#include <EEPROM.h>
#include <DS3231.h>
#include <Wire.h>
#include <TimeLib.h>

//**********************************************************************************
//**********************************************************************************
//*                               Constants                                        *
//**********************************************************************************
//**********************************************************************************
#define EE_12_24            1      // 12 or 24 hour mode
#define EE_FADE_STEPS       2      // How quickly we fade, higher = slower
#define EE_DATE_FORMAT      3      // Date format to display
#define EE_DAY_BLANKING     4      // Blanking setting
#define EE_DIM_DARK_LO      5      // Dimming dark value
#define EE_DIM_DARK_HI      6      // Dimming dark value
#define EE_BLANK_LEAD_ZERO  7      // If we blank leading zero on hours
#define EE_DIGIT_COUNT_HI   8      // The number of times we go round the main loop
#define EE_DIGIT_COUNT_LO   9      // The number of times we go round the main loop
#define EE_SCROLLBACK       10     // if we use scollback or not
#define EE_FADE             11     // if we use fade or not
#define EE_PULSE_LO         12     // The pulse on width for the PWM mode
#define EE_PULSE_HI         13     // The pulse on width for the PWM mode
#define EE_SCROLL_STEPS     14     // The steps in a scrollback
#define EE_BACKLIGHT_MODE   15     // The back light node
#define EE_DIM_BRIGHT_LO    16     // Dimming bright value
#define EE_DIM_BRIGHT_HI    17     // Dimming bright value
#define EE_DIM_SMOOTH_SPEED 18     // Dimming adaptation speed
#define EE_RED_INTENSITY    19     // Red channel backlight max intensity
#define EE_GRN_INTENSITY    20     // Green channel backlight max intensity
#define EE_BLU_INTENSITY    21     // Blue channel backlight max intensity
#define EE_HV_VOLTAGE       22     // The HV voltage we want to use
#define EE_SUPPRESS_ACP     23     // Do we want to suppress ACP during dimmed time
#define EE_HOUR_BLANK_START 24     // Start of daily blanking period
#define EE_HOUR_BLANK_END   25     // End of daily blanking period
#define EE_CYCLE_SPEED      26     // How fast the color cycling does it's stuff
#define EE_PWM_TOP_LO       27     // The PWM top value if we know it, 0xFF if we need to calculate
#define EE_PWM_TOP_HI       28     // The PWM top value if we know it, 0xFF if we need to calculate
#define EE_HVG_NEED_CALIB   29     // 1 if we need to calibrate the HVGenerator, otherwise 0
#define EE_MIN_DIM_LO       30     // The min dim value
#define EE_MIN_DIM_HI       31     // The min dim value
#define EE_ANTI_GHOST       32     // The value we use for anti-ghosting
#define EE_NEED_SETUP       33     // used for detecting auto config for startup. By default the flashed, empty EEPROM shows us we need to do a setup 
#define EE_USE_LDR          34     // if we use the LDR or not (if we don't use the LDR, it has 100% brightness
#define EE_BLANK_MODE       35     // blank tubes, or LEDs or both
#define EE_SLOTS_MODE       36     // Show date every now and again

// Software version shown in config menu
#define SOFTWARE_VERSION      51

// Display handling
#define DIGIT_DISPLAY_COUNT   1000 // The number of times to traverse inner fade loop per digit
#define DIGIT_DISPLAY_ON      0    // Switch on the digit at the beginning by default
#define DIGIT_DISPLAY_OFF     999  // Switch off the digit at the end by default
#define DIGIT_DISPLAY_NEVER   -1   // When we don't want to switch on or off (i.e. blanking)
#define DISPLAY_COUNT_MAX     2000 // Maximum value we can set to
#define DISPLAY_COUNT_MIN     500  // Minimum value we can set to

#define MIN_DIM_DEFAULT       100  // The default minimum dim count
#define MIN_DIM_MIN           100  // The minimum dim count
#define MIN_DIM_MAX           500  // The maximum dim count

#define SENSOR_LOW_MIN        0
#define SENSOR_LOW_MAX        900
#define SENSOR_LOW_DEFAULT    100  // Dark
#define SENSOR_HIGH_MIN       0
#define SENSOR_HIGH_MAX       900
#define SENSOR_HIGH_DEFAULT   700 // Bright

#define SENSOR_SMOOTH_READINGS_MIN     1
#define SENSOR_SMOOTH_READINGS_MAX     255
#define SENSOR_SMOOTH_READINGS_DEFAULT 100  // Speed at which the brighness adapts to changes

#define BLINK_COUNT_MAX                25   // The number of impressions between blink state toggle

// The target voltage we want to achieve
#define HVGEN_TARGET_VOLTAGE_DEFAULT 180
#define HVGEN_TARGET_VOLTAGE_MIN     150
#define HVGEN_TARGET_VOLTAGE_MAX     200

// The PWM parameters
#define PWM_TOP_DEFAULT   1000
#define PWM_TOP_MIN       300
#define PWM_TOP_MAX       10000
#define PWM_PULSE_DEFAULT 200
#define PWM_PULSE_MIN     50
#define PWM_PULSE_MAX     500
#define PWM_OFF_MIN       50

// How quickly the scroll works
#define SCROLL_STEPS_DEFAULT 4
#define SCROLL_STEPS_MIN     1
#define SCROLL_STEPS_MAX     40

// The number of dispay impessions we need to fade by default
// 100 is about 1 second
#define FADE_STEPS_DEFAULT 50
#define FADE_STEPS_MAX     200
#define FADE_STEPS_MIN     20

// Display mode, set per digit
#define BLANKED  0
#define DIMMED   1
#define FADE     2
#define NORMAL   3
#define BLINK    4
#define SCROLL   5
#define BRIGHT   6

#define SECS_MAX  60
#define MINS_MAX  60
#define HOURS_MAX 24

#define COLOUR_CNL_MAX                  15
#define COLOUR_RED_CNL_DEFAULT          15
#define COLOUR_GRN_CNL_DEFAULT          0
#define COLOUR_BLU_CNL_DEFAULT          0
#define COLOUR_CNL_MIN                  0

// Clock modes - normal running is MODE_TIME, other modes accessed by a middle length ( 1S < press < 2S ) button press
#define MODE_MIN                        0
#define MODE_TIME                       0

// Time setting, need all six digits, so no flashing mode indicator
#define MODE_HOURS_SET                  1
#define MODE_MINS_SET                   2
#define MODE_SECS_SET                   3
#define MODE_DAYS_SET                   4
#define MODE_MONTHS_SET                 5
#define MODE_YEARS_SET                  6

// Basic settings
#define MODE_12_24                      7  // Mode "00" 0 = 24, 1 = 12
#define HOUR_MODE_DEFAULT               false
#define MODE_LEAD_BLANK                 8  // Mode "01" 1 = blanked
#define LEAD_BLANK_DEFAULT              false
#define MODE_SCROLLBACK                 9  // Mode "02" 1 = use scrollback
#define SCROLLBACK_DEFAULT              false
#define MODE_FADE                       10 // Mode "03" 1 = use fade
#define FADE_DEFAULT                    false
#define MODE_DATE_FORMAT                11 // Mode "04"
#define MODE_DAY_BLANKING               12 // Mode "05"
#define MODE_HR_BLNK_START              13 // Mode "06" - skipped if not using hour blanking
#define MODE_HR_BLNK_END                14 // Mode "07" - skipped if not using hour blanking
#define MODE_SUPPRESS_ACP               15 // Mode "08" 1 = suppress ACP when fully dimmed
#define SUPPRESS_ACP_DEFAULT            true
#define MODE_USE_LDR                    16 // Mode "09" 1 = use LDR, 0 = don't (and have 100% brightness)
#define MODE_USE_LDR_DEFAULT            true
#define MODE_BLANK_MODE                 17 // Mode "10" 
#define MODE_BLANK_MODE_DEFAULT         BLANK_MODE_BOTH

// Display tricks
#define MODE_FADE_STEPS_UP              18 // Mode "11"
#define MODE_FADE_STEPS_DOWN            19 // Mode "12"
#define MODE_DISPLAY_SCROLL_STEPS_UP    20 // Mode "13"
#define MODE_DISPLAY_SCROLL_STEPS_DOWN  21 // Mode "14"
#define MODE_SLOTS_MODE                 22 // Mode "15"

// Back light
#define MODE_BACKLIGHT_MODE             23 // Mode "16"
#define MODE_RED_CNL                    24 // Mode "17"
#define MODE_GRN_CNL                    25 // Mode "18"
#define MODE_BLU_CNL                    26 // Mode "19"
#define MODE_CYCLE_SPEED                27 // Mode "20" - speed the colour cycle cyles at

// HV generation
#define MODE_TARGET_HV_UP               28 // Mode "21"
#define MODE_TARGET_HV_DOWN             29 // Mode "22"
#define MODE_PULSE_UP                   30 // Mode "23"
#define MODE_PULSE_DOWN                 31 // Mode "24"

#define MODE_MIN_DIM_UP                 32 // Mode "25"
#define MODE_MIN_DIM_DOWN               33 // Mode "26"

#define MODE_ANTI_GHOST_UP              34 // Mode "27"
#define MODE_ANTI_GHOST_DOWN            35 // Mode "28"

// Temperature
#define MODE_TEMP                       36 // Mode "29"

// Software Version
#define MODE_VERSION                    37 // Mode "30"

// Tube test - all six digits, so no flashing mode indicator
#define MODE_TUBE_TEST                  38

#define MODE_MAX                        38

// Pseudo mode - burn the tubes and nothing else
#define MODE_DIGIT_BURN                 99 // Digit burn mode - accesible by super long press

// Temporary display modes - accessed by a short press ( < 1S ) on the button when in MODE_TIME
#define TEMP_MODE_MIN                   0
#define TEMP_MODE_DATE                  0 // Display the date for 5 S
#define TEMP_MODE_TEMP                  1 // Display the temperature for 5 S
#define TEMP_MODE_LDR                   2 // Display the normalised LDR reading for 5S, returns a value from 100 (dark) to 999 (bright)
#define TEMP_MODE_VERSION               3 // Display the version for 5S
#define TEMP_IP_ADDR12                  4 // IP xxx.yyy.zzz.aaa: xxx.yyy
#define TEMP_IP_ADDR34                  5 // IP xxx.yyy.zzz.aaa: zzz.aaa
#define TEMP_IMPR                       6 // number of impressions per second
#define TEMP_MODE_MAX                   6

#define DATE_FORMAT_MIN                 0
#define DATE_FORMAT_YYMMDD              0
#define DATE_FORMAT_MMDDYY              1
#define DATE_FORMAT_DDMMYY              2
#define DATE_FORMAT_MAX                 2
#define DATE_FORMAT_DEFAULT             2

#define DAY_BLANKING_MIN                0
#define DAY_BLANKING_NEVER              0  // Don't blank ever (default)
#define DAY_BLANKING_WEEKEND            1  // Blank during the weekend
#define DAY_BLANKING_WEEKDAY            2  // Blank during weekdays
#define DAY_BLANKING_ALWAYS             3  // Always blank
#define DAY_BLANKING_HOURS              4  // Blank between start and end hour every day
#define DAY_BLANKING_WEEKEND_OR_HOURS   5  // Blank between start and end hour during the week AND all day on the weekend
#define DAY_BLANKING_WEEKDAY_OR_HOURS   6  // Blank between start and end hour during the weekends AND all day on week days
#define DAY_BLANKING_WEEKEND_AND_HOURS  7  // Blank between start and end hour during the weekend
#define DAY_BLANKING_WEEKDAY_AND_HOURS  8  // Blank between start and end hour during week days
#define DAY_BLANKING_MAX                8
#define DAY_BLANKING_DEFAULT            0

#define BLANK_MODE_MIN                  0
#define BLANK_MODE_TUBES                0  // Use blanking for tubes only 
#define BLANK_MODE_LEDS                 1  // Use blanking for LEDs only
#define BLANK_MODE_BOTH                 2  // Use blanking for tubes and LEDs
#define BLANK_MODE_MAX                  2
#define BLANK_MODE_DEFAULT              2

#define BACKLIGHT_MIN                   0
#define BACKLIGHT_FIXED                 0   // Just define a colour and stick to it
#define BACKLIGHT_PULSE                 1   // pulse the defined colour
#define BACKLIGHT_CYCLE                 2   // cycle through random colours
#define BACKLIGHT_FIXED_DIM             3   // A defined colour, but dims with bulb dimming
#define BACKLIGHT_PULSE_DIM             4   // pulse the defined colour, dims with bulb dimming
#define BACKLIGHT_CYCLE_DIM             5   // cycle through random colours, dims with bulb dimming
#define BACKLIGHT_MAX                   5
#define BACKLIGHT_DEFAULT               0

#define CYCLE_SPEED_MIN                 4
#define CYCLE_SPEED_MAX                 64
#define CYCLE_SPEED_DEFAULT             10

#define ANTI_GHOST_MIN                  0
#define ANTI_GHOST_MAX                  50
#define ANTI_GHOST_DEFAULT              0

#define TEMP_DISPLAY_MODE_DUR_MS        5000

#define USE_LDR_DEFAULT                 true

// Limit on the length of time we stay in test mode
#define TEST_MODE_MAX_MS                60000

#define SLOTS_MODE_MIN                  0
#define SLOTS_MODE_NONE                 0   // Don't use slots effect
#define SLOTS_MODE_1M_SCR_SCR           1   // use slots effect every minute, scroll in, scramble out
#define SLOTS_MODE_MAX                  1
#define SLOTS_MODE_DEFAULT              1

// RTC address
#define RTC_I2C_ADDRESS                0x68

// I2C Slave Interface definition
#define I2C_SLAVE_ADDR                 0x69
#define I2C_TIME_UPDATE                0x00
#define I2C_GET_OPTIONS                0x01
#define I2C_SET_OPTION_12_24           0x02
#define I2C_SET_OPTION_BLANK_LEAD      0x03
#define I2C_SET_OPTION_SCROLLBACK      0x04
#define I2C_SET_OPTION_SUPPRESS_ACP    0x05
#define I2C_SET_OPTION_DATE_FORMAT     0x06
#define I2C_SET_OPTION_DAY_BLANKING    0x07
#define I2C_SET_OPTION_BLANK_START     0x08
#define I2C_SET_OPTION_BLANK_END       0x09
#define I2C_SET_OPTION_FADE_STEPS      0x0a
#define I2C_SET_OPTION_SCROLL_STEPS    0x0b
#define I2C_SET_OPTION_BACKLIGHT_MODE  0x0c
#define I2C_SET_OPTION_RED_CHANNEL     0x0d
#define I2C_SET_OPTION_GREEN_CHANNEL   0x0e
#define I2C_SET_OPTION_BLUE_CHANNEL    0x0f
#define I2C_SET_OPTION_CYCLE_SPEED     0x10
#define I2C_SHOW_IP_ADDR               0x11
#define I2C_SET_OPTION_FADE            0x12
#define I2C_SET_OPTION_USE_LDR         0x13
#define I2C_SET_OPTION_BLANK_MODE      0x14
#define I2C_SET_OPTION_SLOTS_MODE      0x15

#define MAX_WIFI_TIME                  5

//**********************************************************************************
//**********************************************************************************
//*                               Variables                                        *
//**********************************************************************************
//**********************************************************************************

// ***** Pin Defintions ****** Pin Defintions ****** Pin Defintions ******

// SN74141/K155ID1
// These are now managed directly on PORT B, we don't use digitalWrite() for these
#define ledPin_0_a  13    // package pin 19 // PB5
#define ledPin_0_b  10    // package pin 16 // PB2
#define ledPin_0_c  8     // package pin 14 // PB0
#define ledPin_0_d  12    // package pin 18 // PB4

// anode pins
#define ledPin_a_6  0     // low  - Secs  units // package pin 2  // PD0
#define ledPin_a_5  1     //      - Secs  tens  // package pin 3  // PD1
#define ledPin_a_4  2     //      - Mins  units // package pin 4  // PD2
#define ledPin_a_3  4     //      - Mins  tens  // package pin 6  // PD4
#define ledPin_a_2  A2    //      - Hours units // package pin 25 // PC2
#define ledPin_a_1  A3    // high - Hours tens  // package pin 26 // PC3

// button input
#define inputPin1   7     // package pin 13 // PD7

// PWM pin used to drive the DC-DC converter
#define hvDriverPin 9     // package pin 15 // PB1

// Tick led - PWM capable
#define tickLed     11    // package pin 17 // PB3

// PWM capable output for backlight
#define RLed        6     // package pin 12 // PD6
#define GLed        5     // package pin 11 // PD5
#define BLed        3     // package pin 5  // PD3

#define sensorPin   A0    // Package pin 23 // PC0 // Analog input pin for HV sense: HV divided through 390k and 4k7 divider, using 5V reference
#define LDRPin      A1    // Package pin 24 // PC1 // Analog input for Light dependent resistor.

//**********************************************************************************

/**
 * A class that displays a message by scrolling it into and out of the display
 *
 * Thanks judge!
 */
extern byte NumberArray[];
extern byte displayType[];
extern boolean scrollback;

struct Transition {
  /**
   * Default the effect and hold duration.
   */
  Transition() : Transition(500, 500, 3000) {}

  Transition(int effectDuration, int holdDuration) : Transition(effectDuration, effectDuration, holdDuration) {}

  Transition(int effectInDuration, int effectOutDuration, int holdDuration) {
    this->effectInDuration = effectInDuration;
    this->effectOutDuration = effectOutDuration;
    this->holdDuration = holdDuration;
    this->started = 0;
    this->end = 0;
  }

  void start(unsigned long now) {
    if (end < now) {
      this->started = now;
      this->end = getEnd();
      saveCurrentDisplayType(); 
    }
    // else we are already running!
  }

  boolean isMessageOnDisplay(unsigned long now)
  {
    return (now < end);
  }

  // we need to get the seconds updated, otherwise we show the old
  // time at the end of the stunt
  void updateRegularDisplaySeconds() {
    regularDisplay[5] = second() % 10;
    regularDisplay[4] = second() / 10;
  }

  boolean scrollMsg(unsigned long now)
  {
    if (now < end) {
      int msCount = now - started;
      if (msCount < effectInDuration) {
        loadRegularValues();
        // Scroll -1 -> -6
        scroll(-(msCount % effectInDuration) * 6 / effectInDuration - 1);
      } else if (msCount < effectInDuration * 2) {
        loadAlternateValues();
        // Scroll 5 -> 0
        scroll(5 - (msCount % effectInDuration) * 6 / effectInDuration);
      } else if (msCount < effectInDuration * 2 + holdDuration) {
        loadAlternateValues();
      } else if (msCount < effectInDuration * 2 + holdDuration + effectOutDuration) {
        loadAlternateValues();
        // Scroll 1 -> 6
        scroll(((msCount - holdDuration) % effectOutDuration) * 6 / effectOutDuration + 1);
      } else if (msCount < effectInDuration * 2 + holdDuration + effectOutDuration * 2) {
        loadRegularValues();
        // Scroll 0 -> -5
        scroll(((msCount - holdDuration) % effectOutDuration) * 6 / effectOutDuration - 5);
      }

      return true;  // we are still running
    }

    return false;   // We aren't running
  }

  boolean scrambleMsg(unsigned long now)
  {
    if (now < end) {
      int msCount = now - started;
      if (msCount < effectInDuration) {
        loadRegularValues();
        scramble(msCount, 5 - (msCount % effectInDuration) * 6 / effectInDuration, 6);
      } else if (msCount < effectInDuration * 2) {
        loadAlternateValues();
        scramble(msCount, 0, 5 - (msCount % effectInDuration) * 6 / effectInDuration);
      } else if (msCount < effectInDuration * 2 + holdDuration) {
        loadAlternateValues();
      } else if (msCount < effectInDuration * 2 + holdDuration + effectOutDuration) {
        loadAlternateValues();
        scramble(msCount, 0, ((msCount - holdDuration) % effectOutDuration) * 6 / effectOutDuration + 1);
      } else if (msCount < effectInDuration * 2 + holdDuration + effectOutDuration * 2) {
        loadRegularValues();
        scramble(msCount, ((msCount - holdDuration) % effectOutDuration) * 6 / effectOutDuration + 1, 6);
      }
      return true;  // we are still running
    }
    return false;   // We aren't running
  }

  boolean scrollInScrambleOut(unsigned long now)
  {
    if (now < end) {
      int msCount = now - started;
      if (msCount < effectInDuration) {
        loadRegularValues();
        scroll(-(msCount % effectInDuration) * 6 / effectInDuration - 1);
      } else if (msCount < effectInDuration * 2) {
        restoreCurrentDisplayType();
        loadAlternateValues();
        scroll(5 - (msCount % effectInDuration) * 6 / effectInDuration);
      } else if (msCount < effectInDuration * 2 + holdDuration) {
        loadAlternateValues();
      } else if (msCount < effectInDuration * 2 + holdDuration + effectOutDuration) {
        loadAlternateValues();
        scramble(msCount, 0, ((msCount - holdDuration) % effectOutDuration) * 6 / effectOutDuration + 1);
      } else if (msCount < effectInDuration * 2 + holdDuration + effectOutDuration * 2) {
        loadRegularValues();
        scramble(msCount, ((msCount - holdDuration) % effectOutDuration) * 6 / effectOutDuration + 1, 6);
      }
      return true;  // we are still running
    }
    return false;   // We aren't running
  }

  /**
   * +ve scroll right
   * -ve scroll left
   */
  static int scroll(char count) {
    byte copy[6] = {0, 0, 0, 0, 0, 0};
    memcpy(copy, NumberArray, sizeof(copy));
    char offset = 0;
    char slope = 1;
    if (count < 0) {
      count = -count;
      offset = 5;
      slope = -1;
    }
    for (char i=0; i<6; i++) {
      if (i < count) {
        displayType[offset + i * slope] = BLANKED;
      }
      if (i >= count) {
        NumberArray[offset + i * slope] = copy[offset + (i-count) * slope];
      }
    }

    return count;
  }

  static unsigned long hash(unsigned long x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
  }

  // In these functions we want something that changes quickly
  // hence msCount/20. Plus it needs to be different for different
  // indices, hence +i. Plus it needs to be 'random', hence hash function
  static int scramble(int msCount, byte start, byte end) {
    for (int i=start; i < end; i++) {
      NumberArray[i] = hash(msCount / 20 + i) % 10;
    }

    return start;
  }

  void setRegularValues() {
    memcpy(regularDisplay, NumberArray, sizeof(regularDisplay));    
  }
  
  void setAlternateValues() {
    memcpy(alternateDisplay, NumberArray, sizeof(alternateDisplay));    
  }

  void loadRegularValues() {
    memcpy(NumberArray, regularDisplay, sizeof(regularDisplay));    
  }

  void loadAlternateValues() {
    memcpy(NumberArray, alternateDisplay, sizeof(alternateDisplay));    
  }
  
  void saveCurrentDisplayType() {
    memcpy(savedDisplayType, displayType, sizeof(savedDisplayType));  
    savedScrollback = scrollback;
    scrollback = false;
  }
  
  void restoreCurrentDisplayType() {
    memcpy(displayType, savedDisplayType, sizeof(savedDisplayType));
    scrollback = savedScrollback;
  }
  
protected:
  /**** Don't let Joe Public see this stuff ****/

  int effectInDuration;  // How long an effect should take in ms
  int effectOutDuration; // How long an effect should take in ms
  int holdDuration;      // How long the message should be displayed for in ms
  unsigned long started; // When we were started (timestamp)
  unsigned long end;     // When the whole thing will end (timestamp)

  /**
   * The end time has to match what displayMessage() wants,
   * so let sub-classes override it.
   */
  unsigned long getEnd() {
    return started + effectInDuration * 2 + holdDuration + effectOutDuration * 2;
  }

  // buffer variables for doing the scrolling/scrambling with
  byte regularDisplay[6] = {0, 0, 0, 0, 0, 0};
  byte alternateDisplay[6] = {0, 0, 0, 0, 0, 0};
  boolean savedScrollback;
  byte savedDisplayType[6] = {FADE, FADE, FADE, FADE, FADE, FADE};
};

Transition transition(500, 1000, 3000);

//**********************************************************************************

// Structure for encapsulating button debounce and management
struct Button {
  public:
    // Constructor
    Button(byte newInputPin, boolean newActiveLow) : inputPin(0), activeLow(false) {
      inputPin = newInputPin;
      activeLow = newActiveLow;
    }

    // ************************************************************
    // MAIN BUTTON CHECK ENTRY POINT - should be called periodically
    // See if the button was pressed and debounce. We perform a
    // sort of preview here, then confirm by releasing. We track
    // 3 lengths of button press: momentarily, 1S and 2S.
    // ************************************************************
    void checkButton(unsigned long nowMillis) {
      checkButtonInternal(nowMillis);
    }

    // ************************************************************
    // Reset everything
    // ************************************************************
    void reset() {
      resetInternal();
    }

    // ************************************************************
    // Check if button is pressed right now (just debounce)
    // ************************************************************
    boolean isButtonPressedNow() {
      return button1PressedCount == debounceCounter;
    }

    // ************************************************************
    // Check if button is pressed momentarily
    // ************************************************************
    boolean isButtonPressed() {
      return buttonPress;
    }

    // ************************************************************
    // Check if button is pressed for a long time (> 1S)
    // ************************************************************
    boolean isButtonPressed1S() {
      return buttonPress1S;
    }

    // ************************************************************
    // Check if button is pressed for a moderately long time (> 2S)
    // ************************************************************
    boolean isButtonPressed2S() {
      return buttonPress2S;
    }

    // ************************************************************
    // Check if button is pressed for a very long time (> 8S)
    // ************************************************************
    boolean isButtonPressed8S() {
      return buttonPress8S;
    }

    // ************************************************************
    // Check if button is pressed for a short time (> 200mS) and released
    // ************************************************************
    boolean isButtonPressedAndReleased() {
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
    boolean isButtonPressedReleased1S() {
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
    boolean isButtonPressedReleased2S() {
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
    boolean isButtonPressedReleased8S() {
      if (buttonPressRelease8S) {
        buttonPressRelease8S = false;
        return true;
      } else {
        return false;
      }
    }

  private:
    byte inputPin;
    boolean activeLow;

    int  button1PressedCount = 0;
    unsigned long button1PressStartMillis = 0;
    const byte debounceCounter = 5; // Number of successive reads before we say the switch is down
    boolean buttonWasReleased = false;
    boolean buttonPress8S = false;
    boolean buttonPress2S = false;
    boolean buttonPress1S = false;
    boolean buttonPress = false;
    boolean buttonPressRelease8S = false;
    boolean buttonPressRelease2S = false;
    boolean buttonPressRelease1S = false;
    boolean buttonPressRelease = false;

    void checkButtonInternal(unsigned long nowMillis) {
      if (digitalRead(inputPin) == 0) {
        buttonWasReleased = false;

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

    void resetInternal() {
      buttonPressRelease8S = false;
      buttonPressRelease2S = false;
      buttonPressRelease1S = false;
      buttonPressRelease = false;
      buttonPress8S = false;
      buttonPress2S = false;
      buttonPress1S = false;
      buttonPress = false;
      button1PressedCount = 0;
    }
};

// ********************** HV generator variables *********************
int hvTargetVoltage = HVGEN_TARGET_VOLTAGE_DEFAULT;
int pwmTop = PWM_TOP_DEFAULT;
int pwmOn = PWM_PULSE_DEFAULT;

// All-In-One Rev1 has a mix up in the tube wiring. All other clocks are 
// correct.
#define NOT_AIO_REV1 // [AIO_REV1,NOT_AIO_REV1]

// Used for special mappings of the 74141 -> digit (wiring aid)
// allows the board wiring to be much simpler
#ifdef AIO_REV1 
  // This is a mapping for All-In-One Revision 1 ONLY! Not generally used.
  byte decodeDigit[16] = {3,2,8,9,0,1,5,4,6,7,10,10,10,10,10,10};
#else
  byte decodeDigit[16] = {2,3,7,6,4,5,1,0,9,8,10,10,10,10,10,10};
#endif

// Driver pins for the anodes
byte anodePins[6] = {ledPin_a_1, ledPin_a_2, ledPin_a_3, ledPin_a_4, ledPin_a_5, ledPin_a_6};

// precalculated values for turning on and off the HV generator
// Put these in TCCR1B to turn off and on
byte tccrOff;
byte tccrOn;
int rawHVADCThreshold;
double sensorHVSmoothed = 0;

// ************************ Display management ************************
byte NumberArray[6]    = {0, 0, 0, 0, 0, 0};
byte currNumberArray[6] = {0, 0, 0, 0, 0, 0};
byte displayType[6]    = {FADE, FADE, FADE, FADE, FADE, FADE};
byte fadeState[6]      = {0, 0, 0, 0, 0, 0};
byte ourIP[4]          = {0, 0, 0, 0}; // set by the WiFi module

// how many fade steps to increment (out of DIGIT_DISPLAY_COUNT) each impression
// 100 is about 1 second
int fadeSteps = FADE_STEPS_DEFAULT;
int digitOffCount = DIGIT_DISPLAY_OFF;
int scrollSteps = SCROLL_STEPS_DEFAULT;
boolean scrollback = true;
boolean fade = true;
byte antiGhost = ANTI_GHOST_DEFAULT;
int dispCount = DIGIT_DISPLAY_COUNT + antiGhost;
float fadeStep = DIGIT_DISPLAY_COUNT / fadeSteps;

// For software blinking
int blinkCounter = 0;
boolean blinkState = true;

// leading digit blanking
boolean blankLeading = false;

// Dimming value
const int DIM_VALUE = DIGIT_DISPLAY_COUNT / 5;
int minDim = MIN_DIM_DEFAULT;

unsigned int tempDisplayModeDuration;      // time for the end of the temporary display
int  tempDisplayMode;

int acpOffset = 0;        // Used to provide one arm bandit scolling
int acpTick = 0;          // The number of counts before we scroll
boolean suppressACP = false;
byte slotsMode = SLOTS_MODE_DEFAULT;

byte currentMode = MODE_TIME;   // Initial cold start mode
byte nextMode = currentMode;
byte blankHourStart = 0;
byte blankHourEnd = 0;
byte blankMode = 0;
boolean blankTubes = false;
boolean blankLEDs = false;

// ************************ Ambient light dimming ************************
int dimDark = SENSOR_LOW_DEFAULT;
int dimBright = SENSOR_HIGH_DEFAULT;
double sensorLDRSmoothed = 0;
double sensorFactor = (double)(DIGIT_DISPLAY_OFF) / (double)(dimBright - dimDark);
int sensorSmoothCountLDR = SENSOR_SMOOTH_READINGS_DEFAULT;
int sensorSmoothCountHV = SENSOR_SMOOTH_READINGS_DEFAULT / 8;
boolean useLDR = true;

// ************************ Clock variables ************************
// RTC, uses Analogue pins A4 (SDA) and A5 (SCL)
DS3231 Clock;

// State variables for detecting changes
byte lastSec;
unsigned long nowMillis = 0;
unsigned long lastCheckMillis = 0;

byte dateFormat = DATE_FORMAT_DEFAULT;
byte dayBlanking = DAY_BLANKING_DEFAULT;
boolean blanked = false;
byte blankSuppressStep = 0;    // The press we are on: 1 press = suppress for 1 min, 2 press = 1 hour, 3 = 1 day
unsigned long blankSuppressedMillis = 0;   // The end time of the blanking, 0 if we are not suppressed
unsigned long blankSuppressedSelectionTimoutMillis = 0;   // Used for determining the end of the blanking period selection timeout
boolean hourMode = false;
boolean triggeredThisSec = false;

byte useRTC = false;  // true if we detect an RTC
byte useWiFi = 0; // the number of minutes ago we recevied information from the WiFi module, 0 = don't use WiFi


// **************************** LED management ***************************
boolean upOrDown;

// Blinking colons led in settings modes
int ledBlinkCtr = 0;
int ledBlinkNumber = 0;

byte backlightMode = BACKLIGHT_DEFAULT;

// Back light intensities
byte redCnl = COLOUR_RED_CNL_DEFAULT;
byte grnCnl = COLOUR_GRN_CNL_DEFAULT;
byte bluCnl = COLOUR_BLU_CNL_DEFAULT;
byte cycleCount = 0;
byte cycleSpeed = CYCLE_SPEED_DEFAULT;

// Back light cycling
int colors[3];
int changeSteps = 0;
byte currentColour = 0;

int impressionsPerSec = 0;
int lastImpressionsPerSec = 0;

// ********************** Input switch management **********************
Button button1(inputPin1, false);

// **************************** digit healing ****************************
// This is a special mode which repairs cathode poisoning by driving a
// single element at full power. To be used with care!
// In theory, this should not be necessary because we have ACP every 10mins,
// but some tubes just want to be poisoned
byte digitBurnDigit = 0;
byte digitBurnValue = 0;

// ************************************************************
// LED brightness correction: The perceived brightness is not linear
// ************************************************************
const byte dim_curve[] = {
  0,   1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,
  3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,   4,   4,   4,
  4,   4,   4,   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,   6,   6,   6,
  6,   6,   6,   6,   6,   7,   7,   7,   7,   7,   7,   7,   8,   8,   8,   8,
  8,   8,   9,   9,   9,   9,   9,   9,   10,  10,  10,  10,  10,  11,  11,  11,
  11,  11,  12,  12,  12,  12,  12,  13,  13,  13,  13,  14,  14,  14,  14,  15,
  15,  15,  16,  16,  16,  16,  17,  17,  17,  18,  18,  18,  19,  19,  19,  20,
  20,  20,  21,  21,  22,  22,  22,  23,  23,  24,  24,  25,  25,  25,  26,  26,
  27,  27,  28,  28,  29,  29,  30,  30,  31,  32,  32,  33,  33,  34,  35,  35,
  36,  36,  37,  38,  38,  39,  40,  40,  41,  42,  43,  43,  44,  45,  46,  47,
  48,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,
  63,  64,  65,  66,  68,  69,  70,  71,  73,  74,  75,  76,  78,  79,  81,  82,
  83,  85,  86,  88,  90,  91,  93,  94,  96,  98,  99,  101, 103, 105, 107, 109,
  110, 112, 114, 116, 118, 121, 123, 125, 127, 129, 132, 134, 136, 139, 141, 144,
  146, 149, 151, 154, 157, 159, 162, 165, 168, 171, 174, 177, 180, 183, 186, 190,
  193, 196, 200, 203, 207, 211, 214, 218, 222, 226, 230, 234, 238, 242, 248, 255,
};

const byte rgb_backlight_curve[] = {0, 16, 32, 48, 64, 80, 99, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255};


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

  // The LEDS sometimes glow at startup, it annoys me, so turn them completely off
  analogWrite(tickLed, 0);
  analogWrite(RLed, 0);
  analogWrite(GLed, 0);
  analogWrite(BLed, 0);

  // NOTE:
  // Grounding the input pin causes it to actuate
  pinMode(inputPin1, INPUT ); // set the input pin 1
  digitalWrite(inputPin1, HIGH); // set pin 1 as a pull up resistor.

  // Set the driver pin to putput
  pinMode(hvDriverPin, OUTPUT);

  /* disable global interrupts while we set up them up */
  cli();

  // **************************** HV generator ****************************

  TCCR1A = 0;    // disable all PWM on Timer1 whilst we set it up
  TCCR1B = 0;    // disable all PWM on Timer1 whilst we set it up

  // Configure timer 1 for Fast PWM mode via ICR1, with prescaling=1
  TCCR1A = (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);

  tccrOff = TCCR1A;

  TCCR1A |= (1 <<  COM1A1);  // enable PWM on port PD4 in non-inverted compare mode 2

  tccrOn = TCCR1A;

  // Set up timer 2 like timer 0 (for RGB leds)
  TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
  TCCR2B = (1 << CS22);

  // we don't need the HV yet, so turn it off
  TCCR1A = tccrOff;

  /* enable global interrupts */
  sei();

  // **********************************************************************

  // Test if the button is pressed for factory reset
  for (int i = 0 ; i < 20 ; i++ ) {
    button1.checkButton(nowMillis);
  }

  // Set up the PRNG with something so that it looks random
  randomSeed(analogRead(LDRPin));

  // Test if the button is pressed for factory reset
  for (int i = 0 ; i < 20 ; i++ ) {
    button1.checkButton(nowMillis);
  }

  // User requested factory reset
  if (button1.isButtonPressedNow()) {
    // do this before the flashing, because this way we can set up the EEPROM to
    // autocalibrate on first start (press factory reset and then power off before
    // flashing ends)
    EEPROM.write(EE_HVG_NEED_CALIB, true);

    // Flash some ramdom c0lours to signal that we have accepted the factory reset
    for (int i = 0 ; i < 60 ; i++ ) {
      randomRGBFlash(20);
    }

    // mark that we have done the EEPROM setup
    EEPROM.write(EE_NEED_SETUP, true);
  }
  
  // If the button is held down while we are flashing, then do the test pattern
  boolean doTestPattern = false;

  // Detect factory reset: button pressed on start or uninitialised EEPROM
  if (EEPROM.read(EE_NEED_SETUP)) {
    doTestPattern = true;
  }

  // Clear down any spurious button action
  button1.reset();

  // Test if the button is pressed for factory reset
  for (int i = 0 ; i < 20 ; i++ ) {
    button1.checkButton(nowMillis);
  }

  if (button1.isButtonPressedNow()) {
    doTestPattern = true;
  }

  // Read EEPROM values
  readEEPROMValues();

  // set our PWM profile
  setPWMOnTime(pwmOn);
  setPWMTopTime(pwmTop);

  // Set the target voltage
  rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);

  // HV GOOOO!!!!
  TCCR1A = tccrOn;

  if (doTestPattern) {
    boolean oldUseLDR = useLDR;
    
    // reset the EEPROM values
    factoryReset();
    
    // turn off LDR
    useLDR = false;

    // turn off Scrollback
    scrollback = false;

    // All the digits on full
    allBright();

    int secCount = 0;
    lastCheckMillis = millis();

    boolean inLoop = true;

    // We don't want to stay in test mode forever
    long startTestMode = lastCheckMillis;

    while (inLoop) {
      nowMillis = millis();
      if (nowMillis - lastCheckMillis > 1000) {
        lastCheckMillis = nowMillis;
        secCount++;
        secCount = secCount % 10;
      }

      // turn off test mode
      blankTubes = (nowMillis - startTestMode > TEST_MODE_MAX_MS);

      loadNumberArraySameValue(secCount);
      outputDisplay();
      checkHVVoltage();

      setLedsTestPattern(nowMillis);
      button1.checkButton(nowMillis);

      if (button1.isButtonPressedNow() && (secCount == 8)) {
        inLoop = false;
        blankTubes = false;
      }
    }

    useLDR = oldUseLDR;
  }

  // reset the LEDs
  setLedsTestPattern(0);

  // Set up the HVG if we need to
  if (EEPROM.read(EE_HVG_NEED_CALIB)) {
    calibrateHVG();

    // Save the PWM values
    EEPROM.write(EE_PULSE_LO, pwmOn % 256);
    EEPROM.write(EE_PULSE_HI, pwmOn / 256);
    EEPROM.write(EE_PWM_TOP_LO, pwmTop % 256);
    EEPROM.write(EE_PWM_TOP_HI, pwmTop / 256);

    // Mark that we don't need to do this next time
    EEPROM.write(EE_HVG_NEED_CALIB, false);
  }

  // and return it to target voltage so we can regulate the PWM on time
  rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);

  // Clear down any spurious button action
  button1.reset();

  // initialise the internal time (in case we don't find the time provider)
  nowMillis = millis();
  setTime(12, 34, 56, 1, 3, 2017);
  getRTCTime();

  // Show the version for 1 s
  tempDisplayMode = TEMP_MODE_VERSION;
  tempDisplayModeDuration = TEMP_DISPLAY_MODE_DUR_MS;

  // don't blank anything right now
  blanked = false;
  setTubesAndLEDSBlankMode();

  // mark that we have done the EEPROM setup
  EEPROM.write(EE_NEED_SETUP, false);
}

//**********************************************************************************
//**********************************************************************************
//*                              Main loop                                         *
//**********************************************************************************
//**********************************************************************************
boolean millisNotSet = true;
void loop()
{
  nowMillis = millis();

  // shows us how fast the inner loop is running
  impressionsPerSec++;

  // We don't want to get the time from the external time provider always,
  // just enough to keep the internal time provider correct
  // This keeps the outer loop fast and responsive
  // We do this once per minute
  if (abs(nowMillis - lastCheckMillis) >= 1000) {
    performOncePerSecondProcessing();

    // we only want to once per minute processing to be called once each minute 
    if ((second() == 0) && (!triggeredThisSec)) {
      performOncePerMinuteProcessing();
      triggeredThisSec = true;
    }

    if ((second() == 1) && triggeredThisSec) {
      triggeredThisSec = false;
    }

    lastCheckMillis = nowMillis;
  }

  // Check button, we evaluate below
  button1.checkButton(nowMillis);

  // ******* Preview the next display mode *******
  // What is previewed here will get actioned when
  // the button is released
  if (button1.isButtonPressed2S()) {
    // Just jump back to the start
    nextMode = MODE_MIN;
  } else if (button1.isButtonPressed1S()) {
    nextMode = currentMode + 1;

    if (nextMode > MODE_MAX) {
      nextMode = MODE_MIN;
    }
  }

  // ******* Set the display mode *******
  if (button1.isButtonPressedReleased8S()) {
    // 8 Sec press toggles burn mode
    if (currentMode == MODE_DIGIT_BURN) {
      currentMode = MODE_MIN;
    } else {
      currentMode = MODE_DIGIT_BURN;
    }

    nextMode = currentMode;
  } else if (button1.isButtonPressedReleased2S()) {
    currentMode = MODE_MIN;

    // Store the EEPROM if we exit the config mode
    saveEEPROMValues();

    // Preset the display
    allFadeOrNormal(false);

    nextMode = currentMode;
  } else if (button1.isButtonPressedReleased1S()) {
    currentMode++;

    if (currentMode > MODE_MAX) {
      currentMode = MODE_MIN;

      // Store the EEPROM if we exit the config mode
      saveEEPROMValues();

      // Preset the display
      allFadeOrNormal(false);
    }

    nextMode = currentMode;
  }

  // ************* Process the modes *************
  if (nextMode != currentMode) {
    setNextMode();
  } else {
    processCurrentMode();
  }

  // get the LDR ambient light reading
  digitOffCount = getDimmingFromLDR();
  fadeStep = digitOffCount / fadeSteps;

  // One armed bandit trigger every 10th minute
  if ((currentMode != MODE_DIGIT_BURN) && (nextMode != MODE_DIGIT_BURN)) {
    if (acpOffset == 0) {
      if (((minute() % 10) == 9) && (second() == 15)) {
        // suppress ACP when fully dimmed
        if (suppressACP) {
          if (digitOffCount > minDim) {
            acpOffset = 1;
          }
        } else {
          acpOffset = 1;
        }
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
    digitOn(digitBurnDigit, digitBurnValue);
  }

  // Slow regulation of the voltage
  checkHVVoltage();

  // Prepare the tick and backlight LEDs
  setLeds();
}

// ************************************************************
// Called once per second
// ************************************************************
void performOncePerSecondProcessing() {

  // Store the current value and reset
  lastImpressionsPerSec = impressionsPerSec;
  impressionsPerSec = 0;

  // Change the direction of the pulse
  upOrDown = !upOrDown;

  // If we are in temp display mode, decrement the count
  if (tempDisplayModeDuration > 0) {
    if (tempDisplayModeDuration > 1000) {
      tempDisplayModeDuration -= 1000;
    } else {
      tempDisplayModeDuration = 0;
    }
  }

  // decrement the blanking supression counter
  if (blankSuppressedMillis > 0) {
    if (blankSuppressedMillis > 1000) {
      blankSuppressedMillis -= 1000;
    } else {
      blankSuppressedMillis = 0;
    }
  }

  // Get the blanking status, this may be overridden by blanking suppression
  // Only blank if we are in TIME mode
  if (currentMode == MODE_TIME) {
    boolean nativeBlanked = checkBlanking();

    // Check if we are in blanking suppression mode
    blanked = nativeBlanked && (blankSuppressedMillis == 0);

    // reset the blanking period selection timer
    if (nowMillis > blankSuppressedSelectionTimoutMillis) {
      blankSuppressedSelectionTimoutMillis = 0;
      blankSuppressStep = 0;
    }
  } else {
    blanked = false;
  }
  setTubesAndLEDSBlankMode();
}

// ************************************************************
// Called once per minute
// ************************************************************
void performOncePerMinuteProcessing() {
  if (useWiFi > 0) {
    if (useWiFi == MAX_WIFI_TIME) {
      // We recently got an update, send to the RTC (if installed)
      setRTC();      
    }
    useWiFi--;
  } else {
    // get the time from the external RTC provider - (if installed)
    getRTCTime();
  }
}

// ************************************************************
// Set the seconds tick led(s) and the back lights
// ************************************************************
void setLeds()
{
  int secsDelta;
  if (upOrDown) {
    secsDelta = (nowMillis - lastCheckMillis);
  } else {
    secsDelta = 1000 - (nowMillis - lastCheckMillis);
  }

  // calculate the PWM factor, goes between minDim% and 100%
  float dimFactor = (float) digitOffCount / (float) DIGIT_DISPLAY_OFF;
  float pwmFactor = (float) secsDelta / (float) 1000.0;

  // Tick led output
  analogWrite(tickLed, getLEDAdjusted(255, pwmFactor, dimFactor));

  if (blankLEDs) {
    analogWrite(RLed, 0);
    analogWrite(GLed, 0);
    analogWrite(BLed, 0);
  } else {
    // RGB Backlight PWM led output
    if (currentMode == MODE_TIME) {
      switch (backlightMode) {
        case BACKLIGHT_FIXED:
          analogWrite(RLed, getLEDAdjusted(rgb_backlight_curve[redCnl], 1, 1));
          analogWrite(GLed, getLEDAdjusted(rgb_backlight_curve[grnCnl], 1, 1));
          analogWrite(BLed, getLEDAdjusted(rgb_backlight_curve[bluCnl], 1, 1));
          break;
        case BACKLIGHT_PULSE:
          analogWrite(RLed, getLEDAdjusted(rgb_backlight_curve[redCnl], pwmFactor, 1));
          analogWrite(GLed, getLEDAdjusted(rgb_backlight_curve[grnCnl], pwmFactor, 1));
          analogWrite(BLed, getLEDAdjusted(rgb_backlight_curve[bluCnl], pwmFactor, 1));
          break;
        case BACKLIGHT_CYCLE:
          cycleColours3(colors);
          analogWrite(RLed, getLEDAdjusted(colors[0], 1, 1));
          analogWrite(GLed, getLEDAdjusted(colors[1], 1, 1));
          analogWrite(BLed, getLEDAdjusted(colors[2], 1, 1));
          break;
        case BACKLIGHT_FIXED_DIM:
          analogWrite(RLed, getLEDAdjusted(rgb_backlight_curve[redCnl], 1, dimFactor));
          analogWrite(GLed, getLEDAdjusted(rgb_backlight_curve[grnCnl], 1, dimFactor));
          analogWrite(BLed, getLEDAdjusted(rgb_backlight_curve[bluCnl], 1, dimFactor));
          break;
        case BACKLIGHT_PULSE_DIM:
          analogWrite(RLed, getLEDAdjusted(rgb_backlight_curve[redCnl], pwmFactor, dimFactor));
          analogWrite(GLed, getLEDAdjusted(rgb_backlight_curve[grnCnl], pwmFactor, dimFactor));
          analogWrite(BLed, getLEDAdjusted(rgb_backlight_curve[bluCnl], pwmFactor, dimFactor));
          break;
        case BACKLIGHT_CYCLE_DIM:
          cycleColours3(colors);
          analogWrite(RLed, getLEDAdjusted(colors[0], 1, dimFactor));
          analogWrite(GLed, getLEDAdjusted(colors[1], 1, dimFactor));
          analogWrite(BLed, getLEDAdjusted(colors[2], 1, dimFactor));
          break;
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

      if ((ledBlinkNumber <= nextMode) && (ledBlinkNumber > 0)) {
        if (ledBlinkCtr < 3) {
          analogWrite(RLed, 255);
          analogWrite(GLed, 255);
          analogWrite(BLed, 255);
        } else {
          analogWrite(RLed, 0);
          analogWrite(GLed, 0);
          analogWrite(BLed, 0);
        }
      }
    }
  }
}

// ************************************************************
// Show the preview of the next mode - we stay in this mode until the 
// button is released
// ************************************************************
void setNextMode() {
  // turn off blanking
  blanked = false;
  setTubesAndLEDSBlankMode();

  switch (nextMode) {
    case MODE_TIME: {
        loadNumberArrayTime();
        allFadeOrNormal(true);
        break;
      }
    case MODE_HOURS_SET: {
        if (useWiFi > 0) {
          // skip past the time settings
          nextMode++;
          currentMode++;
          nextMode++;
          currentMode++;
          nextMode++;
          currentMode++;
        }
        loadNumberArrayTime();
        highlight0and1();
        break;
      }
    case MODE_MINS_SET: {
        loadNumberArrayTime();
        highlight2and3();
        break;
      }
    case MODE_SECS_SET: {
        loadNumberArrayTime();
        highlight4and5();
        break;
      }
    case MODE_DAYS_SET: {
        if (useWiFi > 0) {
          // skip past the time settings
          nextMode++;
          currentMode++;
          nextMode++;
          currentMode++;
          nextMode++;
          currentMode++;
        }
        loadNumberArrayDate();
        highlightDaysDateFormat();
        break;
      }
    case MODE_MONTHS_SET: {
        loadNumberArrayDate();
        highlightMonthsDateFormat();
        break;
      }
    case MODE_YEARS_SET: {
        loadNumberArrayDate();
        highlightYearsDateFormat();
        break;
      }
    case MODE_12_24: {
        loadNumberArrayConfBool(hourMode, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_LEAD_BLANK: {
        loadNumberArrayConfBool(blankLeading, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_SCROLLBACK: {
        loadNumberArrayConfBool(scrollback, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_FADE: {
        loadNumberArrayConfBool(fade, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_DATE_FORMAT: {
        loadNumberArrayConfInt(dateFormat, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_DAY_BLANKING: {
        loadNumberArrayConfInt(dayBlanking, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_HR_BLNK_START: {
        if (dayBlanking < DAY_BLANKING_HOURS) {
          // Skip past the start and end hour if the blanking mode says it is not relevant
          nextMode++;
          currentMode++;
          nextMode++;
          currentMode++;
        }

        loadNumberArrayConfInt(blankHourStart, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_HR_BLNK_END: {
        loadNumberArrayConfInt(blankHourEnd, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_SUPPRESS_ACP: {
        loadNumberArrayConfBool(suppressACP, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_BLANK_MODE: {
        loadNumberArrayConfInt(blankMode, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_USE_LDR: {
        loadNumberArrayConfBool(useLDR, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_FADE_STEPS_UP:
    case MODE_FADE_STEPS_DOWN: {
        loadNumberArrayConfInt(fadeSteps, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_DISPLAY_SCROLL_STEPS_UP:
    case MODE_DISPLAY_SCROLL_STEPS_DOWN: {
        loadNumberArrayConfInt(scrollSteps, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_SLOTS_MODE: {
        loadNumberArrayConfInt(slotsMode, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_BACKLIGHT_MODE: {
        loadNumberArrayConfInt(backlightMode, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_RED_CNL: {
        if ((backlightMode == BACKLIGHT_CYCLE) || (backlightMode == BACKLIGHT_CYCLE_DIM))  {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }
        loadNumberArrayConfInt(redCnl, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_GRN_CNL: {
        if ((backlightMode == BACKLIGHT_CYCLE) || (backlightMode == BACKLIGHT_CYCLE_DIM))  {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }
        loadNumberArrayConfInt(grnCnl, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_BLU_CNL: {
        if ((backlightMode == BACKLIGHT_CYCLE) || (backlightMode == BACKLIGHT_CYCLE_DIM))  {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }
        loadNumberArrayConfInt(bluCnl, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_CYCLE_SPEED: {
        if ((backlightMode != BACKLIGHT_CYCLE) && (backlightMode != BACKLIGHT_CYCLE_DIM))  {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }
        loadNumberArrayConfInt(cycleSpeed, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_TARGET_HV_UP:
    case MODE_TARGET_HV_DOWN: {
        loadNumberArrayConfInt(hvTargetVoltage, nextMode - MODE_12_24);
        displayConfig();
      }
    case MODE_PULSE_UP:
    case MODE_PULSE_DOWN: {
        loadNumberArrayConfInt(pwmOn, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_MIN_DIM_UP:
    case MODE_MIN_DIM_DOWN: {
        loadNumberArrayConfInt(minDim, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_ANTI_GHOST_UP:
    case MODE_ANTI_GHOST_DOWN: {
        loadNumberArrayConfInt(antiGhost, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_TEMP: {
        loadNumberArrayTemp(nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_VERSION: {
        loadNumberArrayConfInt(SOFTWARE_VERSION, nextMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_TUBE_TEST: {
        loadNumberArrayTestDigits();
        allNormal();
        break;
      }
    case MODE_DIGIT_BURN: {
        // Nothing: handled separately to suppress multiplexing
      }
  }
}

// ************************************************************
// Show the next mode - once the button is released
// ************************************************************
void processCurrentMode() {
  switch (currentMode) {
    case MODE_TIME: {
        if (button1.isButtonPressedAndReleased()) {
          // Deal with blanking first
          if ((nowMillis < blankSuppressedSelectionTimoutMillis) || blanked) {
            if (blankSuppressedSelectionTimoutMillis == 0) {
              // Apply 5 sec tineout for setting the suppression time
              blankSuppressedSelectionTimoutMillis = nowMillis + TEMP_DISPLAY_MODE_DUR_MS;
            }

            blankSuppressStep++;
            if (blankSuppressStep > 3) {
              blankSuppressStep = 3;
            }

            if (blankSuppressStep == 1) {
              blankSuppressedMillis = 10000;
            } else if (blankSuppressStep == 2) {
              blankSuppressedMillis = 3600000;
            } else if (blankSuppressStep == 3) {
              blankSuppressedMillis = 3600000 * 4;
            }
            blanked = false;
            setTubesAndLEDSBlankMode();
          } else {
            // Always start from the first mode, or increment the temp mode if we are already in a display
            if (tempDisplayModeDuration > 0) {
              tempDisplayModeDuration = TEMP_DISPLAY_MODE_DUR_MS;
              tempDisplayMode++;
            } else {
              tempDisplayMode = TEMP_MODE_MIN;
            }

            if (tempDisplayMode > TEMP_MODE_MAX) {
              tempDisplayMode = TEMP_MODE_MIN;
            }

            tempDisplayModeDuration = TEMP_DISPLAY_MODE_DUR_MS;
          }
        }

        if (tempDisplayModeDuration > 0) {
          blanked = false;
          setTubesAndLEDSBlankMode();
          if (tempDisplayMode == TEMP_MODE_DATE) {
            loadNumberArrayDate();
          }

          if (tempDisplayMode == TEMP_MODE_TEMP) {
            byte timeProv = 10;
            if (useRTC) timeProv += 1;
            if (useWiFi > 0) timeProv += 2;
            loadNumberArrayTemp(timeProv);
          }

          if (tempDisplayMode == TEMP_MODE_LDR) {
            loadNumberArrayLDR();
          }

          if (tempDisplayMode == TEMP_MODE_VERSION) {
            loadNumberArrayConfInt(SOFTWARE_VERSION, 0);
          }

          if (tempDisplayMode == TEMP_IP_ADDR12) {
            if (useWiFi > 0) {
              loadNumberArrayIP(ourIP[0], ourIP[1]);
            } else {
              // we can't show the IP address if we have the RTC, just skip
              tempDisplayMode++;
            }
          }

          if (tempDisplayMode == TEMP_IP_ADDR34) {
            if (useWiFi > 0) {
              loadNumberArrayIP(ourIP[2], ourIP[3]);
            } else {
              // we can't show the IP address if we have the RTC, just skip
              tempDisplayMode++;
            }
          }

          if (tempDisplayMode == TEMP_IMPR) {
            loadNumberArrayConfInt(lastImpressionsPerSec, 0);
          }

          allFadeOrNormal(false);

        } else {
          if (acpOffset > 0) {
            loadNumberArrayACP();
            allBright();
          } else {
            if (slotsMode > SLOTS_MODE_MIN) {
              if (second() == 50) {

                // initialise the slots values
                loadNumberArrayDate();
                transition.setAlternateValues();
                loadNumberArrayTime();
                transition.setRegularValues();
                allFadeOrNormal(false);

                transition.start(nowMillis);
              }

              boolean msgDisplaying;
              switch (slotsMode) {
                case SLOTS_MODE_1M_SCR_SCR:
                {
                  msgDisplaying = transition.scrollInScrambleOut(nowMillis);
                  break;
                }
              }

              if (msgDisplaying) {
                transition.updateRegularDisplaySeconds();
              } else {
                // do normal time thing when we are not in slots
                loadNumberArrayTime();

                allFadeOrNormal(true);
              }
            } else {
              // no slots mode, just do normal time thing
              loadNumberArrayTime();

              allFadeOrNormal(true);
            }
          }
        }
        break;
      }
    case MODE_MINS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incMins();
        }
        loadNumberArrayTime();
        highlight2and3();
        break;
      }
    case MODE_HOURS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incHours();
        }
        loadNumberArrayTime();
        highlight0and1();
        break;
      }
    case MODE_SECS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          resetSecond();
        }
        loadNumberArrayTime();
        highlight4and5();
        break;
      }
    case MODE_DAYS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incDays();
        }
        loadNumberArrayDate();
        highlightDaysDateFormat();
        break;
      }
    case MODE_MONTHS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incMonths();
        }
        loadNumberArrayDate();
        highlightMonthsDateFormat();
        break;
      }
    case MODE_YEARS_SET: {
        if (button1.isButtonPressedAndReleased()) {
          incYears();
        }
        loadNumberArrayDate();
        highlightYearsDateFormat();
        break;
      }
    case MODE_12_24: {
        if (button1.isButtonPressedAndReleased()) {
          hourMode = ! hourMode;
        }
        loadNumberArrayConfBool(hourMode, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_LEAD_BLANK: {
        if (button1.isButtonPressedAndReleased()) {
          blankLeading = !blankLeading;
        }
        loadNumberArrayConfBool(blankLeading, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_SCROLLBACK: {
        if (button1.isButtonPressedAndReleased()) {
          scrollback = !scrollback;
        }
        loadNumberArrayConfBool(scrollback, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_FADE: {
        if (button1.isButtonPressedAndReleased()) {
          fade = !fade;
        }
        loadNumberArrayConfBool(fade, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_DATE_FORMAT: {
        if (button1.isButtonPressedAndReleased()) {
          dateFormat++;
          if (dateFormat > DATE_FORMAT_MAX) {
            dateFormat = DATE_FORMAT_MIN;
          }
        }
        loadNumberArrayConfInt(dateFormat, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_DAY_BLANKING: {
        if (button1.isButtonPressedAndReleased()) {
          dayBlanking++;
          if (dayBlanking > DAY_BLANKING_MAX) {
            dayBlanking = DAY_BLANKING_MIN;
          }
        }
        loadNumberArrayConfInt(dayBlanking, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_HR_BLNK_START: {
        if (button1.isButtonPressedAndReleased()) {
          blankHourStart++;
          if (blankHourStart > HOURS_MAX) {
            blankHourStart = 0;
          }
        }
        loadNumberArrayConfInt(blankHourStart, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_HR_BLNK_END: {
        if (button1.isButtonPressedAndReleased()) {
          blankHourEnd++;
          if (blankHourEnd > HOURS_MAX) {
            blankHourEnd = 0;
          }
        }
        loadNumberArrayConfInt(blankHourEnd, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_SUPPRESS_ACP: {
        if (button1.isButtonPressedAndReleased()) {
          suppressACP = !suppressACP;
        }
        loadNumberArrayConfBool(suppressACP, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_BLANK_MODE: {
        if (button1.isButtonPressedAndReleased()) {
          blankMode++;
          if (blankMode > BLANK_MODE_MAX) {
            blankMode = BLANK_MODE_MIN;
          }
        }
        loadNumberArrayConfInt(blankMode, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_USE_LDR: {
        if (button1.isButtonPressedAndReleased()) {
          useLDR = !useLDR;
        }
        loadNumberArrayConfBool(useLDR, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_FADE_STEPS_UP: {
        if (button1.isButtonPressedAndReleased()) {
          fadeSteps++;
          if (fadeSteps > FADE_STEPS_MAX) {
            fadeSteps = FADE_STEPS_MIN;
          }
        }
        loadNumberArrayConfInt(fadeSteps, currentMode - MODE_12_24);
        displayConfig();
        fadeStep = DIGIT_DISPLAY_COUNT / fadeSteps;
        break;
      }
    case MODE_FADE_STEPS_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          fadeSteps--;
          if (fadeSteps < FADE_STEPS_MIN) {
            fadeSteps = FADE_STEPS_MAX;
          }
        }
        loadNumberArrayConfInt(fadeSteps, currentMode - MODE_12_24);
        displayConfig();
        fadeStep = DIGIT_DISPLAY_COUNT / fadeSteps;
        break;
      }
    case MODE_DISPLAY_SCROLL_STEPS_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          scrollSteps--;
          if (scrollSteps < SCROLL_STEPS_MIN) {
            scrollSteps = SCROLL_STEPS_MAX;
          }
        }
        loadNumberArrayConfInt(scrollSteps, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_DISPLAY_SCROLL_STEPS_UP: {
        if (button1.isButtonPressedAndReleased()) {
          scrollSteps++;
          if (scrollSteps > SCROLL_STEPS_MAX) {
            scrollSteps = SCROLL_STEPS_MIN;
          }
        }
        loadNumberArrayConfInt(scrollSteps, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_SLOTS_MODE: {
        if (button1.isButtonPressedAndReleased()) {
          slotsMode++;
          if (slotsMode > SLOTS_MODE_MAX) {
            slotsMode = SLOTS_MODE_MIN;
          }
        }
        loadNumberArrayConfInt(slotsMode, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_BACKLIGHT_MODE: {
        if (button1.isButtonPressedAndReleased()) {
          backlightMode++;
          if (backlightMode > BACKLIGHT_MAX) {
            backlightMode = BACKLIGHT_MIN;
          }
        }
        loadNumberArrayConfInt(backlightMode, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_RED_CNL: {
        if (backlightMode == BACKLIGHT_CYCLE) {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }

        if (button1.isButtonPressedAndReleased()) {
          redCnl++;
          if (redCnl > COLOUR_CNL_MAX) {
            redCnl = COLOUR_CNL_MIN;
          }
        }
        loadNumberArrayConfInt(redCnl, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_GRN_CNL: {
        if (backlightMode == BACKLIGHT_CYCLE) {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }

        if (button1.isButtonPressedAndReleased()) {
          grnCnl++;
          if (grnCnl > COLOUR_CNL_MAX) {
            grnCnl = COLOUR_CNL_MIN;
          }
        }
        loadNumberArrayConfInt(grnCnl, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_BLU_CNL: {
        if (backlightMode == BACKLIGHT_CYCLE) {
          // Skip if we are in cycle mode
          nextMode++;
          currentMode++;
        }

        if (button1.isButtonPressedAndReleased()) {
          bluCnl++;
          if (bluCnl > COLOUR_CNL_MAX) {
            bluCnl = COLOUR_CNL_MIN;
          }
        }
        loadNumberArrayConfInt(bluCnl, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_CYCLE_SPEED: {
        if (button1.isButtonPressedAndReleased()) {
          cycleSpeed = cycleSpeed + 2;
          if (cycleSpeed > CYCLE_SPEED_MAX) {
            cycleSpeed = CYCLE_SPEED_MIN;
          }
        }
        loadNumberArrayConfInt(cycleSpeed, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_TARGET_HV_UP: {
        if (button1.isButtonPressedAndReleased()) {
          hvTargetVoltage += 5;
          if (hvTargetVoltage > HVGEN_TARGET_VOLTAGE_MAX) {
            hvTargetVoltage = HVGEN_TARGET_VOLTAGE_MIN;
          }
        }
        loadNumberArrayConfInt(hvTargetVoltage, currentMode - MODE_12_24);
        rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);
        displayConfig();
        break;
      }
    case MODE_TARGET_HV_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          hvTargetVoltage -= 5;
          if (hvTargetVoltage < HVGEN_TARGET_VOLTAGE_MIN) {
            hvTargetVoltage = HVGEN_TARGET_VOLTAGE_MAX;
          }
        }
        loadNumberArrayConfInt(hvTargetVoltage, currentMode - MODE_12_24);
        rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);
        displayConfig();
        break;
      }
    case MODE_PULSE_UP: {
        if (button1.isButtonPressedAndReleased()) {
          pwmOn += 10;
          if (pwmOn > PWM_PULSE_MAX) {
            pwmOn = PWM_PULSE_MAX;
          }
          setPWMOnTime(pwmOn);
        }
        loadNumberArrayConfInt(pwmOn, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_PULSE_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          pwmOn -= 10;
          if (pwmOn > PWM_PULSE_MAX) {
            pwmOn = PWM_PULSE_MAX;
          }
          setPWMOnTime(pwmOn);
        }
        loadNumberArrayConfInt(pwmOn, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_MIN_DIM_UP: {
        if (button1.isButtonPressedAndReleased()) {
          minDim += 10;
          if (minDim > MIN_DIM_MAX) {
            minDim = MIN_DIM_MAX;
          }
        }
        loadNumberArrayConfInt(minDim, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_MIN_DIM_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          minDim -= 10;
          if (minDim < MIN_DIM_MIN) {
            minDim = MIN_DIM_MIN;
          }
        }
        loadNumberArrayConfInt(minDim, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_ANTI_GHOST_UP: {
        if (button1.isButtonPressedAndReleased()) {
          antiGhost += 1;
          if (antiGhost > ANTI_GHOST_MAX) {
            antiGhost = ANTI_GHOST_MAX;
          }
          dispCount = DIGIT_DISPLAY_COUNT + antiGhost;
        }
        loadNumberArrayConfInt(antiGhost, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_ANTI_GHOST_DOWN: {
        if (button1.isButtonPressedAndReleased()) {
          antiGhost -= 1;
          if (antiGhost < ANTI_GHOST_MIN) {
            antiGhost = ANTI_GHOST_MIN;
          }
          dispCount = DIGIT_DISPLAY_COUNT + antiGhost;
        }
        loadNumberArrayConfInt(antiGhost, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_TEMP: {
        loadNumberArrayTemp(currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_VERSION: {
        loadNumberArrayConfInt(SOFTWARE_VERSION, currentMode - MODE_12_24);
        displayConfig();
        break;
      }
    case MODE_TUBE_TEST: {
        allNormal();
        loadNumberArrayTestDigits();
        break;
      }
    case MODE_DIGIT_BURN: {
        if (button1.isButtonPressedAndReleased()) {
          digitBurnValue += 1;
          if (digitBurnValue > 9) {
            digitBurnValue = 0;

            digitOff();
            digitBurnDigit += 1;
            if (digitBurnDigit > 5) {
              digitBurnDigit = 0;
            }
          }
        }
      }
  }
}

// ************************************************************
// output a PWM LED channel, adjusting for dimming and PWM
// brightness:
// rawValue: The raw brightness value between 0 - 255
// ledPWMVal: The pwm factor between 0 - 1
// dimFactor: The dimming value between 0 - 1
// ************************************************************
byte getLEDAdjusted(float rawValue, float ledPWMVal, float dimFactor) {
  byte dimmedPWMVal = (byte)(rawValue * ledPWMVal * dimFactor);
  return dim_curve[dimmedPWMVal];
}

// ************************************************************
// Colour cycling 3: one colour dominates
// ************************************************************
void cycleColours3(int colors[3]) {
  cycleCount++;
  if (cycleCount > cycleSpeed) {
    cycleCount = 0;

    if (changeSteps == 0) {
      changeSteps = random(256);
      currentColour = random(3);
    }

    changeSteps--;

    switch (currentColour) {
      case 0:
        if (colors[0] < 255) {
          colors[0]++;
          if (colors[1] > 0) {
            colors[1]--;
          }
          if (colors[2] > 0) {
            colors[2]--;
          }
        } else {
          changeSteps = 0;
        }
        break;
      case 1:
        if (colors[1] < 255) {
          colors[1]++;
          if (colors[0] > 0) {
            colors[0]--;
          }
          if (colors[2] > 0) {
            colors[2]--;
          }
        } else {
          changeSteps = 0;
        }
        break;
      case 2:
        if (colors[2] < 255) {
          colors[2]++;
          if (colors[0] > 0) {
            colors[0]--;
          }
          if (colors[1] > 0) {
            colors[1]--;
          }
        } else {
          changeSteps = 0;
        }
        break;
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
  NumberArray[5] = second() % 10;
  NumberArray[4] = second() / 10;
  NumberArray[3] = minute() % 10;
  NumberArray[2] = minute() / 10;
  if (hourMode) {
    NumberArray[1] = hourFormat12() % 10;
    NumberArray[0] = hourFormat12() / 10;
  } else {
    NumberArray[1] = hour() % 10;
    NumberArray[0] = hour() / 10;
  }
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArraySameValue(byte val) {
  NumberArray[5] = val;
  NumberArray[4] = val;
  NumberArray[3] = val;
  NumberArray[2] = val;
  NumberArray[1] = val;
  NumberArray[0] = val;
}

// ************************************************************
// Break the time into displayable digits
// ************************************************************
void loadNumberArrayDate() {
  switch (dateFormat) {
    case DATE_FORMAT_YYMMDD:
      NumberArray[5] = day() % 10;
      NumberArray[4] = day() / 10;
      NumberArray[3] = month() % 10;
      NumberArray[2] = month() / 10;
      NumberArray[1] = (year() - 2000) % 10;
      NumberArray[0] = (year() - 2000) / 10;
      break;
    case DATE_FORMAT_MMDDYY:
      NumberArray[5] = (year() - 2000) % 10;
      NumberArray[4] = (year() - 2000) / 10;
      NumberArray[3] = day() % 10;
      NumberArray[2] = day() / 10;
      NumberArray[1] = month() % 10;
      NumberArray[0] = month() / 10;
      break;
    case DATE_FORMAT_DDMMYY:
      NumberArray[5] = (year() - 2000) % 10;
      NumberArray[4] = (year() - 2000) / 10;
      NumberArray[3] = month() % 10;
      NumberArray[2] = month() / 10;
      NumberArray[1] = day() % 10;
      NumberArray[0] = day() / 10;
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
  temp = (temp - float(wholeDegrees)) * 100.0;
  int fractDegrees = int(temp);

  NumberArray[3] = fractDegrees % 10;
  NumberArray[2] =  fractDegrees / 10;
  NumberArray[1] =  wholeDegrees % 10;
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
  NumberArray[0] = (digitOffCount / 1000) % 10;
}

// ************************************************************
// Test digits
// ************************************************************
void loadNumberArrayTestDigits() {
  NumberArray[5] =  second() % 10;
  NumberArray[4] = (second() + 1) % 10;
  NumberArray[3] = (second() + 2) % 10;
  NumberArray[2] = (second() + 3) % 10;
  NumberArray[1] = (second() + 4) % 10;
  NumberArray[0] = (second() + 5) % 10;
}

// ************************************************************
// Do the Anti Cathode Poisoning
// ************************************************************
void loadNumberArrayACP() {
  NumberArray[5] = (second() + acpOffset) % 10;
  NumberArray[4] = (second() / 10 + acpOffset) % 10;
  NumberArray[3] = (minute() + acpOffset) % 10;
  NumberArray[2] = (minute() / 10 + acpOffset) % 10;
  NumberArray[1] = (hour() + acpOffset)  % 10;
  NumberArray[0] = (hour() / 10 + acpOffset) % 10;
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
  if (confValue) {
    boolInt = 1;
  } else {
    boolInt = 0;
  }
  NumberArray[5] = (confNum) % 10;
  NumberArray[4] = (confNum / 10) % 10;
  NumberArray[3] = boolInt;
  NumberArray[2] = 0;
  NumberArray[1] = 0;
  NumberArray[0] = 0;
}

// ************************************************************
// Show an integer configuration value
// ************************************************************
void loadNumberArrayIP(byte byte1, byte byte2) {
  NumberArray[5] = (byte2) % 10;
  NumberArray[4] = (byte2 / 10) % 10;
  NumberArray[3] = (byte2 / 100) % 10;
  NumberArray[2] = (byte1) % 10;
  NumberArray[1] = (byte1 / 10) % 10;
  NumberArray[0] = (byte1 / 100) % 10;
}

// ************************************************************
// Decode the value to send to the 74141 and send it
// We do this via the decoder to allow easy adaptation to
// other pin layouts.
// ************************************************************
void SetSN74141Chip(int num1)
{
  // Map the logical numbers to the hardware pins we send to the SN74141 IC
  int decodedDigit = decodeDigit[num1];

  // Mask all digit bits to 0
  byte portb = PORTB;
  portb = portb & B11001010;

  // Set the bits we need
  switch ( decodedDigit )
  {
    case 0:                             break; // a=0;b=0;c=0;d=0
    case 1:  portb = portb | B00100000; break; // a=1;b=0;c=0;d=0
    case 2:  portb = portb | B00000100; break; // a=0;b=1;c=0;d=0
    case 3:  portb = portb | B00100100; break; // a=1;b=1;c=0;d=0
    case 4:  portb = portb | B00000001; break; // a=0;b=0;c=1;d=0
    case 5:  portb = portb | B00100001; break; // a=1;b=0;c=1;d=0
    case 6:  portb = portb | B00000101; break; // a=0;b=1;c=1;d=0
    case 7:  portb = portb | B00100101; break; // a=1;b=1;c=1;d=0
    case 8:  portb = portb | B00010000; break; // a=0;b=0;c=0;d=1
    case 9:  portb = portb | B00110000; break; // a=1;b=0;c=0;d=1
    default: portb = portb | B00110101; break; // a=1;b=1;c=1;d=1
  }
  PORTB = portb;
}

// ************************************************************
// Do a single complete display, including any fading and
// dimming requested. Performs the display loop
// DIGIT_DISPLAY_COUNT times for each digit, with no delays.
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

  for ( int i = 0 ; i < 6 ; i ++ )
  {
    if (blankTubes) {
      tmpDispType = BLANKED;
    } else {
      tmpDispType = displayType[i];
    }

    switch (tmpDispType) {
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
          fadeState[i] = fadeState[i] - 1;
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
        fadeState[i] = 0;
        currNumberArray[i] = NumberArray[i];
        digitSwitchTime = DIGIT_DISPLAY_COUNT;
      } else if (fadeState[i] > 1) {
        // Continue the fade
        fadeState[i] = fadeState[i] - 1;
        digitSwitchTime = (int) fadeState[i] * fadeStep;
      }
    } else {
      digitSwitchTime = DIGIT_DISPLAY_COUNT;
      currNumberArray[i] = NumberArray[i];
    }

    for (int timer = 0 ; timer < dispCount ; timer++) {
      if (timer == digitOnTime) {
        digitOn(i, currNumberArray[i]);
      }

      if  (timer == digitSwitchTime) {
        SetSN74141Chip(NumberArray[i]);
      }

      if (timer == digitOffTime) {
        digitOff();
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
// Assumes that all digits have previously been turned off
// by a call to "digitOff"
// ************************************************************
void digitOn(int digit, int value) {
  switch (digit) {
    case 0: PORTC = PORTC | B00001000; break; // PC3 - equivalent to digitalWrite(ledPin_a_1,HIGH);
    case 1: PORTC = PORTC | B00000100; break; // PC2 - equivalent to digitalWrite(ledPin_a_2,HIGH);
    case 2: PORTD = PORTD | B00010000; break; // PD4 - equivalent to digitalWrite(ledPin_a_3,HIGH);
    case 3: PORTD = PORTD | B00000100; break; // PD2 - equivalent to digitalWrite(ledPin_a_4,HIGH);
    case 4: PORTD = PORTD | B00000010; break; // PD1 - equivalent to digitalWrite(ledPin_a_5,HIGH);
    case 5: PORTD = PORTD | B00000001; break; // PD0 - equivalent to digitalWrite(ledPin_a_6,HIGH);
  }
  SetSN74141Chip(value);
  TCCR1A = tccrOn;
}

// ************************************************************
// Finish displaying a digit and turn the HVGen on
// ************************************************************
void digitOff() {
  TCCR1A = tccrOff;
  //digitalWrite(anodePins[digit], LOW);

  // turn all digits off - equivalent to digitalWrite(ledPin_a_n,LOW); (n=1,2,3,4,5,6) but much faster
  PORTC = PORTC & B11110011;
  PORTD = PORTD & B11101000;
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
void allFadeOrNormal(boolean blanking) {
  if (fade) {
    allFade();
  } else {
    allNormal();
  }

  if (blanking) {
    applyBlanking();
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
  switch (dateFormat) {
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
  switch (dateFormat) {
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
  switch (dateFormat) {
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
// Reset the seconds to 00
// ************************************************************
void resetSecond() {
  byte tmpSecs = 0;
  setTime(hour(), minute(), tmpSecs, day(), month(), year());
  setRTC();
}

// ************************************************************
// increment the time by 1 Sec
// ************************************************************
void incSecond() {
  byte tmpSecs = second();
  tmpSecs++;
  if (tmpSecs >= SECS_MAX) {
    tmpSecs = 0;
  }
  setTime(hour(), minute(), tmpSecs, day(), month(), year());
  setRTC();
}

// ************************************************************
// increment the time by 1 min
// ************************************************************
void incMins() {
  byte tmpMins = minute();
  tmpMins++;
  if (tmpMins >= MINS_MAX) {
    tmpMins = 0;
  }
  setTime(hour(), tmpMins, 0, day(), month(), year());
  setRTC();
}

// ************************************************************
// increment the time by 1 hour
// ************************************************************
void incHours() {
  byte tmpHours = hour();
  tmpHours++;

  if (tmpHours >= HOURS_MAX) {
    tmpHours = 0;
  }
  setTime(tmpHours, minute(), second(), day(), month(), year());
  setRTC();
}

// ************************************************************
// increment the date by 1 day
// ************************************************************
void incDays() {
  byte tmpDays = day();
  tmpDays++;

  int maxDays;
  switch (month())
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

  if (tmpDays > maxDays) {
    tmpDays = 1;
  }
  setTime(hour(), minute(), second(), tmpDays, month(), year());
  setRTC();
}

// ************************************************************
// increment the month by 1 month
// ************************************************************
void incMonths() {
  byte tmpMonths = month();
  tmpMonths++;

  if (tmpMonths > 12) {
    tmpMonths = 1;
  }
  setTime(hour(), minute(), second(), day(), tmpMonths, year());
  setRTC();
}

// ************************************************************
// increment the year by 1 year
// ************************************************************
void incYears() {
  byte tmpYears = year() % 100;
  tmpYears++;

  if (tmpYears > 50) {
    tmpYears = 15;
  }
  setTime(hour(), minute(), second(), day(), month(), 2000 + tmpYears);
  setRTC();
}

// ************************************************************
// Check the blanking
// ************************************************************
boolean checkBlanking() {
  // Check day blanking, but only when we are in
  // normal time mode
  if (currentMode == MODE_TIME) {
    switch (dayBlanking) {
      case DAY_BLANKING_NEVER:
        return false;
      case DAY_BLANKING_HOURS:
        return getHoursBlanked();
      case DAY_BLANKING_WEEKEND:
        return ((weekday() == 1) || (weekday() == 7));
      case DAY_BLANKING_WEEKEND_OR_HOURS:
        return ((weekday() == 1) || (weekday() == 7)) || getHoursBlanked();
      case DAY_BLANKING_WEEKEND_AND_HOURS:
        return ((weekday() == 1) || (weekday() == 7)) && getHoursBlanked();
      case DAY_BLANKING_WEEKDAY:
        return ((weekday() > 1) && (weekday() < 7));
      case DAY_BLANKING_WEEKDAY_OR_HOURS:
        return ((weekday() > 1) && (weekday() < 7)) || getHoursBlanked();
      case DAY_BLANKING_WEEKDAY_AND_HOURS:
        return ((weekday() > 1) && (weekday() < 7)) && getHoursBlanked();
      case DAY_BLANKING_ALWAYS:
        return true;
    }
  }
}

// ************************************************************
// If we are currently blanked based on hours
// ************************************************************
boolean getHoursBlanked() {
  if (blankHourStart > blankHourEnd) {
    // blanking before midnight
    return ((hour() >= blankHourStart) || (hour() < blankHourEnd));
  } else if (blankHourStart < blankHourEnd) {
    // dim at or after midnight
    return ((hour() >= blankHourStart) && (hour() < blankHourEnd));
  } else {
    // no dimming if Start = End
    return false;
  }
}

// ************************************************************
// Set the tubes and LEDs blanking variables based on blanking mode and 
// blank mode settings
// ************************************************************
void setTubesAndLEDSBlankMode() {
  if (blanked) {
    switch(blankMode) {
      case BLANK_MODE_TUBES:
      {
        blankTubes = true;
        blankLEDs = false;
        break;
      }
      case BLANK_MODE_LEDS:
      {
        blankTubes = false;
        blankLEDs = true;
        break;
      }
      case BLANK_MODE_BOTH:
      {
        blankTubes = true;
        blankLEDs = true;
        break;
      }
    }
  } else {
    blankTubes = false;
    blankLEDs = false;
  }
}

void randomRGBFlash(int delayVal) {
  digitalWrite(tickLed, HIGH);
  if (random(3) == 0) {
    digitalWrite(RLed, HIGH);
  }
  if (random(3) == 0) {
    digitalWrite(GLed, HIGH);
  }
  if (random(3) == 0) {
    digitalWrite(BLed, HIGH);
  }
  delay(delayVal);
  digitalWrite(tickLed, LOW);
  digitalWrite(RLed, LOW);
  digitalWrite(GLed, LOW);
  digitalWrite(BLed, LOW);
  delay(delayVal);
}

void setLedsTestPattern(unsigned long currentMillis) {
  unsigned long currentSec = currentMillis / 1000;
  byte phase = currentSec % 4;
  digitalWrite(tickLed, LOW);
  digitalWrite(RLed, LOW);
  digitalWrite(GLed, LOW);
  digitalWrite(BLed, LOW);

  if (phase == 0) {
    digitalWrite(tickLed, HIGH);
  }
  
  if (phase == 1) {
    digitalWrite(RLed, HIGH);
  }
  
  if (phase == 2) {
    digitalWrite(GLed, HIGH);
  }
  
  if (phase == 3) {
    digitalWrite(BLed, HIGH);
  }
}

//**********************************************************************************
//**********************************************************************************
//*                         RTC Module Time Provider                               *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Get the time from the RTC
// ************************************************************
void getRTCTime() {
  // Start the RTC communication in master mode
  Wire.end();
  Wire.begin();

  // Set up the time provider
  // first try to find the RTC, if not available, go into slave mode
  Wire.beginTransmission(RTC_I2C_ADDRESS);
  useRTC = (Wire.endTransmission() == 0);
  if (useRTC) {
    bool PM;
    bool twentyFourHourClock;
    bool century = false;

    byte years = Clock.getYear() + 2000;
    byte months = Clock.getMonth(century);
    byte days = Clock.getDate();
    byte hours = Clock.getHour(twentyFourHourClock, PM);
    byte mins = Clock.getMinute();
    byte secs = Clock.getSecond();
    setTime(hours, mins, secs, days, months, years);
      
    // Make sure the clock keeps running even on battery
    if (!Clock.oscillatorCheck())
      Clock.enableOscillator(true, true, 0);
  }
  
  // Return back to I2C in slave mode
  Wire.end();
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
}

// ************************************************************
// Set the date/time in the RTC from the internal time
// Always hold the time in 24 format, we convert to 12 in the
// display.
// ************************************************************
void setRTC() {
  if (useRTC) {
    // Start the RTC communication in master mode
    Wire.end();
    Wire.begin();

    // Set up the time provider
    // first try to find the RTC, if not available, go into slave mode
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Clock.setClockMode(false); // false = 24h
    Clock.setYear(year() % 100);
    Clock.setMonth(month());
    Clock.setDate(day());
    Clock.setDoW(weekday());
    Clock.setHour(hour());
    Clock.setMinute(minute());
    Clock.setSecond(second());

    Wire.endTransmission();
    Wire.end();
    Wire.begin(I2C_SLAVE_ADDR);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);
  }
}

// ************************************************************
// Get the temperature from the RTC
// ************************************************************
float getRTCTemp() {
  if (useRTC) {
    return Clock.getTemperature();
  } else {
    return 0.0;
  }
}

//**********************************************************************************
//**********************************************************************************
//*                               EEPROM interface                                 *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Save current values back to EEPROM
// ************************************************************
void saveEEPROMValues() {
  EEPROM.write(EE_12_24, hourMode);
  EEPROM.write(EE_FADE_STEPS, fadeSteps);
  EEPROM.write(EE_DATE_FORMAT, dateFormat);
  EEPROM.write(EE_DAY_BLANKING, dayBlanking);
  EEPROM.write(EE_DIM_DARK_LO, dimDark % 256);
  EEPROM.write(EE_DIM_DARK_HI, dimDark / 256);
  EEPROM.write(EE_BLANK_LEAD_ZERO, blankLeading);
  EEPROM.write(EE_SCROLLBACK, scrollback);
  EEPROM.write(EE_FADE, fade);
  EEPROM.write(EE_SCROLL_STEPS, scrollSteps);
  EEPROM.write(EE_DIM_BRIGHT_LO, dimBright % 256);
  EEPROM.write(EE_DIM_BRIGHT_HI, dimBright / 256);
  EEPROM.write(EE_DIM_SMOOTH_SPEED, sensorSmoothCountLDR);
  EEPROM.write(EE_RED_INTENSITY, redCnl);
  EEPROM.write(EE_GRN_INTENSITY, grnCnl);
  EEPROM.write(EE_BLU_INTENSITY, bluCnl);
  EEPROM.write(EE_BACKLIGHT_MODE, backlightMode);
  EEPROM.write(EE_HV_VOLTAGE, hvTargetVoltage);
  EEPROM.write(EE_SUPPRESS_ACP, suppressACP);
  EEPROM.write(EE_HOUR_BLANK_START, blankHourStart);
  EEPROM.write(EE_HOUR_BLANK_END, blankHourEnd);
  EEPROM.write(EE_CYCLE_SPEED, cycleSpeed);
  EEPROM.write(EE_PULSE_LO, pwmOn % 256);
  EEPROM.write(EE_PULSE_HI, pwmOn / 256);
  EEPROM.write(EE_PWM_TOP_LO, pwmTop % 256);
  EEPROM.write(EE_PWM_TOP_HI, pwmTop / 256);
  EEPROM.write(EE_MIN_DIM_LO, minDim % 256);
  EEPROM.write(EE_MIN_DIM_HI, minDim / 256);
  EEPROM.write(EE_ANTI_GHOST, antiGhost);
  EEPROM.write(EE_USE_LDR, useLDR);
  EEPROM.write(EE_BLANK_MODE, blankMode);
  EEPROM.write(EE_SLOTS_MODE, slotsMode);
}

// ************************************************************
// read EEPROM values
// ************************************************************
void readEEPROMValues() {
  hourMode = EEPROM.read(EE_12_24);

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

  dimDark = EEPROM.read(EE_DIM_DARK_HI) * 256 + EEPROM.read(EE_DIM_DARK_LO);
  if ((dimDark < SENSOR_LOW_MIN) || (dimDark > SENSOR_LOW_MAX)) {
    dimDark = SENSOR_LOW_DEFAULT;
  }

  blankLeading = EEPROM.read(EE_BLANK_LEAD_ZERO);
  scrollback = EEPROM.read(EE_SCROLLBACK);
  fade = EEPROM.read(EE_FADE);

  scrollSteps = EEPROM.read(EE_SCROLL_STEPS);
  if ((scrollSteps < SCROLL_STEPS_MIN) || (scrollSteps > SCROLL_STEPS_MAX)) {
    scrollSteps = SCROLL_STEPS_DEFAULT;
  }

  dimBright = EEPROM.read(EE_DIM_BRIGHT_HI) * 256 + EEPROM.read(EE_DIM_BRIGHT_LO);
  if ((dimBright < SENSOR_HIGH_MIN) || (dimBright > SENSOR_HIGH_MAX)) {
    dimBright = SENSOR_HIGH_DEFAULT;
  }

  sensorSmoothCountLDR = EEPROM.read(EE_DIM_SMOOTH_SPEED);
  if ((sensorSmoothCountLDR < SENSOR_SMOOTH_READINGS_MIN) || (sensorSmoothCountLDR > SENSOR_SMOOTH_READINGS_MAX)) {
    sensorSmoothCountLDR = SENSOR_SMOOTH_READINGS_DEFAULT;
  }

  backlightMode = EEPROM.read(EE_BACKLIGHT_MODE);
  if ((backlightMode < BACKLIGHT_MIN) || (backlightMode > BACKLIGHT_MAX)) {
    backlightMode = BACKLIGHT_DEFAULT;
  }

  redCnl = EEPROM.read(EE_RED_INTENSITY);
  if ((redCnl < COLOUR_CNL_MIN) || (redCnl > COLOUR_CNL_MAX)) {
    redCnl = COLOUR_RED_CNL_DEFAULT;
  }

  grnCnl = EEPROM.read(EE_GRN_INTENSITY);
  if ((grnCnl < COLOUR_CNL_MIN) || (grnCnl > COLOUR_CNL_MAX)) {
    grnCnl = COLOUR_GRN_CNL_DEFAULT;
  }

  bluCnl = EEPROM.read(EE_BLU_INTENSITY);
  if ((bluCnl < COLOUR_CNL_MIN) || (bluCnl > COLOUR_CNL_MAX)) {
    bluCnl = COLOUR_BLU_CNL_DEFAULT;
  }

  hvTargetVoltage = EEPROM.read(EE_HV_VOLTAGE);
  if ((hvTargetVoltage < HVGEN_TARGET_VOLTAGE_MIN) || (hvTargetVoltage > HVGEN_TARGET_VOLTAGE_MAX)) {
    hvTargetVoltage = HVGEN_TARGET_VOLTAGE_DEFAULT;
  }

  pwmOn = EEPROM.read(EE_PULSE_HI) * 256 + EEPROM.read(EE_PULSE_LO);
  if ((pwmOn < PWM_PULSE_MIN) || (pwmOn > PWM_PULSE_MAX)) {
    pwmOn = PWM_PULSE_DEFAULT;

    // Hmmm, need calibration
    EEPROM.write(EE_HVG_NEED_CALIB, true);
  }

  pwmTop = EEPROM.read(EE_PWM_TOP_HI) * 256 + EEPROM.read(EE_PWM_TOP_LO);
  if ((pwmTop < PWM_TOP_MIN) || (pwmTop > PWM_TOP_MAX)) {
    pwmTop = PWM_TOP_DEFAULT;

    // Hmmm, need calibration
    EEPROM.write(EE_HVG_NEED_CALIB, true);
  }

  suppressACP = EEPROM.read(EE_SUPPRESS_ACP);

  blankHourStart = EEPROM.read(EE_HOUR_BLANK_START);
  if ((blankHourStart < 0) || (blankHourStart > HOURS_MAX)) {
    blankHourStart = 0;
  }

  blankHourEnd = EEPROM.read(EE_HOUR_BLANK_END);
  if ((blankHourEnd < 0) || (blankHourEnd > HOURS_MAX)) {
    blankHourEnd = 7;
  }

  cycleSpeed = EEPROM.read(EE_CYCLE_SPEED);
  if ((cycleSpeed < CYCLE_SPEED_MIN) || (cycleSpeed > CYCLE_SPEED_MAX)) {
    cycleSpeed = CYCLE_SPEED_DEFAULT;
  }

  minDim = EEPROM.read(EE_MIN_DIM_HI) * 256 + EEPROM.read(EE_MIN_DIM_LO);
  if ((minDim < MIN_DIM_MIN) || (minDim > MIN_DIM_MAX)) {
    minDim = MIN_DIM_DEFAULT;
  }

  antiGhost = EEPROM.read(EE_ANTI_GHOST);
  if ((antiGhost < ANTI_GHOST_MIN) || (antiGhost > ANTI_GHOST_MAX)) {
    antiGhost = ANTI_GHOST_DEFAULT;
  }
  dispCount = DIGIT_DISPLAY_COUNT + antiGhost;

  blankMode= EEPROM.read(EE_BLANK_MODE);
  if ((blankMode < BLANK_MODE_MIN) || (blankMode > BLANK_MODE_MAX)) {
    blankMode = BLANK_MODE_DEFAULT;
  }

  useLDR = EEPROM.read(EE_USE_LDR);

  slotsMode= EEPROM.read(EE_SLOTS_MODE);
  if ((slotsMode < SLOTS_MODE_MIN) || (slotsMode > SLOTS_MODE_MAX)) {
    slotsMode = SLOTS_MODE_DEFAULT;
  }

}

// ************************************************************
// Reset EEPROM values back to what they once were
// ************************************************************
void factoryReset() {
  hourMode = HOUR_MODE_DEFAULT;
  blankLeading = LEAD_BLANK_DEFAULT;
  scrollback = SCROLLBACK_DEFAULT;
  fade = FADE_DEFAULT;
  fadeSteps = FADE_STEPS_DEFAULT;
  dateFormat = DATE_FORMAT_DEFAULT;
  dayBlanking = DAY_BLANKING_DEFAULT;
  dimDark = SENSOR_LOW_DEFAULT;
  scrollSteps = SCROLL_STEPS_DEFAULT;
  dimBright = SENSOR_HIGH_DEFAULT;
  sensorSmoothCountLDR = SENSOR_SMOOTH_READINGS_DEFAULT;
  dateFormat = DATE_FORMAT_DEFAULT;
  dayBlanking = DAY_BLANKING_DEFAULT;
  backlightMode = BACKLIGHT_DEFAULT;
  redCnl = COLOUR_RED_CNL_DEFAULT;
  grnCnl = COLOUR_GRN_CNL_DEFAULT;
  bluCnl = COLOUR_BLU_CNL_DEFAULT;
  hvTargetVoltage = HVGEN_TARGET_VOLTAGE_DEFAULT;
  suppressACP = SUPPRESS_ACP_DEFAULT;
  blankHourStart = 0;
  blankHourEnd = 7;
  cycleSpeed = CYCLE_SPEED_DEFAULT;
  pwmOn = PWM_PULSE_DEFAULT;
  pwmTop = PWM_TOP_DEFAULT;
  minDim = MIN_DIM_DEFAULT;
  antiGhost = ANTI_GHOST_DEFAULT;
  useLDR = USE_LDR_DEFAULT;
  blankMode = BLANK_MODE_DEFAULT;
  slotsMode = SLOTS_MODE_DEFAULT;

  saveEEPROMValues();
}

//**********************************************************************************
//**********************************************************************************
//*                          High Voltage generator                                *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Adjust the HV gen to achieve the voltage we require
// Pre-calculate the threshold value of the ADC read and make
// a simple comparison against this for speed
// We control only the PWM "off" time, because the "on" time
// affects the current consumption and MOSFET heating
// ************************************************************
void checkHVVoltage() {
  if (getSmoothedHVSensorReading() > rawHVADCThreshold) {
    setPWMTopTime(pwmTop + 1);
  } else {
    setPWMTopTime(pwmTop - 1);
  }
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

//**********************************************************************************
//**********************************************************************************
//*                          Light Dependent Resistor                              *
//**********************************************************************************
//**********************************************************************************

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
  if (useLDR) {
    int rawSensorVal = 1023 - analogRead(LDRPin);
    double sensorDiff = rawSensorVal - sensorLDRSmoothed;
    sensorLDRSmoothed += (sensorDiff / sensorSmoothCountLDR);

    double sensorSmoothedResult = sensorLDRSmoothed - dimDark;
    if (sensorSmoothedResult < dimDark) sensorSmoothedResult = dimDark;
    if (sensorSmoothedResult > dimBright) sensorSmoothedResult = dimBright;
    sensorSmoothedResult = (sensorSmoothedResult - dimDark) * sensorFactor;

    int returnValue = sensorSmoothedResult;

    if (returnValue < minDim) returnValue = minDim;
    if (returnValue > DIGIT_DISPLAY_OFF) returnValue = DIGIT_DISPLAY_OFF;
    return returnValue;
  } else {
    return DIGIT_DISPLAY_OFF;  
  }
}

// ******************************************************************
// Routine to check the PWM LEDs
// brightens and dims a PWM capable LED
// - 0 to 255 ramp up
// - 256 to 511 plateau
// - 512 to 767 ramp down
// ******************************************************************
void checkLEDPWM(byte LEDPin, int step) {
  if (step > 767) {
    analogWrite(LEDPin, getLEDAdjusted(0, 1, 1));
  } else if (step > 512) {
    analogWrite(LEDPin, getLEDAdjusted(255 - (step - 512), 1, 1));
  } else if (step > 255) {
    analogWrite(LEDPin, getLEDAdjusted(255, 1, 1));
  } else if (step > 0) {
    analogWrite(LEDPin, getLEDAdjusted(step, 1, 1));
  }
}

// ******************************************************************
// Calibrate the HV generator
// The idea here is to get the right combination of PWM on and top
// time to provide the right high voltage with the minimum power
// Consumption.
//
// Every combination of tubes and external power supply is different
// and we need to pick the right PWM total duration ("top") and
// PWM on time ("on") to match the power supply and tubes.
// Once we pick the "on" time, it is not adjusted during run time.
// PWM top is adjusted during run.
//
// The PWM on time is picked so that we reach just the point that the
// inductor goes into saturation - any more time on is just being used
// to heat the MOSFET and the inductor, but not provide any voltage.
//
// We go through two cycles: each time we decrease the PWM top
// (increase frequency) to give more voltage, then reduce PWM on
// until we notice a drop in voltage.
// ******************************************************************
void calibrateHVG() {

  // *************** first pass - get approximate frequency *************
  rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage + 5);

  setPWMOnTime(PWM_PULSE_DEFAULT);
  // Calibrate HVGen at full
  for (int i = 0 ; i < 768 ; i++ ) {
    loadNumberArraySameValue(8);
    allBright();
    outputDisplay();
    checkHVVoltage();
    checkLEDPWM(tickLed, i);
  }

  // *************** second pass - get on time minimum *************
  rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage);

  // run up the on time from the minimum to where we reach the required voltage
  setPWMOnTime(PWM_PULSE_MIN);
  for (int i = 0 ; i < 768 ; i++ ) {
    //loadNumberArray8s();
    loadNumberArrayConfInt(pwmOn, currentMode - MODE_12_24);
    allBright();
    outputDisplay();

    if (getSmoothedHVSensorReading() < rawHVADCThreshold) {
      if ((i % 8) == 0 ) {
        incPWMOnTime();
      }
    }
    checkLEDPWM(RLed, i);
  }

  int bottomOnValue = pwmOn;

  // *************** third pass - get on time maximum *************
  setPWMOnTime(pwmOn + 50);
  for (int i = 0 ; i < 768 ; i++ ) {
    //loadNumberArray8s();
    loadNumberArrayConfInt(pwmOn, currentMode - MODE_12_24);
    allBright();
    outputDisplay();

    if (getSmoothedHVSensorReading() > rawHVADCThreshold) {
      if ((i % 8) == 0 ) {
        decPWMOnTime();
      }
    }
    checkLEDPWM(GLed, i);
  }

  int topOnValue = pwmOn;

  int aveOnValue = (bottomOnValue + topOnValue) / 2;
  setPWMOnTime(aveOnValue);

  // *************** fourth pass - adjust the frequency *************
  rawHVADCThreshold = getRawHVADCThreshold(hvTargetVoltage + 5);

  // Calibrate HVGen at full
  for (int i = 0 ; i < 768 ; i++ ) {
    loadNumberArraySameValue(8);
    allBright();
    outputDisplay();
    checkHVVoltage();
    checkLEDPWM(BLed, i);
  }
}

/**
   Set the PWM top time. Bounds check it so that it stays
   between the defined minimum and maximum, and that it
   does not go under the PWM On time (plus a safety margin).

   Set both the internal "pwmTop" value and the register.
*/
void setPWMTopTime(int newTopTime) {
  if (newTopTime < PWM_TOP_MIN) {
    newTopTime = PWM_TOP_MIN;
  }

  if (newTopTime > PWM_TOP_MAX) {
    newTopTime = PWM_TOP_MAX;
  }

  if (newTopTime < (pwmOn + PWM_OFF_MIN)) {
    newTopTime = pwmOn + PWM_OFF_MIN;
  }

  ICR1 = newTopTime;
  pwmTop = newTopTime;
}

/**
   Set the new PWM on time. Bounds check it to make sure
   that is stays between pulse min and max, and that it
   does not get bigger than PWM top, less the safety margin.

   Set both the internal "pwmOn" value and the register.
*/
void setPWMOnTime(int newOnTime) {
  if (newOnTime < PWM_PULSE_MIN) {
    newOnTime = PWM_PULSE_MIN;
  }

  if (newOnTime > PWM_PULSE_MAX) {
    newOnTime = PWM_PULSE_MAX;
  }

  if (newOnTime > (pwmTop - PWM_OFF_MIN)) {
    newOnTime = pwmTop - PWM_OFF_MIN;
  }

  OCR1A = newOnTime;
  pwmOn = newOnTime;
}

void incPWMOnTime() {
  setPWMOnTime(pwmOn + 1);
}

void decPWMOnTime() {
  setPWMOnTime(pwmOn - 1);
}

/**
   Get the HV sensor reading. Smooth it using a simple
   moving average calculation.
*/
int getSmoothedHVSensorReading() {
  int rawSensorVal = analogRead(sensorPin);
  double sensorDiff = rawSensorVal - sensorHVSmoothed;
  sensorHVSmoothed += (sensorDiff / sensorSmoothCountHV);
  int sensorHVSmoothedInt = (int) sensorHVSmoothed;
  return sensorHVSmoothedInt;
}

//**********************************************************************************
//**********************************************************************************
//*                                 I2C interface                                  *
//**********************************************************************************
//**********************************************************************************

/**
   receive information from the master
*/
void receiveEvent(int bytes) {  
  // the operation tells us what we are getting
  int operation = Wire.read();

  if (operation == I2C_TIME_UPDATE) {
    // If we're getting time from the WiFi module, mark that we have an active WiFi with a 5 min time out
    useWiFi = MAX_WIFI_TIME;
    
    int newYears = Wire.read();
    int newMonths = Wire.read();
    int newDays = Wire.read();

    int newHours = Wire.read();
    int newMins = Wire.read();
    int newSecs = Wire.read();

    setTime(newHours, newMins, newSecs, newDays, newMonths, newYears);
  } else if (operation == I2C_SET_OPTION_12_24) {
    byte readByte1224 = Wire.read();
    hourMode = (readByte1224 == 1);
    EEPROM.write(EE_12_24, true);
  } else if (operation == I2C_SET_OPTION_BLANK_LEAD) {
    byte readByteBlank = Wire.read();
    blankLeading = (readByteBlank == 1);
    EEPROM.write(EE_BLANK_LEAD_ZERO, blankLeading);
  } else if (operation == I2C_SET_OPTION_SCROLLBACK) {
    byte readByteSB = Wire.read();
    scrollback = (readByteSB == 1);
    EEPROM.write(EE_SCROLLBACK, scrollback);
  } else if (operation == I2C_SET_OPTION_SUPPRESS_ACP) {
    byte readByteSA = Wire.read();
    suppressACP = (readByteSA == 1);
    EEPROM.write(EE_SUPPRESS_ACP, suppressACP);
  } else if (operation == I2C_SET_OPTION_DATE_FORMAT) {
    dateFormat = Wire.read();
    EEPROM.write(EE_DATE_FORMAT, dateFormat);
  } else if (operation == I2C_SET_OPTION_DAY_BLANKING) {
    dayBlanking = Wire.read();
    EEPROM.write(EE_DAY_BLANKING, dayBlanking);
  } else if (operation == I2C_SET_OPTION_BLANK_START) {
    blankHourStart = Wire.read();
    EEPROM.write(EE_HOUR_BLANK_START, blankHourStart);
  } else if (operation == I2C_SET_OPTION_BLANK_END) {
    blankHourEnd = Wire.read();
    EEPROM.write(EE_HOUR_BLANK_END, blankHourEnd);
  } else if (operation == I2C_SET_OPTION_FADE_STEPS) {
    fadeSteps = Wire.read();
    EEPROM.write(EE_FADE_STEPS, fadeSteps);
  } else if (operation == I2C_SET_OPTION_SCROLL_STEPS) {
    scrollSteps = Wire.read();
    EEPROM.write(EE_SCROLL_STEPS, scrollSteps);
  } else if (operation == I2C_SET_OPTION_BACKLIGHT_MODE) {
    backlightMode = Wire.read();
    EEPROM.write(EE_BACKLIGHT_MODE, backlightMode);
  } else if (operation == I2C_SET_OPTION_RED_CHANNEL) {
    redCnl = Wire.read();
    EEPROM.write(EE_RED_INTENSITY, redCnl);
  } else if (operation == I2C_SET_OPTION_GREEN_CHANNEL) {
    grnCnl = Wire.read();
    EEPROM.write(EE_GRN_INTENSITY, grnCnl);
  } else if (operation == I2C_SET_OPTION_BLUE_CHANNEL) {
    bluCnl = Wire.read();
    EEPROM.write(EE_BLU_INTENSITY, bluCnl);
  } else if (operation == I2C_SET_OPTION_CYCLE_SPEED) {
    cycleSpeed = Wire.read();
    EEPROM.write(EE_CYCLE_SPEED, cycleSpeed);
  } else if (operation == I2C_SHOW_IP_ADDR) {
    ourIP[0] = Wire.read();
    ourIP[1] = Wire.read();
    ourIP[2] = Wire.read();
    ourIP[3] = Wire.read();
  } else if (operation == I2C_SET_OPTION_FADE) {
    fade = Wire.read();
    EEPROM.write(EE_FADE, fade);
  } else if (operation == I2C_SET_OPTION_USE_LDR) {
    byte readByteUseLDR = Wire.read();
    useLDR = (readByteUseLDR == 1);
    EEPROM.write(EE_USE_LDR, useLDR);
  } else if (operation == I2C_SET_OPTION_BLANK_MODE) {
    blankMode = Wire.read();
    EEPROM.write(EE_BLANK_MODE, blankMode);
  } else if (operation == I2C_SET_OPTION_SLOTS_MODE) {
    slotsMode = Wire.read();
    EEPROM.write(EE_SLOTS_MODE, slotsMode);
  }
}

/**
   send information to the master
*/
void requestEvent() {
  byte configArray[20];
  int idx = 0;
  configArray[idx++] = 48;  // protocol version
  configArray[idx++] = encodeBooleanForI2C(hourMode);
  configArray[idx++] = encodeBooleanForI2C(blankLeading);
  configArray[idx++] = encodeBooleanForI2C(scrollback);
  configArray[idx++] = encodeBooleanForI2C(suppressACP);
  configArray[idx++] = encodeBooleanForI2C(fade);
  configArray[idx++] = dateFormat;
  configArray[idx++] = dayBlanking;
  configArray[idx++] = blankHourStart;
  configArray[idx++] = blankHourEnd;
  configArray[idx++] = fadeSteps;
  configArray[idx++] = scrollSteps;
  configArray[idx++] = backlightMode;
  configArray[idx++] = redCnl;
  configArray[idx++] = grnCnl;
  configArray[idx++] = bluCnl;
  configArray[idx++] = cycleSpeed;
  configArray[idx++] = encodeBooleanForI2C(useLDR);
  configArray[idx++] = blankMode;
  configArray[idx++] = slotsMode;

  Wire.write(configArray, 20);
}

byte encodeBooleanForI2C(boolean valueToProcess) {
  if (valueToProcess) {
    byte byteToSend = 1;
    return byteToSend;
  } else {
    byte byteToSend = 0;
    return byteToSend;
  }
}

