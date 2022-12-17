# Orchid Sniffer

This sketch sniffs data from the AIS server aboard a sailboat and
displays it on an M5StickC Plus.

The file secrets.template.h must be copied to secrets.h and your Wifi 
SSID and password must be added to it in order to build this sketch.

## Prerequisites

These instructions tell how to install Arduino support for the M5StickPlus:
https://docs.m5stack.com/en/quick start/m5stickc plus/arduino

In addition, You will need to install the following Arduino libraries:
NMEAParser - parses NMEA sentences
PubSubClient - provides MQTT support
