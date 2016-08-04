/*
   ESP8266 Webserver for the Arduino Nixie clock
    - Starts the ESP8266 as an access point and provides a web interface to configure and store WiFi credentials.
    - Allows the time server to be defined and stored

   Program with following settings (status line in IDE):

   Generic ESP8266 Module, 80MHz, 80MHz, DIO, 115200, 1M (64k SPIFFS), ck, Disabled, none

   Go to http://192.168.4.1 in a web browser connected to this access point to see it
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <time.h>

#define SOFTWARE_VERSION "1.0.2"

#define DEBUG_OFF             // DEBUG or DEBUG_OFF

// Access point credentials, be default it is as open AP
const char *ap_ssid = "NixieTimeModule";
const char *ap_password = "";

const char *serialNumber = "0x0x0x";


// Pin 1 on ESP-01, pin 2 on ESP-12E
#define blueLedPin 1
boolean blueLedState = true;

long lastMillis = 0;
long lastI2CUpdateTime = -60000;

String timeServerURL = "";

// I2C Interface definition
#define I2C_SLAVE_ADDR                0x68
#define I2C_TIME_UPDATE               0x00
#define I2C_GET_OPTIONS               0x01
#define I2C_SET_OPTION_12_24          0x02
#define I2C_SET_OPTION_BLANK_LEAD     0x03
#define I2C_SET_OPTION_SCROLLBACK     0x04
#define I2C_SET_OPTION_SUPPRESS_ACP   0x05
#define I2C_SET_OPTION_DATE_FORMAT    0x06
#define I2C_SET_OPTION_DAY_BLANKING   0x07
#define I2C_SET_OPTION_BLANK_START    0x08
#define I2C_SET_OPTION_BLANK_END      0x09
#define I2C_SET_OPTION_FADE_STEPS     0x0a
#define I2C_SET_OPTION_SCROLL_STEPS   0x0b
#define I2C_SET_OPTION_BACKLIGHT_MODE 0x0c
#define I2C_SET_OPTION_RED_CHANNEL    0x0d
#define I2C_SET_OPTION_GREEN_CHANNEL  0x0e
#define I2C_SET_OPTION_BLUE_CHANNEL   0x0f
#define I2C_SET_OPTION_CYCLE_SPEED    0x10

// Clock config
byte configHourMode;
byte configBlankLead;
byte configScrollback;
byte configSuppressACP;
byte configDateFormat;
byte configDayBlanking;
byte configBlankFrom;
byte configBlankTo;
byte configFadeSteps;
byte configScrollSteps;
byte configBacklightMode;
byte configRedCnl;
byte configGreenCnl;
byte configBlueCnl;
byte configCycleSpeed;

ESP8266WebServer server(80);

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------  Set up  --------------------------------------------
// ----------------------------------------------------------------------------------------------------
void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println();
  Serial.println("Configuring access point...");
#else
  pinMode(blueLedPin, OUTPUT);
#endif

  // Set that we are using all the 512 bytes of EEPROM
  EEPROM.begin(512);
  delay(10);

  // read eeprom for ssid and pass
  String esid = getSSIDFromEEPROM();
  String epass = getPasswordFromEEPROM();
  timeServerURL = getTimeServerURLFromEEPROM();

  // Try to connect
  boolean wlanConnected = connectToWLAN(esid.c_str(), epass.c_str());

  // If we can't connect, fall back into AP mode
  if (wlanConnected) {
#ifdef DEBUG
    Serial.println("WiFi connected, stop softAP");
#endif
    WiFi.mode(WIFI_STA);
  } else {
#ifdef DEBUG
    Serial.println("WiFi not connected, start softAP");
#endif
    WiFi.mode(WIFI_AP_STA);
    // You can add the password parameter if you want the AP to be password protected
    if (strlen(ap_password) > 0) {
      WiFi.softAP(ap_ssid, ap_password);
    } else {
      WiFi.softAP(ap_ssid);
    }
  }

  IPAddress apIP = WiFi.softAPIP();
  IPAddress myIP = WiFi.localIP();
#ifdef DEBUG
  Serial.print("AP IP address: ");
  Serial.println(apIP);
  Serial.print("IP address: ");
  Serial.println(myIP);
#endif

  Wire.begin(0, 2); // SDA = 0, SCL = 2
#ifdef DEBUG
  Serial.println("I2C master started");
#endif

  /* Set page handler functions */
  server.on("/",            rootPageHandler);
  server.on("/wlan_config", wlanPageHandler);
  server.on("/time",        timeServerPageHandler);
  server.on("/reset",       resetPageHandler);
  server.on("/updatetime",  updateTimePageHandler);
  server.on("/clockconfig", clockConfigPageHandler);
  server.on("/local.css",   localCSSHandler);
  server.onNotFound(handleNotFound);

  server.begin();

#ifdef DEBUG
  Serial.println("HTTP server started");
#endif
}

