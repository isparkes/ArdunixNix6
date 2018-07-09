#ifndef ClockButton_h
#define ClockButton_h

#include "Arduino.h"

class ClockButton
{
  public:
    ClockButton(int inputPin, boolean activeLow);
    void checkButton(unsigned long nowMillis);
    void reset();
    boolean isButtonPressedNow();
    boolean isButtonPressed();
    boolean isButtonPressed1S();
    boolean isButtonPressed2S();
    boolean isButtonPressed8S();
    boolean isButtonPressedAndReleased();
    boolean isButtonPressedReleased1S();
    boolean isButtonPressedReleased2S();
    boolean isButtonPressedReleased8S();
  private:
    int _inputPin;
    boolean _activeLow;
    void checkButtonInternal(unsigned long nowMillis);
    void resetInternal();
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
};

#endif
