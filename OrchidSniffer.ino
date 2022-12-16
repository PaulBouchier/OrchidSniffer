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
#include <NMEAParser.h>
#include <EEPROM.h>

#define MSG_BUFFER_SIZE (50)

// library objects
WiFiClient mqttServerClient;
NMEAParser<4> parser;
WiFiClient aisClient;
PubSubClient mqttClient(mqttServerClient);

// Configure the name and password of the connected wifi and your MQTT Serve
// host.
const char* aisServer = "192.168.1.114";
const char* mqttServer = "192.168.1.116";
const int aisPort = 39150;

// send test message buffers
unsigned long lastMsg = 0;
char msg[MSG_BUFFER_SIZE];
int value = 0;

// display modes
enum DisplayMode {
    DISPLAY_TIME,
    DISPLAY_WIND_SPEED,
    DISPLAY_WIND_DIR,
    DISPLAY_POWER,
    DISPLAY_MODE_LAST
};

// UI data
int displayMode = DISPLAY_TIME;
bool buttonA = false;

// AIS data
const int BUF_SIZE = 1800;
char aisBuffer[BUF_SIZE];
int timeZone = 11;
int hours = -1;
int minutes = -1;
float windSpeed = -1;
float windDirRelative = 0;
float headingMag = 0;
float magVariation = 0;

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
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    if (!strncmp((char*)payload, "tz,", 3))
    {
        Serial.printf("Got MQTT timezone msg: %s", payload);
        char tzBuf[4];
        int i;

        for (i=0; i<4; i++) tzBuf[i] = 0;  // clear timezone string buffer
        
        i=0;
        while (i < 2 && (i+3 < length))
        {
            if ((payload[i+3] < '0') && (payload[i+3] > '9'))
                break;
            else
                tzBuf[i] = payload[i+3];
                i++;
        }

        timeZone = atoi(tzBuf);
        EEPROM.write(0, timeZone);
        EEPROM.commit();
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
            mqttClient.subscribe("display_time");
            mqttClient.subscribe("M5Stack");

        } else {
            M5.Lcd.print("failed, rc=");
            M5.Lcd.print(mqttClient.state());
            M5.Lcd.println("try again in 5 seconds");
            delay(5000);
        }
    }
}

void airmcHandler()
{
    float time;
    char timeBuf[20];
    char tempchar[4] = {0, 0, 0, 0};
    int temp = 0;
    
    if (!parser.getArg(0, timeBuf))
    {
        Serial.println("Error getting AIRMC arg 0");
        return;
    }
    // M5.Lcd.printf("$AIRMC time: %s\n", timeBuf);
    hours = (int)(timeBuf[0] - 0x30) * 10;
    hours += (int)(timeBuf[1] - 0x30);
    hours = (hours + timeZone) % 24;
    
    minutes = (int)(timeBuf[2]-0x30) * 10;
    minutes += (int)(timeBuf[3]-0x30);

    if (hours > 23 || hours < 0 || minutes > 59 || minutes < 0)
        Serial.println("Time conversion error");
}

void aimwvHandler()
{
    if (!parser.getArg(0, windDirRelative))
    {
        Serial.println("Error getting aimwv arg 0");
        return;
    }

    if (!parser.getArg(2, windSpeed))
    {
        Serial.println("Error getting aimwv arg 2");
        return;
    }
    
    Serial.printf("Wind Dir (Relative): %f, wind speed (kt) %f\n", windDirRelative, windSpeed);
}

void aihdgHandler()
{
    if (!parser.getArg(0, headingMag))
    {
        Serial.println("Error getting aihdg arg 0");
        return;
    }
    if (!parser.getArg(3, magVariation))
    {
        Serial.println("Error getting aihdg arg 3");
        return;
    }
    
    Serial.printf("Heading (magnetic) %f, variation %f\n", headingMag, magVariation);
}

