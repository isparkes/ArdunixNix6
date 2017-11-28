#include <IRremote.h> // Include the IRremote library

//Button release timeout
#define MAX 110

#define sensorPin 2

//Codes of Sparkfun's IR remote
const uint16_t MANUAL_OVERRIDE = 0xD827; // i.e. 0x10EFD827
const uint16_t RED_A = 0xF807;
const uint16_t GREEN_B = 0x7887;
const uint16_t BLUE_C = 0x58A7;
const uint16_t CICLE_UP = 0xA05F;
const uint16_t MAIN_BUTTON = 0x20DF;
const uint16_t MEMORY_DOWN = 0x00FF;

long lastPressed = 0; //Button release timeout counter
boolean manual; //True if a physical button is pressed
IRrecv irrecv(sensorPin); //Sets up the receiver
decode_results results; // This will store our IR received codes
uint16_t lastCode = 0; // This keeps track of the last code RX'd

void setup()
{
  //Set up PORTB as output
  DDRB = 11111111;
  
  //Set up physical button pins. Active if grounded
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
  
  // Start the receiver
  irrecv.enableIRIn(); 
}

//Codes: 127, 254, 383, 508, 636, 764

void loop()
{ 

  //If one of the physical buttons is pressed, override IR sensor until the button is released
  
  while(!digitalRead(A5)){
    manual = true;
    PORTB = B00000001;
  }

  if(manual) {
    manual = false;
    PORTB = B00000000;
  }
  
  while(!digitalRead(A4)){
    manual = true;
    PORTB = B00000010;
  }

  if(manual) {
    manual = false;
    PORTB = B00000000;
  }

  //Decode buttons
    
  if (irrecv.decode(&results)) 
  {
    /* read the RX'd IR into a 16-bit variable: */
    uint16_t resultCode = (results.value & 0xFFFF);

    /* The remote will continue to spit out 0xFFFFFFFF if a 
     button is held down. If we get 0xFFFFFFF, let's just
     assume the previously pressed button is being held down */
    if (resultCode == 0xFFFF){
      resultCode = lastCode;
      lastPressed = millis();
    } else {
      lastCode = resultCode;
      lastPressed = millis();
    }

    // This switch statement checks the received IR code against
    // all of the known codes. Each button press produces a 
    // serial output, and has an effect on the LED output.
    switch (resultCode)
    {
      case MANUAL_OVERRIDE:
      //127
        PORTB = B00000001;
        lastPressed = millis();
        break;
      case RED_A:
      //383
        PORTB = B00000011;
        lastPressed = millis();
        break;
      case GREEN_B:
      //508
        PORTB = B00000100;
        lastPressed = millis();
        break;
      case BLUE_C:
      //636
        PORTB = B00000101;
        lastPressed = millis();
        break;
      case CICLE_UP:
      //764
        PORTB = B00000110;
        lastPressed = millis();
        break;
      case MAIN_BUTTON:
      //254
        PORTB = B00000010;
        lastPressed = millis();
        break;
      case MEMORY_DOWN:
      //890
        PORTB = B00000111;
        lastPressed = millis();
        break;
      default:
        PORTB = B00000000;
        lastPressed = millis();
        break;        
    }    
    irrecv.resume(); // Receive the next value
  }

  //Release button after timeout
  if(PORTB != B00000000 && millis() - lastPressed > MAX) {
    PORTB = B00000000;
  }
}
