/**
 * ----------------------------------------------------------------------------
 * ESP32 Web Controlled Thermostat
 * ----------------------------------------------------------------------------
 * Author: Stéphane Calderoni
 * Date:   April 2020
 * ----------------------------------------------------------------------------
 * This project is a response to a request made on the RNT Lab forum:
 * https://rntlab.com/question/java-script-code-to-refresh-home-page-only-once/
 * ----------------------------------------------------------------------------
 */

#include <EEPROM.h>
#include <SPIFFS.h>
#include <DHT.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino.h>

// ----------------------------------------------------------------------------
// Macros
// ----------------------------------------------------------------------------

// LED indicators
// --------------

/**
 * We are going to use 2 LEDs:
 * - one as a WiFi network connection indicator
 * - one as an activity indicator for temperature sensor read requests from
 *   the client browser
 */

#define INIT_LED LED_BUILTIN
#define TEMP_LED GPIO_NUM_23

// DHT11 temperature sensor
// ------------------------

#define DHT_PIN  GPIO_NUM_32
#define DHT_TYPE DHT11

// ----------------------------------------------------------------------------
// Global constants
// ----------------------------------------------------------------------------

// Temperature limits
// ------------------

/**
 * The thermostat will be configured to operate within a temperature range
 * defined by a lower and an upper values that can be set by the operator.
 * The operator will be able to save these values in the EEPROM. An `INIT_FLAG`
 * backup indicator will allow the firmware to determine if this backup has
 * been performed at least once.
 * 
 * In addition, we define here the extreme values that the upper and lower
 * limits of operator-defined temperatures must not be exceeded.
 */

constexpr uint8_t INIT_FLAG = 42; // 😉 "The Hitchhiker's Guide to the Galaxy" (Douglas Adams)
constexpr int8_t  MIN_TEMP  = 10; // ideal temperatures
constexpr int8_t  MAX_TEMP  = 14; // for a wine cellar

// Definition of the 3 memory slots to be reserved in the EEPROM
// -------------------------------------------------------------

constexpr uint8_t EEPROM_SIZE    = 3;
constexpr uint8_t ADDR_INIT_FLAG = 0;
constexpr uint8_t ADDR_MIN_TEMP  = 1;
constexpr uint8_t ADDR_MAX_TEMP  = 2;

// WiFi credentials
// ----------------

constexpr char WIFI_SSID[] = "your WiFi SSID";
constexpr char WIFI_PASS[] = "your WiFi password";

// Web server listening port
// -------------------------

constexpr uint16_t HTTP_PORT = 80;

// Serial monitor
// --------------

constexpr char PREAMBLE[] = R"PREAMBLE(

-------------------------------
ESP32 Web Controlled Thermostat
-------------------------------
   © 2020 Stéphane Calderoni
-------------------------------

-------------------------------
     Initialization process
-------------------------------
)PREAMBLE";

constexpr char CLOSING[] = "\n-------------------------------\n";

// ----------------------------------------------------------------------------
// Global variables
// ----------------------------------------------------------------------------

// Temperature range supported by the thermostat
// ---------------------------------------------

struct TempRange {
    bool   initialized;
    int8_t lower;
    int8_t upper;
};

TempRange tempRange;

// Temperature sensor reading parameters
// -------------------------------------

bool readingTemperature; // -> DHT11 LED indicator
uint32_t startRead;      // -> start time of reading

// Firmware operating modules
// --------------------------

DHT dht(DHT_PIN, DHT_TYPE);       // -> DHT11 temperature sensor
AsyncWebServer server(HTTP_PORT); // -> Web server

// ----------------------------------------------------------------------------
// Initialization procedures
// ----------------------------------------------------------------------------

// Serial monitor initialization
// -----------------------------

void initSerial() {
    Serial.begin(115200);
    delay(500);
    Serial.println(PREAMBLE);
}

// LED indicator initialization
// ----------------------------

void initLEDs() {
    pinMode(INIT_LED, OUTPUT);
    pinMode(TEMP_LED, OUTPUT);
    readingTemperature = false;
    startRead = 0;
    Serial.println(F("1. LED indicators activated"));
}

// EEPROM initialization
// ---------------------

void initEEPROM() {
    Serial.print(F("2. Initializing EEPROM... "));
    if (EEPROM.begin(EEPROM_SIZE)) {
        // display of the values currently stored in the EEPROM
        Serial.print("done\n   -> [ ");
        for (uint8_t i=0; i<EEPROM_SIZE; i++) {
            Serial.printf("0x%02x => %i%s", i, (int8_t)EEPROM.readChar(i), i < EEPROM_SIZE - 1 ? " | " : " ]\n");
        }
    } else {
        Serial.println("error!");
    }
}

