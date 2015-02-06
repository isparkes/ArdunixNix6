int ledPin = 13; // LED connected to digital pin 13
int inputPin1 = 7;   // pushbutton connected to digital pin 7
int val = 0;     // variable to store the read value

int buttonPressedCount;

void setup()
{
  pinMode(ledPin, OUTPUT);      // sets the digital pin 13 as output
  pinMode(inputPin1, INPUT ); // set the input pin 1
  digitalWrite(inputPin1, HIGH); // set pin 1 as a pull up resistor.
}

void loop()
{
  if (digitalRead(inputPin1) == 1) {
      if (buttonPressedCount < 10) { 
        buttonPressedCount += 1;
      }
  } else {
      buttonPressedCount = 0;
  }
  
  if (buttonPressedCount == 10) { 
    digitalWrite(ledPin, 1);    // sets the LED to the button's value
  } else {
    digitalWrite(ledPin, 0);    // sets the LED to the button's value
  }
  
  delay(100);
}

