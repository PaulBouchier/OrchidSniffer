# Modifications to OrchidSniffer

The file OrchidSniffer.ino in the current directory
is an Arduino sketch containing code that accesses an AIS server
and an MQTT server and extracts data from them, and displays selected data items on the display
of an M5StickC Plus Arduino-compatible device.

The system design needs to change in significant ways.

This file contains instructions on which modifications are needed and how the updated system
should work.

## High level objective

Remove direct access to the AIS server. Get all needed data from the MQTT server. Modify
the menu system which is operated by button pushes to display new data.

## Implementation notes

- Be sure to follow the low-level tasks in order and in detail
- Use the current git branch
- Modify the arduino sketch OrchidSniffer.ino in the current directory
- comment the code

## MQTT server context

The MQTT server publishes messages on topic named "Time". Each message contains a string
containing the following comma-separated fields formatted as text strings:
- local time
- TWD
- TWS
- COG
- SOG

There may be additional fields.

### MQ_Reset

In place of the fields above, the message may contain a special string: "MQ_reset".

## Low level tasks

These tasks are ordered from start to finish

1. Remove AIS code

```gemini
    - Remove the WiFiClient aisClient and associated data. Keeep the mqttServer.
    - Remove the readSocket() function and the line that calls it and any data or
methods it uses, including the ai***Handler functions and the parser initialization in setup()
    - remove the EEPROM logic
    - remove the DISPLAY POWER enum and associated display logic
```

2. Subscribe to the MQTT Time topic and parse data from it

```gemini
    - Change the topic name that the mqttConnect method subscribes to to "Time"
    - Change the mqttCallback() function to implement the parsing function specified by
the python MQTT callback code below. You can ignore and discard the Cabin and Cockpit fields.
```
```gemini
def sub_cb(topic, msg):
  global udoo_time, TW$, TWS, COG, SOG, Cabin, Cockpit
  print('topic:', topic, 'msg: ', msg)

  if msg == wo_global.MQ_reset:
    print('MQ_reset received')
    webrepl.stop()
    time.sleep(30)                # Delay so printed message can be seen
    machine.reset()

  else:
    msg_str = msg.decode("utf-8")
    list = msg_str.split(',')
    udoo_time, TWD, TWS, COG, SOG, Cabin, Cockpit = [list[i] for i in (0,1,2,3,4,5,6)]
```
IMPORTANT: Data received from the MQTT topic is a C-style string (null-terminated character array). Parsing must treat it as such when copying. udoo_time is an array of characters formatted as HH:MM:SS. TWD, TWS, COG, SOG are floats expressed as a C-string.

3. Print received data to Serial

    - Print the fields udoo_time, TWD, TWS, COG, SOG to the Serial port

4. Modify the display output in the loop() function. The
variables represent:
- TWD: wind direction
- TWS: wind speed
- COG: boat heading
- SOG: boat speed
- udoo_time: boat time

Order of display
- The first (default) variable to display should be wind speed
- The order of display should be: TWS, TWD, SOG, COG, time