void initTempRange() {
    // the temperature range stored in the EEPROM is read out
    int8_t minTemp = EEPROM.readChar(ADDR_MIN_TEMP);
    int8_t maxTemp = EEPROM.readChar(ADDR_MAX_TEMP);
    // whether these values are to be taken into account
    // (only if they have already been stored in the EEPROM at least once)
    tempRange.initialized = EEPROM.readByte(ADDR_INIT_FLAG) == INIT_FLAG;
    // the temperature range to be taken over by the thermostat is deduced from this:
    tempRange.lower = tempRange.initialized ? max(minTemp, MIN_TEMP) : MIN_TEMP;
    tempRange.upper = tempRange.initialized ? min(maxTemp, MAX_TEMP) : MAX_TEMP;
    Serial.print(F("3. Temperature range set to "));
    Serial.printf("[ %i°C , %i°C ]\n", tempRange.lower, tempRange.upper);
}

// DHT11 temperature sensor initialization
// ---------------------------------------

void initTempSensor() {
    dht.begin();
    Serial.println(F("4. DHT11 temperature sensor activated"));
}

// SPIFFS initialization
// ---------------------

/**
 * The web user interface will be stored on the ESP32 Flash memory file system
 * as 5 separate files :
 * - index.html  (the interface structure)
 * - index.css   (the graphical layout of the interface)
 * - index.js    (the dynamic interface management program)
 * - D7MR.woff2  (the font used for numeric displays)
 * - favicon.ico (the tiny icon for the browser)
 */

void initSPIFFS() {
    if (!SPIFFS.begin()) {
        Serial.println(F("Cannot mount SPIFFS volume..."));
        while(1) digitalWrite(INIT_LED, millis() % 200 < 20 ? HIGH : LOW);
    }
    Serial.println(F("5. SPIFFS volume is mounted"));
}

// WiFi connection initialization
// ------------------------------

/**
 * A connection to the ambient WiFi network is required here to be able to
 * interact with an operator (who will access ESP32 through a web browser).
 */

void initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("6. Trying to connect to [%s] network ", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
    Serial.printf("\n7. Connected! => %s\n", WiFi.localIP().toString().c_str());
}

// ----------------------------------------------------------------------------
// Temperature handling
// ----------------------------------------------------------------------------

// Sensor reading
// --------------

/**
 * A temperature reading on the sensor will trigger a flash of the LED indicator.
 * It is therefore necessary to store the instant of this reading, in order to
 * later determine when the LED should turn off.
 */

float readTemperature() {
    startRead = millis();
    readingTemperature = true;
    return dht.readTemperature();
}

// Triggers
// --------

/**
 * When temperatures are outside the range allowed by the operator,
 * special actions may be desired. These must be defined here.
 * 
 * Be careful to understand, however, that in the case of this demonstration,
 * only the client browser is at the origin of the temperature control,
 * by making successive periodic requests.
 * 
 * If you want ESP32 to also monitor the temperature on its own initiative,
 * you will have to add this monitoring to the main control loop!
 */

void lowTemperatureTrigger () {
    // trigger whatever you want here...
}

void highTemperatureTrigger () {
    // trigger whatever you want here...
}

void checkForTriggers(float temp) {
    if (temp < tempRange.lower) {
        lowTemperatureTrigger();
    } else if (temp > tempRange.upper) {
        highTemperatureTrigger();
    }
}

// Temperature range backup
// ------------------------

/**
 * The lower and upper temperature limits are only saved if the current values
 * differ from the values already stored in the EEPROM. There is no need to
 * write to the EEPROM if this is not necessary.
 */

void saveTempRangeToEEPROM(int8_t lower, int8_t upper) {
    bool hasToBeSaved = false;

    // Remember that `tempRange` contains the values that were
    // read from the EEPROM during initialization.

    if (lower != tempRange.lower) {
        tempRange.lower = lower;
        EEPROM.writeChar(ADDR_MIN_TEMP, lower);
        hasToBeSaved = true;
    }

    if (upper != tempRange.upper) {
        tempRange.upper = upper;
        EEPROM.writeChar(ADDR_MAX_TEMP, upper);
        hasToBeSaved = true;
    }

    if (hasToBeSaved) {
        // If no storage has ever taken place in the EEPROM,
        // a control value is also stored so that it can be remembered
        // that it has actually taken place at least once.
        if (!tempRange.initialized) {
            EEPROM.writeByte(ADDR_INIT_FLAG, INIT_FLAG);
            tempRange.initialized = true;
        }
        // we end by actually writing in the EEPROM:
        EEPROM.commit();
        Serial.println(F("-> Has been stored in EEPROM\n"));
    } else {
        Serial.println(F("Already stored in EEPROM (no change)\n"));
    }
}

// ----------------------------------------------------------------------------
// HTTP route definition & request processing
// ----------------------------------------------------------------------------

// Processing of the `index.html` template
// ---------------------------------------

/**
 * The HTML page (index.html) that is stored in SPIFFS has generic markers
 * of the form `%TAG%`. This routine is responsible for substituting these
 * markers with the actual values that correspond to them and must be included
 * in the page that is sent to the browser.
 * 
 * There are 4 of these markers:
 * - %TEMP%       (the current temperature read by the sensor)
 * - %MIN_TEMP%   (the minimum value of the temperature range that can be set by the operator)
 * - %MAX_TEMP%   (the maximum value of the temperature range that can be set by the operator)
 * - %LOWER_TEMP% (the lower limit of the temperature range set by the operator)
 * - %UPPER_TEMP% (the upper limit of the temperature range set by the operator)
 */

