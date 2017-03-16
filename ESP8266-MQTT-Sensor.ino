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
PubSubClient pubSubClient(espClient);
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
  Serial.print("# Connecting to wifi network: ");
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
    Serial.println("");
    Serial.println("# WiFi connected");
    Serial.print("# IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("# MAC address: ");
    WiFi.macAddress(macAddressBuffer);
    formatMacAddressInto(macAddressBuffer, ':', formattedMacBuffer);
    Serial.println(formattedMacBuffer);
    return 0;
  } else {
    Serial.println("");
    const char *message = wifiStatusToString(status);
    Serial.print("# Failed to connect to WiFi: ");
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
    Serial.print(formattedMacBuffer);
    Serial.print("...");
    // Attempt to connect
    if( pubSubClient.connect(formattedMacBuffer) ) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      snprintf(messageBuffer, messageBufferSize, "# Hi I'm %s and just connected!", formattedMacBuffer);
      pubSubClient.publish("device-chat", messageBuffer);
      // ... and resubscribe
      //client.subscribe(IN_TOPIC);
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

void sayHi( struct Task *task ) {
  snprintf(messageBuffer, messageBufferSize, "# Hello again from %s!", formattedMacBuffer);
  Serial.println(messageBuffer);
  reconnect();
  pubSubClient.publish("device-chat", messageBuffer);
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
  digitalWrite(BUILTIN_LED, LOW);
  
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
      pubSubClient.publish("device-chat", messageBuffer);
      Serial.println(messageBuffer);
      dhtNode->previousReportTime = taskStartTime;
    }
  }
  dhtNode->previousTemperature = temp;
  dhtNode->previousHumidity = humid;

  digitalWrite(BUILTIN_LED, HIGH);
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
  strcpy(formattedMacBuffer, "No MAC!");
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  pubSubClient.setServer(MQTT_SERVER, 1883);
  pubSubClient.setCallback(handleIncomingMessage);
  setUpDhts();
}

void loop() {
  reconnect();
  pubSubClient.loop();
  doTasks();
  delay(100);
}