void readSocket()
{
    // M5.Lcd.print("\nStarting socket connection...");
    if (aisClient.connect(aisServer, aisPort))
    {
        if (!aisClient.connected())
            M5.Lcd.println("FAILED to connect to ais socket");        
    }

    // delay 500ms for server to feed us data, polling for button push
    for (int i=0; i<5; i++)
    {
        delay(100);
        M5.update();
        if (M5.BtnA.wasReleased())
            buttonA = true;    
    }

    int bufIndex = 0;
    while (aisClient.available())
    {
        char c = aisClient.read();
        // Serial.print(c);
        aisBuffer[bufIndex++] = c;
        parser << c;
    }
    aisClient.stop();
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

    // configure EEPROM
    if (!EEPROM.begin(1))
    {
        M5.Lcd.println("Failed to initialize EEPROM");
        delay(5000);
    }
    else
    {
        int tz = EEPROM.read(0);
        if (tz >= 0 && tz <= 23)
        {
            timeZone = tz;
            M5.Lcd.printf("Timezone: %d\n", timeZone);
        }
    }

    // configure NMEA parser
    parser.addHandler("AIRMC", airmcHandler);
    parser.addHandler("AIMWV", aimwvHandler);
    parser.addHandler("AIHDG", aihdgHandler);
    
    resetShutdownTimer(shutdownDelaySec);  // initialize shutdown timer

    delay(1000);  // give time to read msgs
}

void loop() {
    if (!mqttClient.connected())
        mqttConnect();

    // loop() is called periodically to allow clients to process incoming
    // messages and maintain connections to the server
    mqttClient.loop();

    readSocket(); // read NMEA data from the AIS server
    
    if (buttonA == true)
    {
        Serial.println("Button A pushed");
        buttonA = false;
        displayMode += 1;
        if (displayMode == DISPLAY_MODE_LAST)
            displayMode = DISPLAY_TIME;
        resetShutdownTimer(shutdownDelaySec);
    }
     
    M5.Lcd.fillScreen(BLACK);

    if (displayMode == DISPLAY_TIME)
    {
        M5.Lcd.setCursor(0, 0, 4);
        M5.Lcd.print("Time");

        M5.Lcd.setCursor(00, 40, 8);
        M5.Lcd.printf("%02d%02d", hours, minutes);        
    }
    else if (displayMode == DISPLAY_WIND_SPEED)
    {
        M5.Lcd.setCursor(0, 0, 4);
        M5.Lcd.print("Wind speed");
        
        M5.Lcd.setCursor(0, 40, 8);
        M5.Lcd.printf("%0.1f", windSpeed);
        
        M5.Lcd.setCursor(200, 40, 4);
        M5.Lcd.print("kt");
    }

    else if (displayMode == DISPLAY_WIND_DIR)
    {
        float windDir = headingMag + magVariation + windDirRelative;
        if (windDir > 359)
            windDir -= 360;
        Serial.printf("windDir %.0f\n", windDir);
        
        M5.Lcd.setCursor(0, 0, 4);
        M5.Lcd.print("Wind direction");
        
        M5.Lcd.setCursor(0, 40, 8);
        M5.Lcd.printf("%.0f deg\n", windDir);
        
        M5.Lcd.setCursor(180, 40, 4);
        M5.Lcd.print("deg");
    }
    else if (displayMode == DISPLAY_POWER)
    {
        M5.Lcd.setCursor(0, 0, 2);
  
        int timeToShutdown = (shutdownTime - millis()) / 1000;
        M5.Lcd.printf("Shutting down in %d sec\n", timeToShutdown);

        bat = M5.Axp.GetPowerbatData()*1.1*0.5/1000;
        M5.Lcd.printf("batt power: %d mW\n", bat);
  
        charge = M5.Axp.GetIchargeData() / 2;
        M5.Lcd.printf("icharge: %d mA\r\n", charge);

        disCharge = M5.Axp.GetIdischargeData() / 2;
        M5.Lcd.printf("disCharge: %d mA\n", disCharge);
  
        vbat = M5.Axp.GetVbatData() * 1.1 / 1000;
        M5.Lcd.printf("vbat: %.3f V\n", vbat);
    }

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