String processor(const String &var)
{
    if (var == "TEMP") {
        float t = readTemperature();
        return isnan(t) ? String("Error") : String(t);
    } else if (var == "MIN_TEMP") {
        return String(MIN_TEMP);
    } else if (var == "MAX_TEMP") {
        return String(MAX_TEMP);
    } else if (var == "LOWER_TEMP") {
        return String(tempRange.lower);
    } else if (var == "UPPER_TEMP") {
        return String(tempRange.upper);
    }

    return String();
}

// Specific treatment of the root page (as a template)
// ---------------------------------------------------

/**
 * When the browser requests access to the main page `index.html`,
 * the server must first replace the generic markers declared above
 * with their respective values.
 */

void onRootRequest(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html", false, processor);
}

// Method of fallback in case no request could be resolved
// -------------------------------------------------------

void onNotFound(AsyncWebServerRequest *request) {
    request->send(404);
}

// Sensor temperature reading query manager
// ----------------------------------------

void onTemp(AsyncWebServerRequest *request) {
    Serial.println(F("Received temperature request\n-> Performs a sensor reading"));
    float temp = readTemperature();
    
    if (isnan(temp)) {
        Serial.println(F("** Failed to read from DHT sensor!\n"));
        request->send(200, "text/plain", String("Error"));
    } else {
        checkForTriggers(temp);
        Serial.print(F("-> DHT sensor readout: "));
        Serial.printf("%.1f°C\n", temp);
        Serial.println(F("-> Sends the data back to the client\n"));
        request->send(200, "text/plain", String(temp));
    }
}

// ESP32 restart request manager
// -----------------------------

void onReboot(AsyncWebServerRequest *request) {
    Serial.println(CLOSING);
    Serial.println(F("Rebooting...\n"));
    Serial.flush();
    ESP.restart();
}

// Manager for queries to define the temperature range set by the operator
// -----------------------------------------------------------------------

void onSetDefaults(AsyncWebServerRequest *request) {
    if (request->hasParam("lower") && request->hasParam("upper")) {
        int8_t lower = request->getParam("lower")->value().toInt();
        int8_t upper = request->getParam("upper")->value().toInt();
        Serial.printf("Temperature range received: [ %i°C , %i°C ]\n", lower, upper);
        saveTempRangeToEEPROM(lower, upper);
    }

    // Requests are asynchronous and must always be resolved:
    request->send(200);
}

// Definition of request handlers and server initialization
// --------------------------------------------------------

/**
 * This is where we will define the HTTP routes of the application,
 * as well as the handlers associated with each route.
 */

void initWebServer() {

    // Routes that simply return one of the files present on SPIFFS:

    /*
     * for some reason, this doesn't work:
     * -> server.serveStatic("/", SPIFFS, "/index.html");
     *
     * but this does:
     * -> server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
     *
     * and this messes up the rendering in the browser:
     * -> server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setTemplateProcessor(processor);
     *
     * So, I prefer to fall back on the classic method:
     */
    server.on("/", onRootRequest);

    server.serveStatic("/index.js",    SPIFFS, "/index.js");
    server.serveStatic("/index.css",   SPIFFS, "/index.css");
    server.serveStatic("/D7MR.woff2",  SPIFFS, "/D7MR.woff2");
    server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");

    server.onNotFound(onNotFound);

    // Routes that correspond to dynamic processing by the microcontroller:

    server.on("/temp",        onTemp);
    server.on("/boot",        onReboot);
    server.on("/setdefaults", onSetDefaults);

    // Server initialization

    server.begin();
    Serial.println(F("8. Web server started"));
}

// ----------------------------------------------------------------------------
// General initialization procedure
// ----------------------------------------------------------------------------

// Each module is initialized in turn, in a precise order:

void setup() {
    initSerial();
    initLEDs();
    initEEPROM();
    initTempRange();
    initTempSensor();
    initSPIFFS();
    initWiFi();
    initWebServer();

    Serial.println(CLOSING);
}

// ----------------------------------------------------------------------------
// LED indicator handlers
// ----------------------------------------------------------------------------

// WiFi connection indicator
// -------------------------

void flashWiFiBeacon() {
    digitalWrite(INIT_LED, millis() % 2000 < 50 ? HIGH : LOW);
}

// DHT reading indicator
// ---------------------

void flashTempBeacon() {
    if (readingTemperature) {
        readingTemperature = millis() - startRead < 50;
        digitalWrite(TEMP_LED, readingTemperature ? HIGH : LOW);
    }
}

// ----------------------------------------------------------------------------
// Main control loop
// ----------------------------------------------------------------------------

/**
 * All processing that is the responsibility of the web server is carried out
 * asynchronously. There is therefore not much to do in the main loop, except
 * to manage the operation of the LEDs.
 */

void loop() {
    flashWiFiBeacon();
    flashTempBeacon();
}