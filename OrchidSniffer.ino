/*
*******************************************************************************
* Description: A socket client of the AIS that reads NMEA data from AIS, parses & displays
*******************************************************************************
* Last modified: 12/15/22 by P. Bouchier
*/
#include "secrets.h"
#include "M5StickCPlus.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <string.h> // For strtok_r, strncpy, strcmp
#include <stdlib.h> // For atof
// #include <EEPROM.h> // Removed as EEPROM logic is removed

#define MSG_BUFFER_SIZE (50)

// library objects
WiFiClient mqttServerClient;
PubSubClient mqttClient(mqttServerClient);

// Configure the name and password of the connected wifi and your MQTT Serve
// host.
const char* mqttServer = "192.168.1.116";

// send test message buffers
unsigned long lastMsg = 0;
char msg[MSG_BUFFER_SIZE];
int value = 0;

// New MQTT data variables
char udoo_time[9] = "00:00:00"; // HH:MM:SS
float TWD = 0.0;
float TWS = 0.0;
float COG = 0.0;
float SOG = 0.0;
float Cabin = 0.0; // New: Cabin temperature
float Cockpit = 0.0; // New: Cockpit temperature

// display modes
enum DisplayMode {
    DISPLAY_WIND_SPEED, // First (default) variable to display
    DISPLAY_WIND_DIR,
    DISPLAY_BOAT_SPEED, // SOG
    DISPLAY_BOAT_HEADING, // COG
    DISPLAY_COCKPIT_TEMP, // New: Cockpit temperature
    DISPLAY_TIME,
    DISPLAY_MODE_LAST
};

// UI data
int displayMode = DISPLAY_WIND_SPEED; // Start with wind speed
bool buttonA = false;

// Power consumption data
int bat;
int charge;
int disCharge;
double vbat = 0.0;
unsigned long shutdownTime;
int shutdownDelaySec = 300; // duration without button push after which shutdown starts
int cancelDelaySec = 10;     // duration when pushing a button cancels shutdown

void setupWifi() {
    delay(10);
    M5.Lcd.printf("Connecting to %s", ssid);
    WiFi.mode(
        WIFI_STA);  // Set the mode to WiFi station mode.
    WiFi.begin(ssid, password);  // Start Wifi connection.

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        M5.Lcd.print(".");
    }
    M5.Lcd.printf("\nSuccess: Wifi connected\n");
    delay(2000);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // This function is called when an MQTT message is received.
    // It parses the incoming message to extract navigation data.
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    char message[length + 1];
    strncpy(message, (char*)payload, length);
    message[length] = '\0'; // Null-terminate the string received from MQTT.

    Serial.println(message);

    // Check if the message is a reset command.
    if (strcmp(message, "MQ_reset") == 0) {
        Serial.println("MQ_reset received");
        // In a real application, this might trigger a system reset (e.g., ESP.restart()).
        // For now, it just acknowledges the reset command.
    } else {
        // Parse comma-separated data from the MQTT message.
        // The data format is: udoo_time, TWD, TWS, COG, SOG, Cabin, Cockpit
        // strtok_r is used for safe re-entrant tokenization of the string.
        char* token;
        char* rest = message;

        // Extract udoo_time (HH:MM:SS string).
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            strncpy(udoo_time, token, sizeof(udoo_time) - 1);
            udoo_time[sizeof(udoo_time) - 1] = '\0'; // Ensure null-termination.
        }

        // Extract TWD (True Wind Direction) as a float.
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            TWD = atof(token); // Convert string to float.
        }

        // Extract TWS (True Wind Speed) as a float.
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            TWS = atof(token); // Convert string to float.
        }

        // Extract COG (Course Over Ground) as a float.
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            COG = atof(token); // Convert string to float.
        }

        // Extract SOG (Speed Over Ground) as a float.
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            SOG = atof(token); // Convert string to float.
        }
        // Ignoring Cabin and Cockpit fields as per project instructions.
        // Re-enabling parsing for Cabin and Cockpit as per new requirements.

        // Extract Cabin temperature as a float.
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            Cabin = atof(token); // Convert string to float.
        }

        // Extract Cockpit temperature as a float.
        token = strtok_r(rest, ",", &rest);
        if (token != NULL) {
            Cockpit = atof(token); // Convert string to float.
        }

        // Print the parsed data to the Serial monitor for debugging.
        Serial.printf("Parsed Data: Time=%s, TWD=%.1f, TWS=%.1f, COG=%.1f, SOG=%.1f, Cabin=%.1f, Cockpit=%.1f\n",
                      udoo_time, TWD, TWS, COG, SOG, Cabin, Cockpit);
    }
}

