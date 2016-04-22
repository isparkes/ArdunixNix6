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
#define I2C_SLAVE_ADDR 0x68
#define I2C_TIME_UPDATE 0x00        // takes 6 bytes of arguments yy,mm,dd,HH,MM,ss


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
      if(result) {
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
  response_message += getTableHead2Col("Current Configuration","Name","Value");

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);

    response_message += getTableRow2Col("WLAN IP",ipStr);
    response_message += getTableRow2Col("AP IP",ipStrAP);
    response_message += getTableRow2Col("WLAN SSID",WiFi.SSID());
    response_message += getTableRow2Col("Time server URL",timeServerURL);
    response_message += getTableRow2Col("Time according to server",getTimeFromTimeZoneServer());
  }
  else
  {
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);
    response_message += getTableRow2Col("AP IP",ipStrAP);
  }

  // Make the uptime readable
  long upSecs = millis() / 1000;
  long upDays = upSecs / 86400;
  long upHours = (upSecs - (upDays * 86400)) / 3600;
  long upMins = (upSecs - (upDays * 86400) - (upHours * 3600)) / 60;
  upSecs = upSecs - (upDays*86400) - (upHours*3600) - (upMins*60); 
  String uptimeString = ""; uptimeString += upDays; uptimeString += " days, "; uptimeString += upHours, uptimeString += " hours, "; uptimeString += upMins; uptimeString += " mins, "; uptimeString += upSecs; uptimeString += " secs";

  response_message += getTableRow2Col("Uptime",uptimeString);
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

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  response_message += "Scan I2C bus for slaves";
  response_message += "</h3>";

  for (int idx = 0 ; idx < 128 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
      response_message += "<center>Found I2C slave at ";
      response_message += idx;
      response_message += "</center><br>";
    }
  }
  response_message += "<br></div>";
  response_message += "</body></html>";


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
  if (timeServerURL.length() > 10) {
    response_message += " value=\"";
    response_message += timeServerURL;
    response_message += "\"";
  }
  response_message += "><span class=\"input-group-btn\"></div>";
  response_message += "<input type=\"submit\" value=\"Set\" class=\"btn btn-default\">";
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

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  response_message += "Send time to I2C right now";
  response_message += "</h3>";

  response_message += "<center>got time string ";
  response_message += timeString;
  response_message += "</center><br>";

  response_message += "<center>update result ";
  if (result) {
    response_message += "OK";
  } else {
    response_message += "Error";
  }
  response_message += "</center><br>";

  response_message += "</div>";
  response_message += "</body></html>";

  server.send(200, "text/html", response_message);
}
/**
   Give info about the ESP8266
*/
void infoPageHandler() {
  String response_message = getHTMLHead();
  response_message += getNavBar();
  response_message += getTableHead2Col("ESP8266 information","Name","Value");
  response_message += getTableRow2Col("Sketch size",ESP.getSketchSize());
  response_message += getTableRow2Col("Free sketch size",ESP.getFreeSketchSpace());
  response_message += getTableRow2Col("Free heap",ESP.getFreeHeap());
  response_message += getTableRow2Col("Boot version",ESP.getBootVersion());
  response_message += getTableRow2Col("CPU Freqency (MHz)",ESP.getCpuFreqMHz());
  response_message += getTableRow2Col("SDK version",ESP.getSdkVersion());
  response_message += getTableRow2Col("Chip ID",ESP.getChipId());
  response_message += getTableRow2Col("Flash Chip ID",ESP.getFlashChipId());
  response_message += getTableRow2Col("Flash size",ESP.getFlashChipRealSize());
  response_message += getTableRow2Col("Vcc",ESP.getVcc());
  response_message += getTableFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

/**
   Reset the EEPROM and stored values
*/
void resetPageHandler() {
  storeCredentialsInEEPROM("","");
  storeTimeServerURLInEEPROM("");
  
  String response_message = getHTMLHead();
  response_message += getNavBar();
  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  response_message += "Reset done";
  response_message += "</h3></div>";
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
// ------------------------------------------- HTML functions -----------------------------------------
// ----------------------------------------------------------------------------------------------------

String getHTMLHead() {
  return "<html><head><title>Arduino Nixie Clock Time Module</title></head><body>";
}

/**
 * Get the bootstrap top row navbar, including the Bootstrap links
 */
String getNavBar() {
  String navbar = "<link href=\"http://www.open-rate.com/bs336.css\" rel=\"stylesheet\">";
  navbar += "<link href=\"http://www.open-rate.com/wl.css\" rel=\"stylesheet\">";
  navbar += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.3/jquery.min.js\"></script>";
  navbar += "<script src=\"http://www.open-rate.com/bs.js\"></script>";
  navbar += "<nav class=\"navbar navbar-inverse navbar-fixed-top\">";
  navbar += "<div class=\"container-fluid\"><div class=\"navbar-header\"><button type=\"button\" class=\"navbar-toggle collapsed\" data-toggle=\"collapse\" data-target=\"#navbar\" aria-expanded=\"false\" aria-controls=\"navbar\">";
  navbar += "<span class=\"sr-only\">Toggle navigation</span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span></button>";
  navbar += "<a class=\"navbar-brand\" href=\"#\">Arduino Nixie Clock Time Module</a></div><div id=\"navbar\" class=\"navbar-collapse collapse\"><ul class=\"nav navbar-nav navbar-right\">";
  navbar += "<li><a href=\"/\">Summary</a></li><li><a href=\"/time\">Configure Time Server</a></li><li><a href=\"/wlan_config\">Configure WLAN settings</a></li><li><a href=\"/scan_i2c\">Scan I2C</a></li><li><a href=\"/info\">ESP8266 Info</a></li><li><a href=\"/updatetime\">Update the time now</a></li></ul></div></div></nav>";
  return navbar;
}

/**
 * Get the header for a 2 column table
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
  return "</div></tbody></table>";
}
  
/**
 * Get the header for an input form
 */
String getFormHead(String formTitle) {
  String tableHead = "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  tableHead += formTitle;
  tableHead += "<form class=\"form-horizontal\">";
  
  return tableHead;
}

/**
 * Get the header for an input form
 */
String getFormFoot() {
  return "</form></div>";
}

String getHTMLFoot() {
  return "</body></html>";
}

