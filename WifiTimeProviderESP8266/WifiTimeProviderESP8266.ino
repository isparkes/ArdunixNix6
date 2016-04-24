/*
   ESP8266 Webserver for the Arduino Nixie clock
    - Starts the ESP8266 as an access point and provides a web interface to configure and store WiFi credentials.
    - Allows the time server to be defined and stored


   Go to http://192.168.4.1 in a web browser connected to this access point to see it
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <time.h>

#define DEBUG_OFF             // DEBUG or DEBUG_OFF

// Access point credentials, be default it is as open AP
const char *ap_ssid = "NixieTimeModule";
const char *ap_password = "";

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

  // You can add the password parameter if you want the AP to be password protected
  if (strlen(ap_password) > 0) {
    WiFi.softAP(ap_ssid, ap_password);
  } else {
    WiFi.softAP(ap_ssid);
  }

  // read eeprom for ssid and pass
  String esid = getSSIDFromEEPROM();
  String epass = getPasswordFromEEPROM();
  timeServerURL = getTimeServerURLFromEEPROM();

  // Connect
  int connectResult = connectToWLAN(esid.c_str(), epass.c_str());

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
  server.on("/scan_i2c",    i2cScanPageHandler);
  server.on("/info",        infoPageHandler);
  server.on("/time",        timeServerPageHandler);
  server.on("/reset",       resetPageHandler);
  server.on("/updatetime",  updateTimePageHandler);
  server.on("/clockconfig", clockConfigPageHandler);
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

  // Only works if we are not using the serial interface
  if ((millis() - lastMillis) > 1000) {
    lastMillis = millis();

    // See if it is time to update the Clock
    if ((millis() - lastI2CUpdateTime) > 60000) {

      // Send the time to the I2C client
      boolean result = sendTimeToI2C(getTimeFromTimeZoneServer());
      if (result) {
        lastI2CUpdateTime = millis();
      }
    }
#ifndef DEBUG
    toggleBlueLED();
#endif
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
  response_message += getTableHead2Col("Current Configuration", "Name", "Value");

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);

    response_message += getTableRow2Col("WLAN IP", ipStr);
    response_message += getTableRow2Col("AP IP", ipStrAP);
    response_message += getTableRow2Col("WLAN SSID", WiFi.SSID());
    response_message += getTableRow2Col("Time server URL", timeServerURL);
    response_message += getTableRow2Col("Time according to server", getTimeFromTimeZoneServer());
  }
  else
  {
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);
    response_message += getTableRow2Col("AP IP", ipStrAP);
  }

  // Make the uptime readable
  long upSecs = millis() / 1000;
  long upDays = upSecs / 86400;
  long upHours = (upSecs - (upDays * 86400)) / 3600;
  long upMins = (upSecs - (upDays * 86400) - (upHours * 3600)) / 60;
  upSecs = upSecs - (upDays * 86400) - (upHours * 3600) - (upMins * 60);
  String uptimeString = ""; uptimeString += upDays; uptimeString += " days, "; uptimeString += upHours, uptimeString += " hours, "; uptimeString += upMins; uptimeString += " mins, "; uptimeString += upSecs; uptimeString += " secs";

  response_message += getTableRow2Col("Uptime", uptimeString);
  response_message += getTableFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

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
      WiFi.begin(server.arg("ssid").c_str(), server.arg("password").c_str());
    }
    else
    {
      WiFi.begin(server.arg("ssid").c_str());
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

  String response_message = getHTMLHead();
  response_message += getNavBar();

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  response_message += "Set WIFI credentials";
  response_message += "</h3>";

  // Get number of visible access points
  int ap_count = WiFi.scanNetworks();

  if (ap_count == 0)
  {
    response_message += "No access points found.<br>";
  }
  else
  {
    response_message += "<form method=\"get\"><div class=\"form-group\">";

    // Show access points
    for (uint8_t ap_idx = 0; ap_idx < ap_count; ap_idx++)
    {
      response_message += "<input type=\"radio\" name=\"ssid\" value=\"" + String(WiFi.SSID(ap_idx)) + "\">";
      response_message += String(WiFi.SSID(ap_idx)) + " (RSSI: " + WiFi.RSSI(ap_idx) + ")";
      (WiFi.encryptionType(ap_idx) == ENC_TYPE_NONE) ? response_message += " " : response_message += "*";
      response_message += "<br><br>";
    }

    response_message += "WiFi password (if required):<br>";
    response_message += "<input type=\"text\" name=\"password\"><br>";
    response_message += "<input type=\"submit\" value=\"Connect\">";
    response_message += "</div></form>";
  }

  response_message += "</div>";
  response_message += "</body></html>";

  server.send(200, "text/html", response_message);
}

/**
   Scan the I2C bus - master mode looking for slaves
*/
void i2cScanPageHandler()
{
  String response_message = getHTMLHead();
  response_message += getNavBar();

  response_message += getTableHead2Col("I2C Slaves", "Addr Dec", "Addr Hex");

  for (int idx = 0 ; idx < 128 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
      String hexAddr = String(printf("%x", idx));
      String decAddr = String(printf("%d", idx));
      response_message += getTableRow2Col(decAddr, hexAddr);
    }
  }

  response_message += getTableFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

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

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Set Time Server URL</h3>";
  response_message += "<form role=\"form\"><div class=\"form-group\"><input type=\"text\" class=\"form-control input-lg\" name=\"timeserverurl\" placeholder=\"URL\"";

  // only fill in the value we have if it looks realistic
  if (timeServerURL.length() > 10) {
    response_message += " value=\"";
    response_message += timeServerURL;
    response_message += "\"";
  }

  response_message += "><span class=\"input-group-btn\"></div>";
  response_message += "<div class=\"form-group\"><div class=\"col-xs-offset-3 col-xs-9\"><input type=\"submit\" class=\"btn btn-primary\" value=\"Set\"></div></div>";
  response_message += "</form></div>";

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

