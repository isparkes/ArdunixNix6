int ledPin = 13; // LED connected to digital pin 13
int redLedPin = 2;
int inputPin1 = A2;   // pushbutton connected to analog pin 2
int val = 0;     // variable to store the read value

int buttonPressedCount;

void setup()
{
  pinMode(ledPin, OUTPUT); // sets the onboard led as output
  pinMode(redLedPin, OUTPUT); //sets the digital pin 2 as output
  pinMode(inputPin1, INPUT ); // set the input pin 1
  digitalWrite(inputPin1, HIGH); // set pin 1 as a pull up resistor.
}

void loop()
{
  int c = analogRead(inputPin1);
  if (c < 200) {
    digitalWrite(ledPin, 1);    // sets the LED to the button's value
  } else if (c > 200 && c < 800) {
    digitalWrite(redLedPin, 1);    // sets the LED to the button's value
  } else {
    digitalWrite(ledPin, 0);
    digitalWrite(redLedPin, 0);
  }
 
}

