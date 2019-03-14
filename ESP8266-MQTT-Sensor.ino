#include <DHT.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <cstdio>

#include "version.h"

#define ON_READ_FLASH_ENABLED 1

#include "config.h"

enum class EEPROMAddresses {
  VERSION_HI = 0,
  VERSION_LO = 1,
  DHT_ENABLEMENT = 2,
  END
};

uint16_t savedEepromVersion = 0;

char topicBuffer[128];
char messageBuffer[128];
byte macAddressBuffer[6];
char formattedMacAddress[18];
/** Used by the message_ building functions */
int messageBufferOffset;
long taskStartTime;

void handleIncomingMqttMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("# Incoming message on topic:");
  Serial.print(topic);
  Serial.print(": ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}


WiFiClient espClient;
PubSubClient pubSubClient(MQTT_SERVER_HOSTNAME, MQTT_SERVER_PORT, handleIncomingMqttMessage, espClient);

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

/// Message sending

void chat(const char *whatever) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/chat", TOPIC_PREFIX, formattedMacAddress);
  pubSubClient.publish(topicBuffer, whatever);
  Serial.print("# ");
  Serial.println(whatever);
}

void publishAttr(const char *deviceName, const char *attrName, bool value) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/%s/%s", TOPIC_PREFIX, formattedMacAddress, deviceName, attrName);
  const char *valStr = value ? "true" : "false";
  pubSubClient.publish(topicBuffer, valStr);
  Serial.print(deviceName);
  Serial.print("/");
  Serial.print(attrName);
  Serial.print(" ");
  Serial.println(valStr);
}

void publishAttr(const char *deviceName, const char *attrName, float value) {
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s/%s/%s", TOPIC_PREFIX, formattedMacAddress, deviceName, attrName);
  message_clear();
  message_appendFloat(value);
  message_close();
  pubSubClient.publish(topicBuffer, messageBuffer);
  Serial.print(deviceName);
  Serial.print("/");
  Serial.print(attrName);
  Serial.print(" ");
  Serial.println(value);
}

void reportIpAddress() { /* TODO if needed byt porbaly isn't */ }

void checkIn( struct Task *task ) {
  chat("Still alive!");
}

void statusTopic(char *buffer, size_t len) {
  snprintf(buffer, len, "%s%s/%s", TOPIC_PREFIX, formattedMacAddress, "status");
}

