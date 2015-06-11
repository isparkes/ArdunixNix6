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
    http://www.open-rate.com/Downloads/NixieClockV8InstructionManual.pdf<br>
<br>
There is a kit of parts or just the PCB is currently on sale. You can get it on EBay,<br>
or for a discounted price ($45 full kit, $10 PCB) from me. The contact information is in<br>
the instruction manual or in the header above.<br>
<br>
Full Kit on EBay: http://www.ebay.com/itm/6-Digit-Nixie-Clock-Kit-Easy-Build-No-Tubes-Open-Source-Arduino-/131526796106?pt=LH_DefaultDomain_0&hash=item1e9f9ba34a<br>
PCB only on EBay: http://www.ebay.com/itm/131519325267?ssPageName=STRK:MESOX:IT&_trksid=p3984.m1561.l2649<br>
<br>
<strong>Construction and prototyping:</strong><br>
hvTest.ino: code for testing the HV generation<br>
buttonTest.ino: Code for testing button presses<br>
<br>
<br>
YouTube video of the clock in action:<br>
<br>
    https://www.youtube.com/watch?v=Js-7MJpCtvI<br>
<br>
