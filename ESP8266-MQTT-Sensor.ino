#include <DHT.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define D0   16
#define D1    5
#define D2    4
#define D3    0
#define D4    2
#define D5   14
#define D6   12
#define D7   13
#define D8   15
#define D9    3
#define D10   1

// Note that pin 2 is apparently labelled 'D4' on the NodeMCU board!
#define DHTPIN D4
#define DHTTYPE DHT22

#include "config.h"

#define messageBufferSize 128

// Update these with values suitable for your network.

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht0(D4, DHTTYPE);
DHT dht1(D3, DHTTYPE);
char messageBuffer[messageBufferSize];
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

void message_clear() {
  messageBufferOffset = 0;
}
void message_appendString(const char *str) {
  while( *str != 0 && messageBufferOffset < messageBufferSize-1 ) {
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
  int printed = snprintf(messageBuffer+messageBufferOffset, messageBufferSize-messageBufferOffset, "%d.%02d", (int)v, hundredths);
  if( printed > 0 ) messageBufferOffset += printed;
}
void message_close() {
  messageBuffer[messageBufferOffset++] = 0;
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

struct DHTNode {
  char *name;
  float previousTemperature;
  float previousHumidity;
  long previousReportTime;
  DHT *dht;
};

struct DHTNode dhtNodes[] = {
  {
    name: "dht0",
    previousTemperature: 0,
    previousHumidity: 0,
    previousReportTime: 0,
    dht: &dht0
  },
  {
    name: "dht1",
    previousTemperature: 0,
    previousHumidity: 0,
    previousReportTime: 0,
    dht: &dht1
  }
};

void readDht( struct DHTNode *dhtNode ) {
  DHT *dht = dhtNode->dht;
  float temp = dht->readTemperature();
  float humid = dht->readHumidity();
  bool changed = temp != dhtNode->previousTemperature || humid != dhtNode->previousHumidity;
  
  // TODO: Separate topics
  message_clear();
  if( !isnan(temp) ) {
    message_separate(" ");
    message_appendString("temperature:");
    message_appendFloat(temp);
  }
  if( !isnan(temp) ) {
    message_separate(" ");
    message_appendString("humidity:");
    message_appendFloat(humid);
  }
  if( messageBufferOffset > 0 ) {
    message_separate(" ");
    message_appendString("nodeId:");
    message_appendMacAddressHex(macAddressBuffer, "-");
    message_appendString("/");
    message_appendString(dhtNode->name);
    message_close();
    if( changed || taskStartTime - dhtNode->previousReportTime > 60000 ) {
      client.publish("device-chat", messageBuffer);
      Serial.println(messageBuffer);
      dhtNode->previousReportTime = taskStartTime;
    }
  }
  dhtNode->previousTemperature = temp;
  dhtNode->previousHumidity = humid;
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
    invoke: sayHi
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

