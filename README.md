# ArdunixNix6
## Nixie Clock based on Arduino

### DO _NOT_ USE THIS CODE ON THE UNMODIFIED BOARD.

_Please note:_ pin 25 (PC2) and pin 13 (PD7) must be exchanged in order to the code to work. Do NOT use the code on an unmodified board, or you may end damaging it.

Main code for an Arduino based Nixie clock. Features:                          
  - Real Time Clock interface for DS3231                                        
  - Digit fading with configurable fade length                                  
  - Digit scrollback with configurable scroll speed                             
  - Configuration stored in EEPROM                                              
  - Low hardware component count (as much as possible done in code)             
  - Single button operation with software debounce                              
  - Single 74141 for digit display (other versions use 2 or even 6!)            
  - Highly modular code                                                         
  - RGB Back lighting                                                           
  - Automatic dimming using light sensor                                        
                                                                                
  isparkes@protonmail.ch                                                        
                                                                                

ardunixFade9_6_digit.ino: Main code for the 6 Digit Nixie Clock<br>

**Instruction and User Manuals (including schematic) can be found at:**

    http://www.open-rate.com/Manuals.html

You can buy this from:

    http://www.open-rate.com/Store.html

**Construction and prototyping:** 

hvTest.ino: code for testing the HV generation
buttonTest.ino: Code for testing button presses

YouTube video of version 42 of the clock in action:

https://youtu.be/9lNWKlWbXSg

YouTube video of an early version of the clock in action:

    https://www.youtube.com/watch?v=Js-7MJpCtvI

