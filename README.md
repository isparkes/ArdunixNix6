# ArdunixNix6
Nixie Clock based on Arduino<br>
<br>
//**********************************************************************************<br>
//**********************************************************************************<br>
//* Main code for an Arduino based Nixie clock. Features:                          *<br>
//*  - Real Time Clock interface for DS3231                                        *<br>
//*  - Digit fading with configurable fade length                                  *<br>
//*  - Digit scrollback with configurable scroll speed                             *<br>
//*  - Configuration stored in EEPROM                                              *<br>
//*  - Low hardware component count (as much as possible done in code)             *<br>
//*  - Single button operation with software debounce                              *<br>
//*  - Single 74141 for digit display (other versions use 2 or even 6!)            *<br>
//*  - Highly modular code                                                         *<br>
//*  - RGB Back lighting                                                           *<br>
//*  - Automatic dimming using light sensor                                        *<br>
//*                                                                                *<br>
//*  isparkes@protonmail.ch                                                        *<br>
//*                                                                                *<br>
//**********************************************************************************<br>
//**********************************************************************************<br>
<br>
ardunixFade9_6_digit.ino: Main code for the 6 Digit Nixie Clock<br>
<br>
The instruction manual and construction guidelines are available at:<br>
    http://www.open-rate.com/Downloads/NixieClockInstructionManualRev4V41.pdf<br>
<br>
For the PCB only, PCB with programmed controller or a kit of parts see:<br>
<br>
  EBay link: http://www.ebay.com/itm/6-Digit-Nixie-Clock-Kit-Easy-Build-No-Tubes-Open-Source-Arduino-/131740553919?ssPageName=STRK:MESE:IT<br>
<br>
<br>
For a fully assembled and tests module see:<br>
<br>
  EBay link: http://www.ebay.com/itm/6-Digit-Nixie-Clock-Module-No-Tubes-Open-Source-Arduino-Built-and-tested-/131730036509?ssPageName=STRK:MESE:IT<br>
<br>
<br>
<strong>Construction and prototyping:</strong><br>
hvTest.ino: code for testing the HV generation<br>
buttonTest.ino: Code for testing button presses<br>
<br>
<br>
YouTube video of version 42 of the clock in action:<br>
<br>
https://youtu.be/9lNWKlWbXSg<br>
<br>
YouTube video of an early version of the clock in action:<br>
<br>
    https://www.youtube.com/watch?v=Js-7MJpCtvI<br>
<br>
