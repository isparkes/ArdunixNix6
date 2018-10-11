# ArdunixNix6 / ArdunixNix4
Code for a 6-digit / 4-digit Nixie Clock based on Arduino with RAGB LED backlights

This is the main code for an Arduino based Nixie clock, intended for the following clocks:
- [Classic Rev4](https://www.nixieclock.biz/StoreClassic.html)
- [Classic Rev5](https://www.nixieclock.biz/StoreClassicRev5.html)
- [All In One IN-14](https://www.nixieclock.biz/StoreAllInOne.html)
- [Arduino All In On Shield](https://www.nixieclock.biz/StoreArduinoAllInOne.html)

It is not suitable for clocks with NeoPixels! For those you shoud be using [NixieClockFirmwareV2](https://bitbucket.org/isparkes/nixiefirmwarev2)

#### Features:
- Open source
- Wifi Interface for an ESP8266
- Real Time Clock interface for DS3231
- Digit fading with configurable fade length
- Digit scrollback with configurable scroll speed
- Configuration stored in EEPROM
- Low hardware component count (as much as possible done in code)
- Single button operation with software debounce
- Single K155ID1 for digit display (other versions use 2 or even 6!)
- Highly modular code
- RGB Back lighting
- Automatic linear dimming using light sensor


#### File Description:
- ardunixFade9_6_digit.ino: Main code for the 6 Digit Nixie Clock
- ardunixFade9_4_digit.ino: Main code for the 4 Digit Nixie Clock
- WiFiTimeProviderESP8266.ino: Time provider module code

**Instruction and User Manuals (including schematic) can be found at:** [Manuals](https://www.nixieclock.biz/Manuals.html)

You can buy this from: [https://www.nixieclock.biz/Store.html](https://www.nixieclock.biz/Store.html)

YouTube channel: [Ian's YouTube Channel](https://www.youtube.com/channel/UCiC34G8yl0mN2BK-LzPw0ew?view_as=subscriber)

