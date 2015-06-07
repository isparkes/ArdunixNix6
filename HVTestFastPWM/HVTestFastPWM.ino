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

//**********************************************************************************
//**********************************************************************************
//*                                     Variables                                  *
//**********************************************************************************
//**********************************************************************************
const int PWM_TOP_DEFAULT = 1000;
const int PWM_TOP_MIN = 200;
const int PWM_TOP_MAX = 10000;
int pwmTop = PWM_TOP_DEFAULT;

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

  // Set the driver pin to output
  pinMode(9, OUTPUT);

  TCCR1A = 0;    // disable all PWM on Timer1 whilst we set it up
  TCCR1B = 0;    // disable all PWM on Timer1 whilst we set it up
  ICR1 = pwmTop; // Our starting point for the period	

  // Configure timer 1 for Fast PWM mode via ICR1, with prescaling=1
  TCCR1A = (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1<<WGM12) | (1 << CS10);

  tccrOff = TCCR1A;
  
  TCCR1A |= (1 <<  COM1A1);  // enable PWM on port PD4 in non-inverted compare mode 2
 
  tccrOn = TCCR1A;
  
  OCR1A = 150;  // Pulse width of the on time
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
    pwmTop++;
    if (pwmTop > PWM_TOP_MAX) {
      pwmTop = PWM_TOP_MAX;
    }
    ICR1 = pwmTop; // Our starting point for the period	
    digitalWrite(tickLed,0);
  } 
  else {
    pwmTop--;
    if (pwmTop < PWM_TOP_MIN) {
      pwmTop = PWM_TOP_MIN;
    }
    ICR1 = pwmTop; // Our starting point for the period	
    digitalWrite(tickLed,1);
  }
}




