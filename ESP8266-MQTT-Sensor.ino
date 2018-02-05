#include <DHT.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "config.h"

char topicBuffer[128];
char messageBuffer[128];

// Update these with values suitable for your network.

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht0(D4, DHT22);
DHT dht1(D3, DHT22);

/** Used by the message_ building functions */
int messageBufferOffset;
long taskStartTime;

char hexDigit(int num) {
  num = num & 0xF;
  if( num < 10 ) return '0'+num;
  if( num < 16 ) return 'A'+num;
  return '?'; // Should be unpossible
}

byte macAddressBuffer[6];
char formattedMacAddress[18];

char *formatMacAddressInto(byte *macAddress, char separator, char *into=formattedMacAddress) {
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

void message_clear() {
  messageBufferOffset = 0;
}
void message_appendString(const char *str) {
  while( *str != 0 && messageBufferOffset < sizeof(messageBuffer)-1 ) {
    messageBuffer[messageBufferOffset] = *str;
    ++str;
    ++messageBufferOffset;
  }
}
void message_appendMacAddressHex(byte *macAddress, const char *octetSeparator) {
  for( int i=0; i<6; ++i ) {
    if( i > 0 ) message_appendString(octetSeparator);
    messageBuffer[messageBufferOffset++] = hexDigit(macAddress[i]>>4);
    messageBuffer[messageBufferOffset++] = hexDigit(macAddress[i]);
  }
}
void message_separate(const char *separator) {
  if( messageBufferOffset == 0 ) return;
  message_appendString(separator);
}
void message_appendFloat(float v) {
  if( v < 0 ) {
    messageBuffer[messageBufferOffset++] = '-'; // memory unsafety!!
    v = -v;
  }
  int hundredths = (v * 100) - ((int)v) * 100;
  int printed = snprintf(messageBuffer+messageBufferOffset, sizeof(messageBuffer)-messageBufferOffset, "%d.%02d", (int)v, hundredths);
  if( printed > 0 ) messageBufferOffset += printed;
}
void message_close() {
  messageBuffer[messageBufferOffset++] = 0;
}

void setUpWifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("# Connecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("# WiFi connected");
  Serial.print("# IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("# MAC address: ");
  Serial.println(formattedMacAddress);
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

/// Message sending

void chat(const char *whatever) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/chat", TOPIC_PREFIX, formattedMacAddress);
  client.publish(topicBuffer, whatever);
}

void publishAttr(const char *deviceName, const char *attrName, float value) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/%s/%s", TOPIC_PREFIX, formattedMacAddress, deviceName, attrName);
  message_clear();
  message_appendFloat(value);
  message_close();
  client.publish(topicBuffer, messageBuffer);
  Serial.print("# ");
  Serial.print(deviceName);
  Serial.print("/");
  Serial.print(attrName);
  Serial.print(" = ");
  Serial.println(value);
}

void reportIpAddress() { /* TODO if needed byt porbaly isn't */ }

void checkIn( struct Task *task ) {
  chat("Hello again!");
  reportIpAddress();
}



void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("# Attempting MQTT connection to "MQTT_SERVER"...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      snprintf(messageBuffer, sizeof(messageBuffer), "Hi I'm %s (ArduinoTemperatureHumiditySensor) and just connected!", formattedMacAddress);
      Serial.print("# formatted a message; chatting it...");
      chat(messageBuffer);
      Serial.println("ok");
      Serial.print("# reporting IP address...");
      reportIpAddress();
      Serial.println("ok");
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

struct DHTNode {
  char *name;
  float previousTemperature;
  float previousHumidity;
  long previousTemperatureReportTime;
  long previousHumidityReportTime;
  DHT *dht;
};

struct DHTNode dhtNodes[] = {
  {
    name: "dht0",
    previousTemperature: 0,
    previousHumidity: 0,
    previousTemperatureReportTime: 0,
    previousHumidityReportTime: 0,
    dht: &dht0
  },
  {
    name: "dht1",
    previousTemperature: 0,
    previousHumidity: 0,
    previousTemperatureReportTime: 0,
    previousHumidityReportTime: 0,
    dht: &dht1
  }
};

void readDht( struct DHTNode *dhtNode ) {
  DHT *dht = dhtNode->dht;

  // TODO: Take average over multiple seconds.
  // If average strays by more than 0.5 of the minimum measurement unit,
  // then report.

  float temp = dht->readTemperature();
  if( (temp != dhtNode->previousTemperature || taskStartTime - dhtNode->previousTemperatureReportTime > 60000) && !isnan(temp) ) {
    publishAttr(dhtNode->name, "temperature", temp);
    dhtNode->previousTemperature = temp;
    dhtNode->previousTemperatureReportTime = taskStartTime;
  }
  
  float humid = dht->readHumidity();
  if( (humid != dhtNode->previousHumidity || taskStartTime - dhtNode->previousHumidityReportTime > 60000) && !isnan(humid) ) {
    publishAttr(dhtNode->name, "humidity", humid);
    dhtNode->previousHumidity = humid;
    dhtNode->previousHumidityReportTime = taskStartTime;
  }
}

void readDhts( struct Task *task ) {
  int dhtNodeCount = sizeof(dhtNodes)/sizeof(struct DHTNode);
  for( int i=0; i<dhtNodeCount; ++i ) {
    readDht( &dhtNodes[i] );
  }
}

void setUpDhts() {
  int dhtNodeCount = sizeof(dhtNodes)/sizeof(struct DHTNode);
  for( int i=0; i<dhtNodeCount; ++i ) {
    dhtNodes[i].dht->begin();
  }
}

struct Task tasks[] = {
  {
    name: "say-hi",
    interval: 60000,
    lastRunTime: 0,
    invoke: checkIn
  },
  {
    name: "read-dhts",
    interval: 3000,
    lastRunTime: 0,
    invoke: readDhts
  }
};

void doTasks() {
  int taskCount = sizeof(tasks)/sizeof(struct Task);
  taskStartTime = millis();
  for( int i=0; i<taskCount; ++i ) {
    if( taskStartTime - tasks[i].lastRunTime > tasks[i].interval ) {
      tasks[i].invoke(&tasks[i]);
      tasks[i].lastRunTime = taskStartTime;
    }
  }
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  Serial.println("# ArduinoTemperatureHumiditySensor booting");

  // Seems that this can be done before WiFi.begin():
  WiFi.macAddress(macAddressBuffer);
  formatMacAddressInto(macAddressBuffer, ':', formattedMacAddress);

  Serial.print("# MAC address: ");
  Serial.println(formattedMacAddress);
  
  setUpWifi();
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(handleIncomingMessage);
  setUpDhts();
}

void loop() {
  if( !client.connected() ) reconnect();
  client.loop();
  doTasks();
  delay(100);
}