void mqttConnect() {
    while (!mqttClient.connected()) {
        M5.Lcd.print("Attempting MQTT connection...");
        // Create a random client ID.
        String clientId = "M5Stack-";
        clientId += String(random(0xffff), HEX);
        // Attempt to connect.
        if (mqttClient.connect(clientId.c_str())) {
            M5.Lcd.printf("\nSuccess\n");
            // Once connected, subscribe to the timezone topic
            mqttClient.subscribe("Time");

        } else {
            M5.Lcd.print("failed, rc=");
            M5.Lcd.print(mqttClient.state());
            M5.Lcd.println("try again in 5 seconds");
            delay(5000);
        }
    }
}

void resetShutdownTimer(int delaySec)
{
    shutdownTime = millis() + (delaySec * 1000);
}

void setup() {
    M5.begin();
    Serial.begin(115200);
    M5.Lcd.setRotation(1);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.setCursor(0, 0, 2);
    
    setupWifi();
    
    // configure MQTT client
    mqttClient.setServer(mqttServer, 1883);
    mqttClient.setCallback(mqttCallback);

    resetShutdownTimer(shutdownDelaySec);  // initialize shutdown timer

    delay(1000);  // give time to read msgs
}

void loop() {
    if (!mqttClient.connected())
        mqttConnect();

    // loop() is called periodically to allow clients to process incoming
    // messages and maintain connections to the server
    mqttClient.loop();

    // Removed readSocket() call as AIS is no longer used
    // readSocket();

    if (buttonA == true)
    {
        Serial.println("Button A pushed");
        buttonA = false;
        displayMode += 1;
        if (displayMode == DISPLAY_MODE_LAST)
            displayMode = DISPLAY_WIND_SPEED; // Changed from DISPLAY_TIME to DISPLAY_WIND_SPEED
        resetShutdownTimer(shutdownDelaySec);
    }
     
    M5.Lcd.fillScreen(BLACK);

        if (displayMode == DISPLAY_WIND_SPEED)
        {
            M5.Lcd.setCursor(0, 0, 4);
            M5.Lcd.print("TWS");
    
            M5.Lcd.setCursor(0, 40, 8);
            M5.Lcd.printf("%0.1f", TWS);
    
            M5.Lcd.setCursor(200, 40, 4);
            M5.Lcd.print("kt");
        }
        else if (displayMode == DISPLAY_WIND_DIR)
        {
            M5.Lcd.setCursor(0, 0, 4);
            M5.Lcd.print("TWD");
    
            M5.Lcd.setCursor(0, 40, 8);
            M5.Lcd.printf("%.0f deg\n", TWD);
    
            M5.Lcd.setCursor(180, 40, 4);
            M5.Lcd.print("deg");
        }
        else if (displayMode == DISPLAY_BOAT_SPEED)
        {
            M5.Lcd.setCursor(0, 0, 4);
            M5.Lcd.print("SOG");
    
            M5.Lcd.setCursor(0, 40, 8);
            M5.Lcd.printf("%0.1f", SOG);
    
            M5.Lcd.setCursor(200, 40, 4);
            M5.Lcd.print("kt");
        }
        else if (displayMode == DISPLAY_BOAT_HEADING)
        {
            M5.Lcd.setCursor(0, 0, 4);
            M5.Lcd.print("COG");
    
            M5.Lcd.setCursor(0, 40, 8);
            M5.Lcd.printf("%.0f deg\n", COG);
    
            M5.Lcd.setCursor(180, 40, 4);
            M5.Lcd.print("deg");
        }
        else if (displayMode == DISPLAY_COCKPIT_TEMP) // New display for Cockpit Temperature
        {
            M5.Lcd.setCursor(0, 0, 4);
            M5.Lcd.print("Cockpit Temp");
    
            M5.Lcd.setCursor(0, 40, 8);
            M5.Lcd.printf("%.1f C\n", Cockpit);
        }
        else if (displayMode == DISPLAY_TIME)
        {
            M5.Lcd.setCursor(0, 0, 4);
            M5.Lcd.print("Time");
    
            M5.Lcd.setCursor(00, 40, 6);
            M5.Lcd.printf("%s", udoo_time);
        }    // Removed DISPLAY_POWER as AIS is no longer used

    for (int i=0; i<10; i++)
    {
        delay(100);
        M5.update();
        if (M5.BtnA.wasReleased())
            buttonA = true;
    }

    disCharge = M5.Axp.GetIdischargeData() / 2;
    if (disCharge == 0)
    {
        resetShutdownTimer(shutdownDelaySec);
    }
    else if (millis() > shutdownTime)
    {
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0, 4);
        M5.Lcd.println("Shutting Down!!!\nPress button\nto cancel");

        for (int i=0; i<(cancelDelaySec * 10); i++)
        {
            delay(100);
            M5.update();
            if (M5.BtnA.wasReleased())
            {
                buttonA = true;
                m5.Lcd.println("Canceled");
                delay(2000);
                return;
            }
        }
        M5.Axp.PowerOff();
        // never reached
    }
}