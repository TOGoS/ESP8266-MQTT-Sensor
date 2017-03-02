/*
 Basic ESP8266 MQTT example

 This sketch demonstrates the capabilities of the pubsub library in combination
 with the ESP8266 board/library.

 It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off

 It will reconnect to the server if the connection is lost using a blocking
 reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
 achieve the same result without blocking the main loop.

 To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "config.h"

#define messageBufferSize 128

// Update these with values suitable for your network.

WiFiClient espClient;
PubSubClient client(espClient);
char messageBuffer[messageBufferSize];

char hexDigit(int num) {
  num = num & 0xF;
  if( num < 10 ) return '0'+num;
  if( num < 16 ) return 'A'+num;
  return '?'; // Should be unpossible
}

byte macAddressBuffer[6];
char formattedMacBuffer[18];

char *formatMacAddressInto(byte *macAddress, char separator, char *into=formattedMacBuffer) {
  into[ 0] = hexDigit(macAddress[0]>>4);
  into[ 1] = hexDigit(macAddress[0]);
  into[ 2] = separator;
  into[ 3] = hexDigit(macAddress[1]>>4);
  into[ 4] = hexDigit(macAddress[1]);
  into[ 5] = separator;
  into[ 6] = hexDigit(macAddress[2]>>4);
  into[ 7] = hexDigit(macAddress[2]);
  into[ 8] = separator;
  into[ 9] = hexDigit(macAddress[3]>>4);
  into[10] = hexDigit(macAddress[3]);
  into[11] = separator;
  into[12] = hexDigit(macAddress[4]>>4);
  into[13] = hexDigit(macAddress[4]);
  into[14] = separator;
  into[15] = hexDigit(macAddress[5]>>4);
  into[16] = hexDigit(macAddress[5]);
  into[17] = 0;
  return into;
}

void setUpWifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("MAC address: ");
  WiFi.macAddress(macAddressBuffer);
  formatMacAddressInto(macAddressBuffer, ':', formattedMacBuffer);
  Serial.println(formattedMacBuffer);
}

void handleIncomingMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection to "MQTT_SERVER"...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      snprintf(messageBuffer, messageBufferSize, "Hi I'm %s and just connected!", formattedMacBuffer);
      client.publish("device-chat", messageBuffer);
      // ... and resubscribe
      //client.subscribe(IN_TOPIC);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

typedef struct Task {
  const char *name;
  long interval; // Interval at which this task should be done, in milliseconds
  long lastRunTime;
  void (*invoke)(struct Task *);
};

void sayHi( struct Task *task ) {
    Serial.println("Preparing a message...");
    snprintf(messageBuffer, messageBufferSize, "Hello again from %s!", formattedMacBuffer);
    Serial.print("Publish message: ");
    Serial.println(messageBuffer);
    client.publish("device-chat", messageBuffer);

}

struct Task tasks[] = {
  {
    name: "say-hi",
    interval: 60000,
    lastRunTime: -1,
    invoke: sayHi
  }
};

void doTasks() {
  int taskCount = sizeof(tasks)/sizeof(struct Task);
  for( int i=0; i<taskCount; ++i ) {
    long now = millis();
    if( now - tasks[i].lastRunTime > tasks[i].interval ) {
      tasks[i].invoke(&tasks[i]);
      tasks[i].lastRunTime = now;
    }
  }
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  setUpWifi();
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(handleIncomingMessage);
}

void loop() {
  if( !client.connected() ) reconnect();
  client.loop();
  doTasks();
  delay(100);
}

