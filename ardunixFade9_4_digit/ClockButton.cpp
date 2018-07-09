#include "Arduino.h"
#include "ClockButton.h"

ClockButton::ClockButton(int inputPin, boolean activeLow)
{
  pinMode(inputPin, OUTPUT);
  _inputPin = inputPin;
  _activeLow = activeLow;
}

// ************************************************************
// MAIN BUTTON CHECK ENTRY POINT - should be called periodically
// See if the button was pressed and debounce. We perform a
// sort of preview here, then confirm by releasing. We track
// 3 lengths of button press: momentarily, 1S and 2S.
// ************************************************************
void ClockButton::checkButton(unsigned long nowMillis)
{
  checkButtonInternal(nowMillis);
}

// ************************************************************
// Reset everything
// ************************************************************
void ClockButton::reset()
{
  resetInternal();
}

// ************************************************************
// Check if button is pressed right now (just debounce)
// ************************************************************
boolean ClockButton::isButtonPressedNow() {
  return button1PressedCount == debounceCounter;
}

// ************************************************************
// Check if button is pressed momentarily
// ************************************************************
boolean ClockButton::isButtonPressed() {
  return buttonPress;
}

// ************************************************************
// Check if button is pressed for a long time (> 1S)
// ************************************************************
boolean ClockButton::isButtonPressed1S() {
  return buttonPress1S;
}

// ************************************************************
// Check if button is pressed for a moderately long time (> 2S)
// ************************************************************
boolean ClockButton::isButtonPressed2S() {
  return buttonPress2S;
}

// ************************************************************
// Check if button is pressed for a very long time (> 8S)
// ************************************************************
boolean ClockButton::isButtonPressed8S() {
  return buttonPress8S;
}

// ************************************************************
// Check if button is pressed for a short time (> 200mS) and released
// ************************************************************
boolean ClockButton::isButtonPressedAndReleased() {
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
boolean ClockButton::isButtonPressedReleased1S() {
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
boolean ClockButton::isButtonPressedReleased2S() {
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
boolean ClockButton::isButtonPressedReleased8S() {
  if (buttonPressRelease8S) {
    buttonPressRelease8S = false;
    return true;
  } else {
    return false;
  }
}

void ClockButton::checkButtonInternal(unsigned long nowMillis) {
  if (digitalRead(_inputPin) == 0) {
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

void ClockButton::resetInternal() {
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

