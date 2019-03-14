#include <DHT.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "version.h"

#define ON_READ_FLASH_ENABLED 1

#include "config.h"

char topicBuffer[128];
char messageBuffer[128];
byte macAddressBuffer[6];
char formattedMacAddress[18];
/** Used by the message_ building functions */
int messageBufferOffset;
long taskStartTime;

// Update these with values suitable for your network.

WiFiClient espClient;
PubSubClient pubSubClient(espClient);
DHT dhts[] = {
  DHT(D1, DHT22),
  DHT(D2, DHT22),
  DHT(D3, DHT22),
  DHT(D4, DHT22),
  DHT(D5, DHT22),
  DHT(D6, DHT22),
  DHT(D7, DHT22),
  DHT(D8, DHT22),
};

// These should correspond to GPIO port names, e.g.
// dht1 reads from D1, dht8 reads from D8, etc.
const char *dhtNames[] = {
  "dht1",
  "dht2",
  "dht3",
  "dht4",
  "dht5",
  "dht6",
  "dht7",
  "dht8",
};

const size_t dhtCount = sizeof(dhts)/sizeof(class DHT);

char hexDigit(int num) {
  num = num & 0xF;
  if( num < 10 ) return '0'+num;
  if( num < 16 ) return 'A'+(num-10);
  return '?'; // Should be unpossible
}

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

const char *wifiStatusToString( int status ) {
  switch( status ) {
  case WL_CONNECTED: return "connected";
  case WL_NO_SHIELD: return "no shield";
  case WL_NO_SSID_AVAIL: return "no SSID available";
  case WL_SCAN_COMPLETED: return "scan completed";
  case WL_CONNECT_FAILED: return "connect failed";
  case WL_CONNECTION_LOST: return "connection lost";
  case WL_DISCONNECTED: return "disconnected";
  default:
    return "unknown status";
  }
}

int setUpWifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("# Connecting to ");
  Serial.print(WIFI_SSID);
  
  int attempts = 0;
  const int maxAttempts = 10;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int status;
  while( (status = WiFi.status()) != WL_CONNECTED && attempts < maxAttempts ) {
    digitalWrite(BUILTIN_LED, attempts & 0x1 == 0 ? LOW : HIGH );
    delay(500);
    Serial.print(".");
    ++attempts;
    if( attempts < maxAttempts && attempts % 10 == 0 ) {
      Serial.println("");
      Serial.print("# Still connecting to wifi network: ");
      Serial.print(WIFI_SSID);
    }
  }
  digitalWrite(BUILTIN_LED, HIGH );
  
  if( status == WL_CONNECTED ) {
    Serial.println("connected");
    Serial.print("# IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("# MAC address: ");
    Serial.println(formattedMacAddress);
    return 0;
  } else {
    const char *message = wifiStatusToString(status);
    Serial.print("failed to connect; status:");
    Serial.println(message);
    return 1;
  }
}

void handleIncomingMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("# Incoming message on topic:");
  Serial.print(topic);
  Serial.print(": ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

/// Message sending

void chat(const char *whatever) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/chat", TOPIC_PREFIX, formattedMacAddress);
  pubSubClient.publish(topicBuffer, whatever);
  Serial.println(whatever);
}

void publishAttr(const char *deviceName, const char *attrName, float value) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/%s/%s", TOPIC_PREFIX, formattedMacAddress, deviceName, attrName);
  message_clear();
  message_appendFloat(value);
  message_close();
  pubSubClient.publish(topicBuffer, messageBuffer);
  Serial.print("# ");
  Serial.print(deviceName);
  Serial.print("/");
  Serial.print(attrName);
  Serial.print(" = ");
  Serial.println(value);
}

void reportIpAddress() { /* TODO if needed byt porbaly isn't */ }

void checkIn( struct Task *task ) {
  reconnect();
  chat("Hello again!");
}

void statusTopic(char *buffer, size_t len) {
  snprintf(buffer, len, "%s%s/%s", TOPIC_PREFIX, formattedMacAddress, "status");
}

void reconnect() {
  // Loop until we're reconnected
  while( !pubSubClient.connected() ) {
    while( WiFi.status() != WL_CONNECTED ) {
      Serial.println("# WiFi not connected.  Attempting to connect...");
      if( setUpWifi() != 0 ) {
        Serial.println("# Failed to connect to WiFi :(");
        delay(1000);
        return;
      }
    }
    
    Serial.print("# Attempting MQTT connection to ");
    Serial.print(MQTT_SERVER);
    Serial.print(" as ");
    Serial.print(formattedMacAddress);
    Serial.print("...");
    
    statusTopic(topicBuffer, sizeof(topicBuffer));

    // Attempt to connect
    if( pubSubClient.connect(formattedMacAddress, topicBuffer, 1, true, "offline") ) {
      Serial.println("connected");
      pubSubClient.publish(topicBuffer, "online", true);
      // Once connected, publish an announcement...
      snprintf(messageBuffer, sizeof(messageBuffer), "Hi I'm %s (ArduinoTemperatureHumiditySensor) and just connected!", formattedMacAddress);
      chat(messageBuffer);
    } else {
      Serial.print("failed; rc=");
      Serial.print(pubSubClient.state());
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
  const char *name;
  float previousTemperature;
  float previousHumidity;
  long previousTemperatureReportTime;
  long previousHumidityReportTime;
  DHT *dht;
};

struct DHTNode dhtNodes[dhtCount];

void readDht( struct DHTNode *dhtNode ) {
  if( ON_READ_FLASH_ENABLED ) digitalWrite(BUILTIN_LED, LOW);
  
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
  
  if( ON_READ_FLASH_ENABLED ) digitalWrite(BUILTIN_LED, HIGH);
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
    dhtNodes[i].dht = &dhts[i];
    dhtNodes[i].name = dhtNames[i];
    dhtNodes[i].previousTemperature = NAN;
    dhtNodes[i].previousHumidity = NAN;
    dhtNodes[i].previousTemperatureReportTime = -10000;
    dhtNodes[i].previousHumidityReportTime = -10000;
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
  Serial.println("");
  Serial.print("# "); // Hi!  I'm....
  Serial.print(ALC_NAME);
  Serial.print(" ");
  Serial.print(ALC_SUBNAME);
  Serial.print(" v");
  Serial.print(ALC_VERSION);
  Serial.println(", booting!");

  // Seems that this can be done before WiFi.begin():
  WiFi.macAddress(macAddressBuffer);
  formatMacAddressInto(macAddressBuffer, ':', formattedMacAddress);

  Serial.print("# MAC address: ");
  Serial.println(formattedMacAddress);
  
  setUpWifi();
  pubSubClient.setServer(MQTT_SERVER, 1883);
  pubSubClient.setCallback(handleIncomingMessage);

  Serial.print("# Setting up DHT readers...");
  setUpDhts();
  Serial.println("ok");
}

void loop() {
  while( Serial.available() > 0 ) {
    char inputChar = Serial.read();
    if( inputChar == '\n' ) {
      Serial.println("# Hey, thanks for the input!");
    }
  }    
  reconnect();
  pubSubClient.loop();
  doTasks();
  delay(100);
}
