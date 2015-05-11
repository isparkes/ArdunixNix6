//**********************************************************************************
//**********************************************************************************
//*  Fire up HV generator for testing                                              *
//*  Uses a feedback loop via the analogue in to make sure that we produce more or *
//*  less constantly 200V, regardless of the input voltage.                        *
//*  We achieve this by turning off the HV generator if the voltage we measure     *
//*  is above the required voltage, otherwise we turn it on.                       *
//**********************************************************************************
//**********************************************************************************


#include <avr/io.h> 
#include <avr/interrupt.h> 
#include <EEPROM.h>


//**********************************************************************************
//**********************************************************************************
//*                                     Variables                                  *
//**********************************************************************************
//**********************************************************************************
// The divider value determines the frequency of the DC-DC converter
// The output voltage depends on the input voltage and the frequency:
// For 5V input:
// at a divider of 200, the frequency is 16MHz / 200 / 2 (final /2 because of toggle mode)
// Ranges for this are around 800 - 100, with 400 (20KHz) giving about 180V
// Drive up to 100 (80KHz) giving around 200V
int divider = 200;

// pin used to drive the DC-DC converter
int hvDriverPin = 9;
int tickLed = 13;
int sensorPin = A0;    // select the input pin for the potentiometer

// precalculated values for turning on and off the HV generator
// Put these in TCCR1B to turn off and on
int tccrOff;
int tccrOn;


//**********************************************************************************
//**********************************************************************************
//*                                                          Setup                                                                *
//**********************************************************************************
//**********************************************************************************
void setup() 
{
  pinMode(tickLed, OUTPUT);     

  // Set the driver pin to putput
  pinMode(hvDriverPin, OUTPUT);

  /* disable global interrupts */
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

  /* enable global interrupts */
  sei();
}

//**********************************************************************************
//**********************************************************************************
//*                                                       Main loop                                                             *
//**********************************************************************************
//**********************************************************************************
void loop()     
{
  int rawSensorVal = analogRead(sensorPin);  
  double sensorVoltage = rawSensorVal * 5.0  / 1024.0;
  double externalVoltage = sensorVoltage * 394.7 / 4.7;
  //Serial.println(externalVoltage);

  if (externalVoltage > 180) {
    TCCR1A = tccrOff;
    digitalWrite(tickLed,0);
  } 
  else {
    TCCR1A = tccrOn;
    digitalWrite(tickLed,1);
  }
}