void reconnect() {
  if( WiFi.status() != WL_CONNECTED ) {
    Serial.println("# WiFi not connected.  Attempting to connect...");
    if( setUpWifi() != 0 ) {
      Serial.println("# Failed to connect to WiFi :(");
      delay(1000);
      return;
    }
  }

  if( !pubSubClient.connected() ) {
    Serial.print("# Attempting MQTT connection to ");
    Serial.print(MQTT_SERVER_HOSTNAME);
    Serial.print(" as ");
    Serial.print(formattedMacAddress);
    Serial.print("...");
    
    statusTopic(topicBuffer, sizeof(topicBuffer));

    // Attempt to connect
    if( pubSubClient.connect(formattedMacAddress, topicBuffer, 1, true, "disconnected") ) {
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
  bool autoReadEnabled;
  float previousTemperature;
  float previousHumidity;
  long previousTemperatureReportTime;
  long previousHumidityReportTime;
  DHT *dht;
};

struct DHTNode dhtNodes[dhtCount];
const int dhtNodeCount = sizeof(dhtNodes)/sizeof(struct DHTNode);

void readDht( DHTNode &dhtNode, bool explicitly=false ) {
  //if( ON_READ_FLASH_ENABLED ) digitalWrite(BUILTIN_LED, LOW);
  
  DHT *dht = dhtNode.dht;

  // TODO: Take average over multiple seconds.
  // If average strays by more than 0.5 of the minimum measurement unit,
  // then report.

  float temp = dht->readTemperature();
  if( explicitly || (temp != dhtNode.previousTemperature || taskStartTime - dhtNode.previousTemperatureReportTime > 60000) && !isnan(temp) ) {
    publishAttr(dhtNode.name, "temperature", temp);
    dhtNode.previousTemperature = temp;
    dhtNode.previousTemperatureReportTime = taskStartTime;
  }
  float humid = dht->readHumidity();
  if( explicitly || (humid != dhtNode.previousHumidity || taskStartTime - dhtNode.previousHumidityReportTime > 60000) && !isnan(humid) ) {
    publishAttr(dhtNode.name, "humidity", humid);
    dhtNode.previousHumidity = humid;
    dhtNode.previousHumidityReportTime = taskStartTime;
  }
  
  //if( ON_READ_FLASH_ENABLED ) digitalWrite(BUILTIN_LED, HIGH);
}

void readDhts( struct Task *task ) {
  for( int i=0; i<dhtNodeCount; ++i ) {
    if( dhtNodes[i].autoReadEnabled ) {
      readDht( dhtNodes[i], false );
    }
  }
}

void setUpDhts() {
  uint8_t dhtEnablement = (savedEepromVersion == EEPROM_SAVE_VERSION) ?
    EEPROM.read((off_t)EEPROMAddresses::DHT_ENABLEMENT) : 0xFF;
  for( int i=0; i<dhtNodeCount; ++i ) {
    dhtNodes[i].dht = &dhts[i];
    dhtNodes[i].autoReadEnabled = (((dhtEnablement >> i) & 1) == 1);
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
  delay(500);
  pinMode(LED_BUILTIN, OUTPUT);
  for( unsigned int d = 0; d < 5; ++d ) {
    digitalWrite(LED_BUILTIN, LOW); // Light on
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH); // Light off
    delay(200);
  }

  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  Serial.println("");
  Serial.print("# "); // Hi!  I'm....
  Serial.print(ALC_NAME);
  //Serial.print(" ");
  //Serial.print(ALC_SUBNAME);
  Serial.print(" version:");
  Serial.print(ALC_VERSION);
  #ifdef BUILD_NAME
  Serial.print(" build:");
  Serial.print(BUILD_NAME);
  #endif
  Serial.println(", booting!");

  // Seems that this can be done before WiFi.begin():
  WiFi.macAddress(macAddressBuffer);
  formatMacAddressInto(macAddressBuffer, ':', formattedMacAddress);

  Serial.print("# MAC address: ");
  Serial.println(formattedMacAddress);

  Serial.print("# Setting up EEPROM...");
  EEPROM.begin((size_t)EEPROMAddresses::END);
  savedEepromVersion =
    (EEPROM.read((off_t)EEPROMAddresses::VERSION_HI) << 8) |
    (EEPROM.read((off_t)EEPROMAddresses::VERSION_LO));
  Serial.print("version:");
  Serial.print(savedEepromVersion);
  if( savedEepromVersion == EEPROM_SAVE_VERSION ) {
    Serial.println("; matches current");
  } else {
    Serial.println("; does NOT match current; can't read");
  }
  
  Serial.print("# Setting up DHT readers...");
  setUpDhts();
  Serial.println("ok");

  setUpWifi();
}

void dumpSensorList() {
  const char *sensorRowFmt = "# %5s %5s\n";
  Serial.println("# Sensors:");
  Serial.printf(sensorRowFmt, "name", "enabled");
  for( int i=0; i<dhtNodeCount; ++i ) {
    const DHTNode &dhtNode = dhtNodes[i];
    Serial.printf(sensorRowFmt, dhtNode.name, dhtNode.autoReadEnabled ? "true" : "false");
  }
}

void dumpPinList() {
  Serial.print("# D1 = "); Serial.println(D1);
  Serial.print("# D2 = "); Serial.println(D2);
  Serial.print("# D3 = "); Serial.println(D3);
  Serial.print("# D4 = "); Serial.println(D4);
  Serial.print("# D5 = "); Serial.println(D5);
  Serial.print("# D6 = "); Serial.println(D6);
  Serial.print("# D7 = "); Serial.println(D7);
  Serial.print("# D8 = "); Serial.println(D8);
  Serial.print("# BUILTIN_LED = "); Serial.println(BUILTIN_LED);
}

struct DHTNode *findDhtByName( const char *dhtName, bool complainOtherwise ) {
  for( int i=0; i<dhtNodeCount; ++i ) {
    DHTNode &dhtNode = dhtNodes[i];
    if( strcmp(dhtNode.name, dhtName) == 0 ) {
      return &dhtNode;
    }
  }
  if( complainOtherwise ) {
    Serial.print("# Sensor '");
    Serial.print(dhtName);
    Serial.println("' not found");
  }
  return nullptr;
}

void commitSettings() {
  uint8_t dhtEnablement = 0;
  for( int i=0; i<dhtNodeCount; ++i ) {
    const DHTNode &dhtNode = dhtNodes[i];
    dhtEnablement |= ((dhtNode.autoReadEnabled ? 1 : 0) << i);
  }
  EEPROM.write((off_t)EEPROMAddresses::VERSION_HI, EEPROM_SAVE_VERSION >> 8);
  EEPROM.write((off_t)EEPROMAddresses::VERSION_LO, EEPROM_SAVE_VERSION);
  
  Serial.print("Writing 0x");
  Serial.print(dhtEnablement, 16);
  Serial.print(" to dhtEnablement byte");
  EEPROM.write((off_t)EEPROMAddresses::DHT_ENABLEMENT, dhtEnablement);
  EEPROM.commit();
  savedEepromVersion = EEPROM_SAVE_VERSION; // If we did it right, lawl.
  Serial.print("# Saved settings to EEPROM; version = ");
  Serial.println(EEPROM_SAVE_VERSION);
}

void setDhtEnabled(DHTNode& dhtNode, bool enabled) {
  dhtNode.autoReadEnabled = enabled;
  publishAttr(dhtNode.name, "enabled", enabled);
}

char lineBuffer[128];
size_t lineBufferLength = 0;

void doCommand( const char *command ) {
  char dhtName[10];
  DHTNode *dhtNode;
  if( sscanf(command, "read %9s", dhtName) ) {
    if( (dhtNode = findDhtByName(dhtName, true)) ) {
      readDht(*dhtNode, true);
      return;
    }
  } else if( (strcmp(command, "enable *") == 0) ) {
    for( int i=0; i<dhtNodeCount; ++i ) {
      setDhtEnabled(dhtNodes[i], true);
    }
  } else if( (strcmp(command, "disable *") == 0) ) {
    for( int i=0; i<dhtNodeCount; ++i ) {
      setDhtEnabled(dhtNodes[i], false);
    }
  } else if( sscanf(command, "enable %9s", dhtName) ) {
    if( (dhtNode = findDhtByName(dhtName, true)) ) {
      setDhtEnabled(*dhtNode, true);
      return;
    }
  } else if( sscanf(command, "disable %9s", dhtName) ) {
    if( (dhtNode = findDhtByName(dhtName, true)) ) {
      setDhtEnabled(*dhtNode, false);
      return;
    }
  } else if( strcmp("list-sensors", command) == 0 ) {
    dumpSensorList();
  } else if( strcmp("list-pins", command) == 0 ) {
    dumpPinList();
  } else if( strcmp("save", command) == 0 ) {
    commitSettings();
  } else if( strcmp("help", command) == 0 ) {
    Serial.println("# Commands:");
    Serial.println("#   list-sensors ; List sensors");
    Serial.println("#   list-pins ; List I/O pin names and numbers");
    Serial.println("#   read dht<N> ; Read and publish sensor data");
    Serial.println("#   enable|disable dht<N>|* ; Enable or disable auto-reading of a sensor");
    Serial.println("#   save ; Save settings to EEPROM");
  } else {
    Serial.print("# Unrecognized command: ");
    Serial.println(command);
  }
}

void loop() {
  while( Serial.available() > 0 ) {
    char inputChar = Serial.read();
    if( inputChar == '\n' ) {
      lineBuffer[lineBufferLength++] = 0;
      doCommand(lineBuffer);
      lineBufferLength = 0;
    } else {
      lineBuffer[lineBufferLength++] = inputChar;
      if(lineBufferLength == sizeof(lineBuffer)) --lineBufferLength;
    }
  }
  reconnect();
  pubSubClient.loop();
  doTasks();
  delay(100);
}