/**
   Get the local time from the time server, and send it via I2C right now
*/
void updateTimePageHandler()
{
  String timeString = getTimeFromTimeZoneServer();
  boolean result = sendTimeToI2C(timeString);

  String response_message = getHTMLHead();
  response_message += getNavBar();

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";

  if (result) {
    response_message += "<div class=\"alert alert-success fade in\"><strong>Success!</strong> Update sent.</div>";
  } else {
    response_message += "<div class=\"alert alert-danger fade in\"><strong>Error!</strong> Update was not sent.</div>";
  }

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

/**
   Give info about the ESP8266
*/
void infoPageHandler() {
  String response_message = getHTMLHead();
  response_message += getNavBar();
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

/**
   Reset the EEPROM and stored values
*/
void resetPageHandler() {
  storeCredentialsInEEPROM("", "");
  storeTimeServerURLInEEPROM("");

  String response_message = getHTMLHead();
  response_message += getNavBar();
  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  response_message += "Reset done";
  response_message += "</h3></div>";
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

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
      Serial.println("I2C: Set 24h mode");
#endif
      setClockOption12H24H(true);
    }

    if ((server.arg("12h24hMode") == "12h")  && (!configHourMode)) {
#ifdef DEBUG
      Serial.println("I2C: Set 12h mode");
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
      Serial.println("I2C: Set blank leading zero");
#endif
      setClockOptionBlankLeadingZero(false);
    }

    if ((server.arg("blankLeading") == "show") && (configBlankLead)) {
#ifdef DEBUG
      Serial.println("I2C: Set show leading zero");
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
      Serial.println("I2C: Set scrollback on");
#endif
      setClockOptionScrollback(false);
    }

    if ((server.arg("useScrollback") == "off") && (configScrollback)) {
#ifdef DEBUG
      Serial.println("I2C: Set scrollback off");
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
      Serial.println("I2C: Set suppressACP on");
#endif
      setClockOptionSuppressACP(false);
    }

    if ((server.arg("suppressACP") == "off") && (configSuppressACP)) {
#ifdef DEBUG
      Serial.println("I2C: Set suppressACP off");
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
      Serial.print("I2C: Set dateFormat: "); Serial.println(newDateFormat);
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
      Serial.print("I2C: Set dayBlanking: "); Serial.println(newDayBlanking);
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
      Serial.print("I2C: Set blankFrom: "); Serial.println(newBlankFrom);
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
      Serial.print("I2C: Set blankTo: "); Serial.println(newBlankTo);
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
      Serial.print("I2C: Set fadeSteps: "); Serial.println(newFadeSteps);
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
      Serial.print("I2C: Set fadeSteps: "); Serial.println(newScrollSteps);
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
      Serial.print("I2C: Set backLight: "); Serial.println(newBacklight);
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
      Serial.print("I2C: Set redCnl: "); Serial.println(newRedCnl);
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
      Serial.print("I2C: Set grnCnl: "); Serial.println(newGreenCnl);
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
      Serial.print("I2C: Set bluCnl: "); Serial.println(newBlueCnl);
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
      Serial.print("I2C: Set cycleSpeed: "); Serial.println(newCycleSpeed);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  // Get the options, put the result into variables called "config*"
  getClockOptionsFromI2C();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += "<div class=\"container\" role=\"main\"><h3>Set Clock Configuration</h3><form class=\"form-horizontal\">";

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
  response_message += getRadioGroupHeader("Use ACP when dimmed:");
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
  response_message += getDropDownOption("1", "Blank all days on weekends", (configDayBlanking == 1));
  response_message += getDropDownOption("2", "Blank all day on week days", (configDayBlanking == 2));
  response_message += getDropDownOption("3", "Blank always", (configDayBlanking == 3));
  response_message += getDropDownOption("4", "Blank during selected hours on week days and all day at weekends", (configDayBlanking == 4));
  response_message += getDropDownOption("5", "Blank during selected hours on weekends and all day on week days", (configDayBlanking == 5));
  response_message += getDropDownOption("6", "Blank during selected hours on weekends", (configDayBlanking == 6));
  response_message += getDropDownOption("7", "Blank during selected hours on weekends", (configDayBlanking == 7));
  response_message += getDropDownOption("8", "Blank between start and end hour during week days", (configDayBlanking == 8));
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
  response_message += getDropDownOption("1", "Pulsing RGB backlight, no dimming", (configBacklightMode = 1));
  response_message += getDropDownOption("2", "Cycling RGB backlight, no dimming", (configBacklightMode = 2));
  response_message += getDropDownOption("3", "Fixed RGB backlight, dims with ambient light", (configBacklightMode = 3));
  response_message += getDropDownOption("4", "Pulsing RGB backlight, dims with ambient light", (configBacklightMode == 4));
  response_message += getDropDownOption("5", "Cycling RGB backlight, dims with ambient light", (configBacklightMode == 5));
  response_message += getDropDownFooter();

  // RGB channels
  response_message += getNumberInput("Red intensity:", "redCnl", 0, 15, configRedCnl, false);
  response_message += getNumberInput("Green intensity:", "grnCnl", 0, 15, configGreenCnl, false);
  response_message += getNumberInput("Blue intensity:", "bluCnl", 0, 15, configBlueCnl, false);

  // Cycle peed
  response_message += getNumberInput("Backlight Cycle Speed:", "cycleSpeed", 1, 64, configCycleSpeed, false);

  // form footer
  response_message += "<div class=\"form-group\"><div class=\"col-xs-offset-3 col-xs-9\"><input type=\"submit\" class=\"btn btn-primary\" value=\"Submit\"></div></div>";
  response_message += "</div></form>";

  // all done
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

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

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Network handling -----------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Try to connect to the WiFi with the given credentials. Give up after 10 seconds
   if we can't get in.
*/
int connectToWLAN(const char* ssid, const char* password) {
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
      return 1;
    }
  }

  return 0;
}

/**
   Get the local time from the time zone server. Return "ERROR" if something went wrong.
   Uses the global variable timeServerURL.
*/
String getTimeFromTimeZoneServer() {
  HTTPClient http;
  String payload = "ERROR";

  http.begin(timeServerURL);

  int httpCode = http.GET();

  if (httpCode > 0) {
    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
    }
  } else {
#ifdef DEBUG
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
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
    if (readByte != 0) {
      eurl += char(readByte);
    }
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
  Serial.print("I2C: Received bytes: ");
  Serial.println(available);
#endif
  if (available == 16) {

    byte receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got hour mode: ");
    Serial.println(receivedByte);
#endif
    configHourMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got blank lead: ");
    Serial.println(receivedByte);
#endif
    configBlankLead = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got scrollback: ");
    Serial.println(receivedByte);
#endif
    configScrollback = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got suppress ACP: ");
    Serial.println(receivedByte);
#endif
    configSuppressACP = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got date format: ");
    Serial.println(receivedByte);
#endif
    configDateFormat = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got day blanking: ");
    Serial.println(receivedByte);
#endif
    configDayBlanking = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got blank hour start: ");
    Serial.println(receivedByte);
#endif
    configBlankFrom = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got blank hour end: ");
    Serial.println(receivedByte);
#endif
    configBlankTo = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got fade steps: ");
    Serial.println(receivedByte);
#endif
    configFadeSteps = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got scroll steps: ");
    Serial.println(receivedByte);
#endif
    configScrollSteps = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got backlight mode: ");
    Serial.println(receivedByte);
#endif
    configBacklightMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got red channel: ");
    Serial.println(receivedByte);
#endif
    configRedCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got green channel: ");
    Serial.println(receivedByte);
#endif
    configGreenCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got blue channel: ");
    Serial.println(receivedByte);
#endif
    configBlueCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got cycle speed: ");
    Serial.println(receivedByte);
#endif
    configCycleSpeed = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C: Got trailer: ");
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
  Serial.print("I2C: setting boolean option: ");
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
  Serial.print("I2C: setting byte option: ");
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
  return "<!DOCTYPE html><html><head><title>Arduino Nixie Clock Time Module</title></head><body>";
}

/**
   Get the bootstrap top row navbar, including the Bootstrap links
*/
String getNavBar() {
  String navbar = "<link href=\"http://www.open-rate.com/bs336.css\" rel=\"stylesheet\" type=\"text/css\">";
  navbar += "<link href=\"http://www.open-rate.com/wl.css\" rel=\"stylesheet\" type=\"text/css\">";
  navbar += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.3/jquery.min.js\" type=\"text/javascript\"></script>";
  navbar += "<script src=\"http://www.open-rate.com/bs.js\" type=\"text/javascript\"></script>";
  navbar += "<nav class=\"navbar navbar-inverse navbar-fixed-top\">";
  navbar += "<div class=\"container-fluid\"><div class=\"navbar-header\"><button type=\"button\" class=\"navbar-toggle collapsed\" data-toggle=\"collapse\" data-target=\"#navbar\" aria-expanded=\"false\" aria-controls=\"navbar\">";
  navbar += "<span class=\"sr-only\">Toggle navigation</span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span></button>";
  navbar += "<a class=\"navbar-brand\" href=\"#\">Arduino Nixie Clock Time Module</a></div><div id=\"navbar\" class=\"navbar-collapse collapse\"><ul class=\"nav navbar-nav navbar-right\">";
  navbar += "<li><a href=\"/\">Summary</a></li><li><a href=\"/time\">Configure Time Server</a></li><li><a href=\"/wlan_config\">Configure WLAN settings</a></li><li><a href=\"/clockconfig\">Configure clock settings</a></li><li><a href=\"/scan_i2c\">Scan I2C</a></li><li><a href=\"/info\">ESP8266 Info</a></li><li><a href=\"/updatetime\">Update the time now</a></li></ul></div></div></nav>";
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
  tableHead += "<form class=\"form-horizontal\">";

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