// ----------------------------------------------------------------------------------------------------
// --------------------------------------------- Main Loop --------------------------------------------
// ----------------------------------------------------------------------------------------------------
void loop()
{
  server.handleClient();

  int blinkTime;
  if (WiFi.status() == WL_CONNECTED) {
    if ((millis() - lastMillis) > 1000) {
      lastMillis = millis();
      // See if it is time to update the Clock
      if ((millis() - lastI2CUpdateTime) > 60000) {

        // Try to recover the current time
        String timeStr = getTimeFromTimeZoneServer();

        // Send the time to the I2C client
        boolean result;
        if (timeStr.substring(1,6) == "ERROR:") {
          result = false;
        } else {
          result = sendTimeToI2C(timeStr);
        }

        if (result) {
          lastI2CUpdateTime = millis();
        }
      }
#ifndef DEBUG
      toggleBlueLED();
#endif
    }
  } else {
    // offline, flash fast
    if ((millis() - lastMillis) > 100) {
      lastMillis = millis();
#ifndef DEBUG
      toggleBlueLED();
#endif
    }
  }
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- Page Handlers ------------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Root page for the webserver
*/
void rootPageHandler()
{
  String response_message = getHTMLHead();
  response_message += getNavBar();

  // Status table
  response_message += getTableHead2Col("Current Status", "Name", "Value");

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

    response_message += getTableRow2Col("WLAN IP", ipStr);
    response_message += getTableRow2Col("WLAN MAC", WiFi.macAddress());
    response_message += getTableRow2Col("WLAN SSID", WiFi.SSID());
    response_message += getTableRow2Col("Time server URL", timeServerURL);
    response_message += getTableRow2Col("Time according to server", getTimeFromTimeZoneServer());
  }
  else
  {
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);
    response_message += getTableRow2Col("AP IP", ipStrAP);
    response_message += getTableRow2Col("AP MAC", WiFi.softAPmacAddress());
  }

  // Make the uptime readable
  long upSecs = millis() / 1000;
  long upDays = upSecs / 86400;
  long upHours = (upSecs - (upDays * 86400)) / 3600;
  long upMins = (upSecs - (upDays * 86400) - (upHours * 3600)) / 60;
  upSecs = upSecs - (upDays * 86400) - (upHours * 3600) - (upMins * 60);
  String uptimeString = ""; uptimeString += upDays; uptimeString += " days, "; uptimeString += upHours, uptimeString += " hours, "; uptimeString += upMins; uptimeString += " mins, "; uptimeString += upSecs; uptimeString += " secs";

  response_message += getTableRow2Col("Uptime", uptimeString);

  String lastUpdateString = ""; lastUpdateString += (millis() - lastI2CUpdateTime);
  response_message += getTableRow2Col("Time last update", lastUpdateString);

  response_message += getTableRow2Col("Version", SOFTWARE_VERSION);
  response_message += getTableRow2Col("Serial Number", serialNumber);

  // Scan I2C bus
  for (int idx = 0 ; idx < 128 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
      response_message += getTableRow2Col("Found I2C slave at",idx);
    }
  }

  response_message += getTableFoot();

  // ESP8266 Info table
  response_message += getTableHead2Col("ESP8266 information", "Name", "Value");
  response_message += getTableRow2Col("Sketch size", ESP.getSketchSize());
  response_message += getTableRow2Col("Free sketch size", ESP.getFreeSketchSpace());
  response_message += getTableRow2Col("Free heap", ESP.getFreeHeap());
  response_message += getTableRow2Col("Boot version", ESP.getBootVersion());
  response_message += getTableRow2Col("CPU Freqency (MHz)", ESP.getCpuFreqMHz());
  response_message += getTableRow2Col("SDK version", ESP.getSdkVersion());
  response_message += getTableRow2Col("Chip ID", ESP.getChipId());
  response_message += getTableRow2Col("Flash Chip ID", ESP.getFlashChipId());
  response_message += getTableRow2Col("Flash size", ESP.getFlashChipRealSize());
  response_message += getTableRow2Col("Vcc", ESP.getVcc());
  response_message += getTableFoot();

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   WLAN page allows users to set the WiFi credentials
*/
void wlanPageHandler()
{
  // Check if there are any GET parameters, if there are, we are configuring
  if (server.hasArg("ssid"))
  {
    if (server.hasArg("password"))
    {
#ifdef DEBUG
    Serial.println("Connect WiFi");
    Serial.print("SSID:");
    Serial.println(server.arg("ssid"));
    Serial.print("PASSWORD:");
    Serial.println(server.arg("password"));
#endif
      WiFi.begin(server.arg("ssid").c_str(), server.arg("password").c_str());
    }
    else
    {
      WiFi.begin(server.arg("ssid").c_str());
#ifdef DEBUG
    Serial.println("Connect WiFi");
    Serial.print("SSID:");
    Serial.println(server.arg("ssid"));
#endif
    }

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
#ifdef DEBUG
      Serial.print(".");
#endif
    }

    storeCredentialsInEEPROM(server.arg("ssid"), server.arg("password"));

#ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("SoftAP IP address: ");
    Serial.println(WiFi.softAPIP());
#endif
  }

  String esid = getSSIDFromEEPROM();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Set Configuration");

  // Get number of visible access points
  int ap_count = WiFi.scanNetworks();

  // Day blanking
  response_message += getDropDownHeader("WiFi:", "ssid", true);

  if (ap_count == 0)
  {
    response_message += getDropDownOption("-1", "No wifi found", true);
  }
  else
  {
    // Show access points
    for (uint8_t ap_idx = 0; ap_idx < ap_count; ap_idx++)
    {
      String ssid = String(WiFi.SSID(ap_idx));
      String wlanId = String(WiFi.SSID(ap_idx));
      (WiFi.encryptionType(ap_idx) == ENC_TYPE_NONE) ? wlanId += "" : wlanId += " (requires password)";
      wlanId += " (RSSI: ";
      wlanId += String(WiFi.RSSI(ap_idx));
      wlanId += ")";

#ifdef DEBUG
    Serial.println("");
    Serial.print("found ssid: ");
    Serial.println(WiFi.SSID(ap_idx));
    Serial.print("Is current: ");
    if ((esid==ssid)) {
      Serial.println("Y");
    } else {
      Serial.println("N");
    }
#endif

      response_message += getDropDownOption(ssid, wlanId, (esid==ssid));
    }

    response_message += getDropDownFooter();

    response_message += getTextInput("WiFi password (if required)","password","",false);
    response_message += getSubmitButton("Set");

    response_message += getFormFoot();
  }

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Get the local time from the time server, and modify the time server URL if needed
*/
void timeServerPageHandler()
{
  // Check if there are any GET parameters, if there are, we are configuring
  if (server.hasArg("timeserverurl"))
  {
    if (strlen(server.arg("timeserverurl").c_str()) > 4) {
      timeServerURL = server.arg("timeserverurl").c_str();
      storeTimeServerURLInEEPROM(timeServerURL);
    }
  }

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Select time server");

  // only fill in the value we have if it looks realistic
  if ((timeServerURL.length() < 10) || (timeServerURL.length() > 250)) {
    timeServerURL = "";
  }

  response_message += getTextInputWide("URL", "timeserverurl", timeServerURL, false);
  response_message += getSubmitButton("Set");

  response_message += getFormFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Get the local time from the time server, and send it via I2C right now
*/
void updateTimePageHandler()
{
  String timeString = getTimeFromTimeZoneServer();

  String response_message = getHTMLHead();
  response_message += getNavBar();


  if (timeString.substring(1,6) == "ERROR:") {
    response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";
    response_message += "<div class=\"alert alert-danger fade in\"><strong>Error!</strong> Could not recover the time from time server. ";
    response_message += timeString;
    response_message += "</div></div>";
  } else {
    boolean result = sendTimeToI2C(timeString);

    response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";
    if (result) {
      response_message += "<div class=\"alert alert-success fade in\"><strong>Success!</strong> Update sent.</div></div>";
    } else {
      response_message += "<div class=\"alert alert-danger fade in\"><strong>Error!</strong> Update was not sent.</div></div>";
    }
  }

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Reset the EEPROM and stored values
*/
void resetPageHandler() {
  resetEEPROM();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";
  response_message += "<div class=\"alert alert-success fade in\"><strong>Success!</strong> Reset done.</div></div>";

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Page for the clock configuration.
*/
void clockConfigPageHandler()
{
  if (server.hasArg("12h24hMode"))
  {
#ifdef DEBUG
    Serial.print("Got 24h mode param: "); Serial.println(server.arg("12h24hMode"));
#endif
    if ((server.arg("12h24hMode") == "24h") && (configHourMode)) {
#ifdef DEBUG
      Serial.println("I2C --> Set 24h mode");
#endif
      setClockOption12H24H(true);
    }

    if ((server.arg("12h24hMode") == "12h")  && (!configHourMode)) {
#ifdef DEBUG
      Serial.println("I2C --> Set 12h mode");
#endif
      setClockOption12H24H(false);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankLeading"))
  {
#ifdef DEBUG
    Serial.print("Got blankLeading param: "); Serial.println(server.arg("blankLeading"));
#endif
    if ((server.arg("blankLeading") == "blank") && (!configBlankLead)) {
#ifdef DEBUG
      Serial.println("I2C --> Set blank leading zero");
#endif
      setClockOptionBlankLeadingZero(false);
    }

    if ((server.arg("blankLeading") == "show") && (configBlankLead)) {
#ifdef DEBUG
      Serial.println("I2C --> Set show leading zero");
#endif
      setClockOptionBlankLeadingZero(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("useScrollback"))
  {
#ifdef DEBUG
    Serial.print("Got useScrollback param: "); Serial.println(server.arg("useScrollback"));
#endif
    if ((server.arg("useScrollback") == "on") && (!configScrollback)) {
#ifdef DEBUG
      Serial.println("I2C --> Set scrollback on");
#endif
      setClockOptionScrollback(false);
    }

    if ((server.arg("useScrollback") == "off") && (configScrollback)) {
#ifdef DEBUG
      Serial.println("I2C --> Set scrollback off");
#endif
      setClockOptionScrollback(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("suppressACP"))
  {
#ifdef DEBUG
    Serial.print("Got suppressACP param: "); Serial.println(server.arg("suppressACP"));
#endif
    if ((server.arg("suppressACP") == "on") && (!configSuppressACP)) {
#ifdef DEBUG
      Serial.println("I2C --> Set suppressACP on");
#endif
      setClockOptionSuppressACP(false);
    }

    if ((server.arg("suppressACP") == "off") && (configSuppressACP)) {
#ifdef DEBUG
      Serial.println("I2C --> Set suppressACP off");
#endif
      setClockOptionSuppressACP(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("dateFormat")) {
#ifdef DEBUG
    Serial.print("Got dateFormat param: "); Serial.println(server.arg("dateFormat"));
#endif
    byte newDateFormat = atoi(server.arg("dateFormat").c_str());
    if (newDateFormat != configDateFormat) {
      setClockOptionDateFormat(newDateFormat);
#ifdef DEBUG
      Serial.print("I2C --> Set dateFormat: "); Serial.println(newDateFormat);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("dayBlanking")) {
#ifdef DEBUG
    Serial.print("Got dayBlanking param: "); Serial.println(server.arg("dayBlanking"));
#endif
    byte newDayBlanking = atoi(server.arg("dayBlanking").c_str());
    if (newDayBlanking != configDayBlanking) {
      setClockOptionDayBlanking(newDayBlanking);
#ifdef DEBUG
      Serial.print("I2C --> Set dayBlanking: "); Serial.println(newDayBlanking);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankFrom")) {
#ifdef DEBUG
    Serial.print("Got blankFrom param: "); Serial.println(server.arg("blankFrom"));
#endif
    byte newBlankFrom = atoi(server.arg("blankFrom").c_str());
    if (newBlankFrom != configBlankFrom) {
      setClockOptionBlankFrom(newBlankFrom);
#ifdef DEBUG
      Serial.print("I2C --> Set blankFrom: "); Serial.println(newBlankFrom);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankTo")) {
#ifdef DEBUG
    Serial.print("Got blankTo param: "); Serial.println(server.arg("blankTo"));
#endif
    byte newBlankTo = atoi(server.arg("blankTo").c_str());
    if (newBlankTo != configBlankTo) {
      setClockOptionBlankTo(newBlankTo);
#ifdef DEBUG
      Serial.print("I2C --> Set blankTo: "); Serial.println(newBlankTo);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("fadeSteps")) {
#ifdef DEBUG
    Serial.print("Got fadeSteps param: "); Serial.println(server.arg("fadeSteps"));
#endif
    byte newFadeSteps = atoi(server.arg("fadeSteps").c_str());
    if (newFadeSteps != configFadeSteps) {
      setClockOptionFadeSteps(newFadeSteps);
#ifdef DEBUG
      Serial.print("I2C --> Set fadeSteps: "); Serial.println(newFadeSteps);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("scrollSteps")) {
#ifdef DEBUG
    Serial.print("Got scrollSteps param: "); Serial.println(server.arg("scrollSteps"));
#endif
    byte newScrollSteps = atoi(server.arg("scrollSteps").c_str());
    if (newScrollSteps != configScrollSteps) {
      setClockOptionScrollSteps(newScrollSteps);
#ifdef DEBUG
      Serial.print("I2C --> Set fadeSteps: "); Serial.println(newScrollSteps);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("backLight")) {
#ifdef DEBUG
    Serial.print("Got backLight param: "); Serial.println(server.arg("backLight"));
#endif
    byte newBacklight = atoi(server.arg("backLight").c_str());
    if (newBacklight != configBacklightMode) {
      setClockOptionBacklightMode(newBacklight);
#ifdef DEBUG
      Serial.print("I2C --> Set backLight: "); Serial.println(newBacklight);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("redCnl")) {
#ifdef DEBUG
    Serial.print("Got redCnl param: "); Serial.println(server.arg("redCnl"));
#endif
    byte newRedCnl = atoi(server.arg("redCnl").c_str());
    if (newRedCnl != configRedCnl) {
      setClockOptionRedCnl(newRedCnl);
#ifdef DEBUG
      Serial.print("I2C --> Set redCnl: "); Serial.println(newRedCnl);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("grnCnl")) {
#ifdef DEBUG
    Serial.print("Got grnCnl param: "); Serial.println(server.arg("grnCnl"));
#endif
    byte newGreenCnl = atoi(server.arg("grnCnl").c_str());
    if (newGreenCnl != configGreenCnl) {
      setClockOptionGrnCnl(newGreenCnl);
#ifdef DEBUG
      Serial.print("I2C --> Set grnCnl: "); Serial.println(newGreenCnl);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("bluCnl")) {
#ifdef DEBUG
    Serial.print("Got bluCnl param: "); Serial.println(server.arg("bluCnl"));
#endif
    byte newBlueCnl = atoi(server.arg("bluCnl").c_str());
    if (newBlueCnl != configBlueCnl) {
      setClockOptionBluCnl(newBlueCnl);
#ifdef DEBUG
      Serial.print("I2C --> Set bluCnl: "); Serial.println(newBlueCnl);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("cycleSpeed")) {
#ifdef DEBUG
    Serial.print("Got cycleSpeed param: "); Serial.println(server.arg("cycleSpeed"));
#endif
    byte newCycleSpeed = atoi(server.arg("cycleSpeed").c_str());
    if (newCycleSpeed != configCycleSpeed) {
      setClockOptionCycleSpeed(newCycleSpeed);
#ifdef DEBUG
      Serial.print("I2C --> Set cycleSpeed: "); Serial.println(newCycleSpeed);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  // Get the options, put the result into variables called "config*"
  getClockOptionsFromI2C();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Set Configuration");

  // 12/24 mode
  response_message += getRadioGroupHeader("12H/24H mode:");
  if (configHourMode == 0) {
    response_message += getRadioButton("12h24hMode", " 12H", "12h", false);
    response_message += getRadioButton("12h24hMode", " 24H", "24h", true);
  } else {
    response_message += getRadioButton("12h24hMode", " 12H", "12h", true);
    response_message += getRadioButton("12h24hMode", " 24H", "24h", false);
  }
  response_message += getRadioGroupFooter();

  // blank leading
  response_message += getRadioGroupHeader("Blank leading zero:");
  if (configBlankLead) {
    response_message += getRadioButton("blankLeading", "Blank", "blank", true);
    response_message += getRadioButton("blankLeading", "Show", "show", false);
  } else {
    response_message += getRadioButton("blankLeading", "Blank", "blank", false);
    response_message += getRadioButton("blankLeading", "Show", "show", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("blankLeadingZero", "on", "Blank leading zero on hours", (configBlankLead == 1));

  // Scrollback
  response_message += getRadioGroupHeader("Scrollback effect:");
  if (configScrollback) {
    response_message += getRadioButton("useScrollback", "On", "on", true);
    response_message += getRadioButton("useScrollback", "Off", "off", false);
  } else {
    response_message += getRadioButton("useScrollback", "On", "on", false);
    response_message += getRadioButton("useScrollback", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("useScrollback", "on", "Use scrollback effect", (configScrollback == 1));

  // Suppress ACP
  response_message += getRadioGroupHeader("Suppress ACP when dimmed:");
  if (configSuppressACP) {
    response_message += getRadioButton("suppressACP", "On", "on", true);
    response_message += getRadioButton("suppressACP", "Off", "off", false);
  } else {
    response_message += getRadioButton("suppressACP", "On", "on", false);
    response_message += getRadioButton("suppressACP", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("suppressACP", "on", "Suppress ACP when fully dimmed", (configSuppressACP == 1));

  // Date format
  response_message += getDropDownHeader("Date format:", "dateFormat", false);
  response_message += getDropDownOption("0", "YY-MM-DD", (configDateFormat == 0));
  response_message += getDropDownOption("1", "MM-DD-YY", (configDateFormat == 1));
  response_message += getDropDownOption("2", "DD-MM-YY", (configDateFormat == 2));
  response_message += getDropDownFooter();

  // Day blanking
  response_message += getDropDownHeader("Day blanking:", "dayBlanking", true);
  response_message += getDropDownOption("0", "Never blank", (configDayBlanking == 0));
  response_message += getDropDownOption("1", "Blank all day on weekends", (configDayBlanking == 1));
  response_message += getDropDownOption("2", "Blank all day on week days", (configDayBlanking == 2));
  response_message += getDropDownOption("3", "Blank always", (configDayBlanking == 3));
  response_message += getDropDownOption("4", "Blank during selected hours every day", (configDayBlanking == 4));
  response_message += getDropDownOption("5", "Blank during selected hours on week days and all day on weekends", (configDayBlanking == 5));
  response_message += getDropDownOption("6", "Blank during selected hours on weekends and all day on week days", (configDayBlanking == 6));
  response_message += getDropDownOption("7", "Blank during selected hours on weekends only", (configDayBlanking == 7));
  response_message += getDropDownOption("8", "Blank during selected hours on week days only", (configDayBlanking == 8));
  response_message += getDropDownFooter();

  boolean hoursDisabled = (configDayBlanking < 4);

  // Blank hours from
  response_message += getNumberInput("Blank from:", "blankFrom", 0, 23, configBlankFrom, hoursDisabled);

  // Blank hours to
  response_message += getNumberInput("Blank to:", "blankTo", 0, 23, configBlankTo, hoursDisabled);

  // Fade steps
  response_message += getNumberInput("Fade steps:", "fadeSteps", 50, 200, configFadeSteps, false);

  // Scroll steps
  response_message += getNumberInput("Scroll steps:", "scrollSteps", 1, 40, configScrollSteps, false);

  // Back light
  response_message += getDropDownHeader("Back light:", "backLight", true);
  response_message += getDropDownOption("0", "Fixed RGB backlight, no dimming", (configBacklightMode == 0));
  response_message += getDropDownOption("1", "Pulsing RGB backlight, no dimming", (configBacklightMode == 1));
  response_message += getDropDownOption("2", "Cycling RGB backlight, no dimming", (configBacklightMode == 2));
  response_message += getDropDownOption("3", "Fixed RGB backlight, dims with ambient light", (configBacklightMode == 3));
  response_message += getDropDownOption("4", "Pulsing RGB backlight, dims with ambient light", (configBacklightMode == 4));
  response_message += getDropDownOption("5", "Cycling RGB backlight, dims with ambient light", (configBacklightMode == 5));
  response_message += getDropDownFooter();

  // RGB channels
  response_message += getNumberInput("Red intensity:", "redCnl", 0, 15, configRedCnl, false);
  response_message += getNumberInput("Green intensity:", "grnCnl", 0, 15, configGreenCnl, false);
  response_message += getNumberInput("Blue intensity:", "bluCnl", 0, 15, configBlueCnl, false);

  // Cycle peed
  response_message += getNumberInput("Backlight Cycle Speed:", "cycleSpeed", 2, 64, configCycleSpeed, false);

  // form footer
  response_message += getSubmitButton("Set");

  response_message += "</form></div>";

  // all done
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/* Called if requested page is not found */
void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

// ===================================================================================================================
// ===================================================================================================================

/* Called if we need to have a local CSS */
void localCSSHandler()
{
  String message = ".navbar,.table{margin-bottom:20px}.nav>li,.nav>li>a,article,aside,details,figcaption,figure,footer,header,hgroup,main,menu,nav,section,summary{display:block}.btn,.form-control,.navbar-toggle{background-image:none}.table,label{max-width:100%}.sub-header{padding-bottom:10px;border-bottom:1px solid #eee}.h3,h3{font-size:24px}.table{width:100%}table{background-color:transparent;border-spacing:0;border-collapse:collapse}.table-striped>tbody>tr:nth-of-type(2n+1){background-color:#f9f9f9}.table>caption+thead>tr:first-child>td,.table>caption+thead>tr:first-child>th,.table>colgroup+thead>tr:first-child>td,.table>colgroup+thead>tr:first-child>th,.table>thead:first-child>tr:first-child>td,.table>thead:first-child>tr:first-child>th{border-top:0}.table>thead>tr>th{border-bottom:2px solid #ddd}.table>tbody>tr>td,.table>tbody>tr>th,.table>tfoot>tr>td,.table>tfoot>tr>th,.table>thead>tr>td,.table>thead>tr>th{padding:8px;line-height:1.42857143;vertical-align:top;border-top:1px solid #ddd}th{text-align:left}td,th{padding:0}.navbar>.container .navbar-brand,.navbar>.container-fluid .navbar-brand{margin-left:-15px}.container,.container-fluid{padding-right:15px;padding-left:15px;margin-right:auto;margin-left:auto}.navbar-inverse .navbar-brand{color:#9d9d9d}.navbar-brand{float:left;height:50px;padding:15px;font-size:18px;line-height:20px}a{color:#337ab7;text-decoration:none;background-color:transparent}.navbar-fixed-top{border:0;top:0;border-width:0 0 1px}.navbar-inverse{background-color:#222;border-color:#080808}.navbar-fixed-bottom,.navbar-fixed-top{border-radius:0;position:fixed;right:0;left:0;z-index:1030}.nav>li,.nav>li>a,.navbar,.navbar-toggle{position:relative}.navbar{border-radius:4px;min-height:50px;border:1px solid transparent}.container{width:750px}.navbar-right{float:right!important;margin-right:-15px}.navbar-nav{float:left;margin:7.5px -15px}.nav{padding-left:0;margin-bottom:0;list-style:none}.navbar-nav>li{float:left}.navbar-inverse .navbar-nav>li>a{color:#9d9d9d}.navbar-nav>li>a{padding-top:10px;padding-bottom:10px;line-height:20px}.nav>li>a{padding:10px 15px}.navbar-inverse .navbar-toggle{border-color:#333}.navbar-toggle{display:none;float:right;padding:9px 10px;margin-top:8px;margin-right:15px;margin-bottom:8px;background-color:transparent;border:1px solid transparent;border-radius:4px}button,select{text-transform:none}button{overflow:visible}button,html input[type=button],input[type=reset],input[type=submit]{-webkit-appearance:button;cursor:pointer}.btn-primary{color:#fff;background-color:#337ab7;border-color:#2e6da4}.btn{display:inline-block;padding:6px 12px;margin-bottom:0;font-size:14px;font-weight:400;line-height:1.42857143;text-align:center;white-space:nowrap;vertical-align:middle;-ms-touch-action:manipulation;touch-action:manipulation;cursor:pointer;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;border-radius:4px}button,input,select,textarea{font-family:inherit;font-size:inherit;line-height:inherit}input{line-height:normal}button,input,optgroup,select,textarea{margin:0;font:inherit;color:inherit}.form-control,body{font-size:14px;line-height:1.42857143}.form-horizontal .form-group{margin-right:-15px;margin-left:-15px}.form-group{margin-bottom:15px}.form-horizontal .control-label{padding-top:7px;margin-bottom:0;text-align:right}.form-control{display:block;width:100%;height:34px;padding:6px 12px;color:#555;background-color:#fff;border:1px solid #ccc;border-radius:4px;-webkit-box-shadow:inset 0 1px 1px rgba(0,0,0,.075);box-shadow:inset 0 1px 1px rgba(0,0,0,.075);-webkit-transition:border-color ease-in-out .15s,-webkit-box-shadow ease-in-out .15s;-o-transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s;transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s}.col-xs-8{width:66.66666667%}.col-xs-3{width:25%}.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{float:left}.col-lg-1,.col-lg-10,.col-lg-11,.col-lg-12,.col-lg-2,.col-lg-3,.col-lg-4,.col-lg-5,.col-lg-6,.col-lg-7,.col-lg-8,.col-lg-9,.col-md-1,.col-md-10,.col-md-11,.col-md-12,.col-md-2,.col-md-3,.col-md-4,.col-md-5,.col-md-6,.col-md-7,.col-md-8,.col-md-9,.col-sm-1,.col-sm-10,.col-sm-11,.col-sm-12,.col-sm-2,.col-sm-3,.col-sm-4,.col-sm-5,.col-sm-6,.col-sm-7,.col-sm-8,.col-sm-9,.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{position:relative;min-height:1px;padding-right:15px;padding-left:15px}label{display:inline-block;margin-bottom:5px;font-weight:700}*{-webkit-box-sizing:border-box;-moz-box-sizing:border-box;box-sizing:border-box}body{font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;color:#333}html{font-size:10px;font-family:sans-serif;-webkit-text-size-adjust:100%}";

  server.send(200, "text/css", message);
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Network handling -----------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Try to connect to the WiFi with the given credentials. Give up after 10 seconds or 20 retries
   if we can't get in.
*/
boolean connectToWLAN(const char* ssid, const char* password) {
  int retries = 0;
#ifdef DEBUG
  Serial.println("Connecting to WLAN");
#endif
  if (password && strlen(password) > 0 ) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
#ifdef DEBUG
    Serial.print(".");
#endif
    retries++;
    if (retries > 20) {
      return false;
    }
  }

  return true;
}

/**
   Get the local time from the time zone server. Return the error description prefixed by "ERROR:" if something went wrong.
   Uses the global variable timeServerURL.
*/
String getTimeFromTimeZoneServer() {
  HTTPClient http;
  String payload;

  http.begin(timeServerURL);
  String espId = "";espId += ESP.getChipId();
  http.addHeader("ESP",espId);
  http.addHeader("ClientID",serialNumber);

  int httpCode = http.GET();

  if (httpCode > 0) {
    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
    }
  } else {
#ifdef DEBUG
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    payload = "ERROR: " + http.errorToString(httpCode);
#endif
  }

  http.end();

  return payload;
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------ EEPROM functions ----------------------------------------
// ----------------------------------------------------------------------------------------------------

String getSSIDFromEEPROM() {
#ifdef DEBUG
  Serial.println("Reading EEPROM ssid");
#endif
  String esid = "";
  for (int i = 0; i < 32; i++)
  {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) {
      break;
    } else if ((readByte < 32) || (readByte == 0xFF)) {
      continue;
    }
    esid += char(readByte);
  }
#ifdef DEBUG
  Serial.print("Recovered SSID: ");
  Serial.println(esid);
#endif
  return esid;
}

String getPasswordFromEEPROM() {
#ifdef DEBUG
  Serial.println("Reading EEPROM password");
#endif

  String epass = "";
  for (int i = 32; i < 96; i++)
  {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) {
      break;
    } else if ((readByte < 32) || (readByte == 0xFF)) {
      continue;
    }
    epass += char(EEPROM.read(i));
  }
#ifdef DEBUG
  Serial.print("Recovered password: ");
  Serial.println(epass);
#endif

  return epass;
}

void storeCredentialsInEEPROM(String qsid, String qpass) {
#ifdef DEBUG
  Serial.print("writing eeprom ssid, length ");
  Serial.println(qsid.length());
#endif
  for (int i = 0; i < 32; i++)
  {
    if (i < qsid.length()) {
      EEPROM.write(i, qsid[i]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(qsid[i]);
#endif
    } else {
      EEPROM.write(i, 0);
    }
  }
#ifdef DEBUG
  Serial.print("writing eeprom pass, length ");
  Serial.println(qpass.length());
#endif
  for (int i = 0; i < 96; i++)
  {
    if ( i < qpass.length()) {
      EEPROM.write(32 + i, qpass[i]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(qpass[i]);
#endif
    } else {
      EEPROM.write(32 + i, 0);
    }
  }

  EEPROM.commit();
}

String getTimeServerURLFromEEPROM() {
#ifdef DEBUG
  Serial.println("Reading time server URL");
#endif

  String eurl = "";
  for (int i = 96; i < (96 + 256); i++)
  {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) {
      break;
    } else if ((readByte < 32) || (readByte == 0xFF)) {
      continue;
    }
    eurl += char(readByte);
  }
#ifdef DEBUG
  Serial.print("Recovered time server URL: ");
  Serial.println(eurl);
  Serial.println(eurl.length());
#endif

  return eurl;
}

void storeTimeServerURLInEEPROM(String timeServerURL) {
#ifdef DEBUG
  Serial.print("writing time server URL, length ");
  Serial.println(timeServerURL.length());
#endif
  for (int i = 0; i < 256; i++)
  {
    if (i < timeServerURL.length()) {
      EEPROM.write(96 + i, timeServerURL[i]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(timeServerURL[i]);
#endif
    } else {
      EEPROM.write(96 + i, 0);
    }
  }

  EEPROM.commit();
}

void resetEEPROM() {
  for (int i = 0; i < 344; i++)
  {
    EEPROM.write(i, 0);
  }

  EEPROM.commit();
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Utility functions ----------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Switch the state of the blue LED and send it to the GPIO. Used in normal "heartbeat" processing and to show processing.
*/
void toggleBlueLED() {
  blueLedState = ! blueLedState;
  digitalWrite(blueLedPin, blueLedState);
}

/**
   Split a string based on a separator, get the element given by index
*/
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

/**
   Split a string based on a separator, get the element given by index, return an integer value
*/
int getIntValue(String data, char separator, int index) {
  String result = getValue(data, separator, index);
  return atoi(result.c_str());
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- I2C functions ------------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Send the time to the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean sendTimeToI2C(String timeString) {

  int year = getIntValue(timeString, ',', 0);
  byte month = getIntValue(timeString, ',', 1);
  byte day = getIntValue(timeString, ',', 2);
  byte hour = getIntValue(timeString, ',', 3);
  byte minute = getIntValue(timeString, ',', 4);
  byte sec = getIntValue(timeString, ',', 5);

  byte yearAdjusted = (year - 2000);
  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(I2C_TIME_UPDATE); // Command
  Wire.write(yearAdjusted);
  Wire.write(month);
  Wire.write(day);
  Wire.write(hour);
  Wire.write(minute);
  Wire.write(sec);
  int error = Wire.endTransmission();
  return (error == 0);
}

/**
   Get the options from the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean getClockOptionsFromI2C() {
  int available = Wire.requestFrom(I2C_SLAVE_ADDR, 16);

#ifdef DEBUG
  Serial.print("I2C <-- Received bytes: ");
  Serial.println(available);
#endif
  if (available == 16) {

    byte receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got hour mode: ");
    Serial.println(receivedByte);
#endif
    configHourMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blank lead: ");
    Serial.println(receivedByte);
#endif
    configBlankLead = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got scrollback: ");
    Serial.println(receivedByte);
#endif
    configScrollback = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got suppress ACP: ");
    Serial.println(receivedByte);
#endif
    configSuppressACP = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got date format: ");
    Serial.println(receivedByte);
#endif
    configDateFormat = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got day blanking: ");
    Serial.println(receivedByte);
#endif
    configDayBlanking = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blank hour start: ");
    Serial.println(receivedByte);
#endif
    configBlankFrom = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blank hour end: ");
    Serial.println(receivedByte);
#endif
    configBlankTo = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got fade steps: ");
    Serial.println(receivedByte);
#endif
    configFadeSteps = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got scroll steps: ");
    Serial.println(receivedByte);
#endif
    configScrollSteps = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got backlight mode: ");
    Serial.println(receivedByte);
#endif
    configBacklightMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got red channel: ");
    Serial.println(receivedByte);
#endif
    configRedCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got green channel: ");
    Serial.println(receivedByte);
#endif
    configGreenCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blue channel: ");
    Serial.println(receivedByte);
#endif
    configBlueCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got cycle speed: ");
    Serial.println(receivedByte);
#endif
    configCycleSpeed = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got trailer: ");
    Serial.println(receivedByte);
#endif
  }
  int error = Wire.endTransmission();
  return (error == 0);
}

boolean setClockOption12H24H(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_12_24, newMode);
}

boolean setClockOptionBlankLeadingZero(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_BLANK_LEAD, newMode);
}

boolean setClockOptionScrollback(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_SCROLLBACK, newMode);
}

boolean setClockOptionSuppressACP(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_SUPPRESS_ACP, newMode);
}

boolean setClockOptionDateFormat(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_DATE_FORMAT, newMode);
}

boolean setClockOptionDayBlanking(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_DAY_BLANKING, newMode);
}

boolean setClockOptionBlankFrom(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLANK_START, newMode);
}

boolean setClockOptionBlankTo(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLANK_END, newMode);
}

boolean setClockOptionFadeSteps(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_FADE_STEPS, newMode);
}

boolean setClockOptionScrollSteps(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_SCROLL_STEPS, newMode);
}

boolean setClockOptionBacklightMode(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BACKLIGHT_MODE, newMode);
}

boolean setClockOptionRedCnl(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_RED_CHANNEL, newMode);
}

boolean setClockOptionGrnCnl(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_GREEN_CHANNEL, newMode);
}

boolean setClockOptionBluCnl(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLUE_CHANNEL, newMode);
}

boolean setClockOptionCycleSpeed(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_CYCLE_SPEED, newMode);
}

/**
   Send the options from the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean setClockOptionBoolean(byte option, boolean newMode) {
#ifdef DEBUG
  Serial.print("I2C --> setting boolean option: ");
  Serial.print(option);
  Serial.print(" with value: ");
  Serial.println(newMode);
#endif

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(option);
  byte newOption;
  if (newMode) {
    newOption = 0;
  } else {
    newOption = 1;
  }
  Wire.write(newOption);
  int error = Wire.endTransmission();
  delay(10);
  return (error == 0);
}

/**
   Send the options from the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean setClockOptionByte(byte option, byte newMode) {
#ifdef DEBUG
  Serial.print("I2C --> setting byte option: ");
  Serial.print(option);
  Serial.print(" with value: ");
  Serial.println(newMode);
#endif

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(option);
  Wire.write(newMode);
  int error = Wire.endTransmission();
  delay(10);
  return (error == 0);
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- HTML functions -----------------------------------------
// ----------------------------------------------------------------------------------------------------

String getHTMLHead() {
  String header = "<!DOCTYPE html><html><head>";

  if (WiFi.status() == WL_CONNECTED) {
    header += "<link href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.6/css/bootstrap.min.css\" rel=\"stylesheet\" integrity=\"sha384-1q8mTJOASx8j1Au+a5WDVnPi2lkFfwwEAa8hDDdjZlpLegxhjVME1fgjWPGmkzs7\" crossorigin=\"anonymous\">";
    header += "<link href=\"http://www.open-rate.com/wl.css\" rel=\"stylesheet\" type=\"text/css\">";
    header += "<script src=\"http://code.jquery.com/jquery-1.12.3.min.js\" integrity=\"sha256-aaODHAgvwQW1bFOGXMeX+pC4PZIPsvn2h1sArYOhgXQ=\" crossorigin=\"anonymous\"></script>";
    header += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.6/js/bootstrap.min.js\" integrity=\"sha384-0mSbJDEHialfmuBBQP6A4Qrprq5OVfW37PRR3j5ELqxss1yVqOtnepnHVP9aJ7xS\" crossorigin=\"anonymous\"></script>";
  } else {
    header += "<link href=\"local.css\" rel=\"stylesheet\">";
  }
  header += "<title>Arduino Nixie Clock Time Module</title></head>";
  header += "<body>";
  return header;
}

/**
   Get the bootstrap top row navbar, including the Bootstrap links
*/
String getNavBar() {
  String navbar = "<nav class=\"navbar navbar-inverse navbar-fixed-top\">";
  navbar += "<div class=\"container-fluid\"><div class=\"navbar-header\"><button type=\"button\" class=\"navbar-toggle collapsed\" data-toggle=\"collapse\" data-target=\"#navbar\" aria-expanded=\"false\" aria-controls=\"navbar\">";
  navbar += "<span class=\"sr-only\">Toggle navigation</span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span></button>";
  navbar += "<a class=\"navbar-brand\" href=\"#\">Arduino Nixie Clock Time Module</a></div><div id=\"navbar\" class=\"navbar-collapse collapse\"><ul class=\"nav navbar-nav navbar-right\">";
  navbar += "<li><a href=\"/\">Summary</a></li><li><a href=\"/time\">Configure Time Server</a></li><li><a href=\"/wlan_config\">Configure WLAN settings</a></li><li><a href=\"/clockconfig\">Configure clock settings</a></li></ul></div></div></nav>";
  return navbar;
} 

/**
   Get the header for a 2 column table
*/
String getTableHead2Col(String tableHeader, String col1Header, String col2Header) {
  String tableHead = "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  tableHead += tableHeader;
  tableHead += "</h3><div class=\"table-responsive\"><table class=\"table table-striped\"><thead><tr><th>";
  tableHead += col1Header;
  tableHead += "</th><th>";
  tableHead += col2Header;
  tableHead += "</th></tr></thead><tbody>";

  return tableHead;
}

String getTableRow2Col(String col1Val, String col2Val) {
  String tableRow = "<tr><td>";
  tableRow += col1Val;
  tableRow += "</td><td>";
  tableRow += col2Val;
  tableRow += "</td></tr>";

  return tableRow;
}

String getTableRow2Col(String col1Val, int col2Val) {
  String tableRow = "<tr><td>";
  tableRow += col1Val;
  tableRow += "</td><td>";
  tableRow += col2Val;
  tableRow += "</td></tr>";

  return tableRow;
}

String getTableFoot() {
  return "</tbody></table></div></div>";
}

/**
   Get the header for an input form
*/
String getFormHead(String formTitle) {
  String tableHead = "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  tableHead += formTitle;
  tableHead += "</h3><form class=\"form-horizontal\">";

  return tableHead;
}

/**
   Get the header for an input form
*/
String getFormFoot() {
  return "</form></div>";
}

String getHTMLFoot() {
  return "</body></html>";
}

String getRadioGroupHeader(String header) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\">";
  result += header;
  result += "</label>";
  return result;
}

String getRadioButton(String group_name, String text, String value, boolean checked) {
  String result = "<div class=\"col-xs-1\">";
  if (checked) {
    result += "<label class=\"radio-inline\"><input checked type=\"radio\" name=\"";
  } else {
    result += "<label class=\"radio-inline\"><input type=\"radio\" name=\"";
  }
  result += group_name;
  result += "\" value=\"";
  result += value;
  result += "\"> ";
  result += text;
  result += "</label></div>";
  return result;
}

String getRadioGroupFooter() {
  String result = "</div>";
  return result;
}

String getCheckBox(String checkbox_name, String value, String text, boolean checked) {
  String result = "<div class=\"form-group\"><div class=\"col-xs-offset-3 col-xs-9\"><label class=\"checkbox-inline\">";
  if (checked) {
    result += "<input checked type=\"checkbox\" name=\"";
  } else {
    result += "<input type=\"checkbox\" name=\"";
  }

  result += checkbox_name;
  result += "\" value=\"";
  result += value;
  result += "\"> ";
  result += text;
  result += "</label></div></div>";

  return result;
}

String getDropDownHeader(String heading, String group_name, boolean wide) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\">";
  result += heading;
  if (wide) {
    result += "</label><div class=\"col-xs-8\"><select class=\"form-control\" name=\"";
  } else {
    result += "</label><div class=\"col-xs-2\"><select class=\"form-control\" name=\"";
  }
  result += group_name;
  result += "\">";
  return result;
}

String getDropDownOption (String value, String text, boolean checked) {
  String result = "";
  if (checked) {
    result += "<option selected value=\"";
  } else {
    result += "<option value=\"";
  }
  result += value;
  result += "\">";
  result += text;
  result += "</option>";
  return result;
}

String getDropDownFooter() {
  return "</select></div></div>";
}

String getNumberInput(String heading, String input_name, byte minVal, byte maxVal, byte value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-2\"><input type=\"number\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" min=\"";
  result += minVal;
  result += "\" max=\"";
  result += maxVal;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getNumberInputWide(String heading, String input_name, byte minVal, byte maxVal, byte value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-8\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-2\"><input type=\"number\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" min=\"";
  result += minVal;
  result += "\" max=\"";
  result += maxVal;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getTextInput(String heading, String input_name, String value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-2\"><input type=\"text\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getTextInputWide(String heading, String input_name, String value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-8\"><input type=\"text\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getSubmitButton(String buttonText) {
  String result = "<div class=\"form-group\"><div class=\"col-xs-offset-3 col-xs-9\"><input type=\"submit\" class=\"btn btn-primary\" value=\"";
  result += buttonText;
  result += "\"></div></div>";
  return result;
}

