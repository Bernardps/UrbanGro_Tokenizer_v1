// Author: Bernardps
// Project: UrbanGro Tokenizer_V1
// Created: 12 April 2025
// Last updated: 16 May 2025

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ThingSpeak.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "secrets.h"  // <-- Include your credentials here

// Sensor pins and types
#define DHTPIN D5
#define DHTTYPE DHT22
#define UVOUT A0

// Sensor instances
DHT dht(DHTPIN, DHTTYPE);
Adafruit_VEML7700 veml = Adafruit_VEML7700();

// Time client (UTC)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

String chipId;

void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();
  Wire.begin(D2, D1); // I2C for VEML7700

  if (!veml.begin()) {
    Serial.println("Failed to initialize VEML7700!");
    while (1);
  }

  veml.setGain(VEML7700_GAIN_1_8);
  veml.setIntegrationTime(VEML7700_IT_25MS);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  chipId = String(ESP.getChipId(), HEX);
  timeClient.begin();
  timeClient.update();

  ThingSpeak.begin(tsClient);
}

void loop() {
  timeClient.update();
  delay(2000);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping this cycle.");
    return;
  }

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  float rawLux = veml.readALS();
  float resolution = 2.1504;
  float luxUncorrected = rawLux * resolution;
  float correctedLux = luxUncorrected;

  if (luxUncorrected < 20000) {
    correctedLux = 6.0135e-13 * pow(luxUncorrected, 4)
                 - 9.3924e-9  * pow(luxUncorrected, 3)
                 + 8.1488e-5  * pow(luxUncorrected, 2)
                 + 1.0023     * luxUncorrected;
  }

  if (correctedLux > 150000) {
    correctedLux = 150000;
  }

  int uvLevel = analogRead(UVOUT);
  float outputVoltage = uvLevel * (3.3 / 1023.0);
  float uvIntensity = mapfloat(outputVoltage, 1.0, 2.8, 0.0, 15.0);
  if (uvIntensity < 0) uvIntensity = 0.0;

  if (humidity == 0 || temperature == 0 || correctedLux <= 0) {
    Serial.println("Detected invalid readings, skipping.");
    return;
  }

  Serial.printf("Humidity: %.2f %%\tTemperature: %.2f °C\tLux: %.0f lx\tUV: %.2f mW/cm^2\n",
                humidity, temperature, correctedLux, uvIntensity);

  sendToThingSpeak(humidity, temperature, correctedLux, uvIntensity);
  sendToMongoAPI(humidity, temperature, correctedLux, uvIntensity);

  delay(60000);
}

void sendToThingSpeak(float h, float t, float lux, float uv) {
  ThingSpeak.setField(1, h);
  ThingSpeak.setField(2, t);
  ThingSpeak.setField(3, lux);
  ThingSpeak.setField(4, uv);

  int x = ThingSpeak.writeFields(THINGSPEAK_CHANNEL, THINGSPEAK_API_KEY);
  if (x == 200) {
    Serial.println("ThingSpeak update successful.");
  } else {
    Serial.print("ThingSpeak update failed. Code: ");
    Serial.println(x);
  }
}

void sendToMongoAPI(float h, float t, float lux, float uv) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (https.begin(client, MONGODB_URL)) {
    https.addHeader("Content-Type", "application/json");

    String jsonPayload = "{";
    jsonPayload += "\"device\":\"ESP8266_" + chipId + "\",";
    jsonPayload += "\"humidity\":" + String(h, 2) + ",";
    jsonPayload += "\"temperature\":" + String(t, 2) + ",";
    jsonPayload += "\"lux\":" + String((int)round(lux)) + ",";
    jsonPayload += "\"uv\":" + String(uv, 2) + ",";
    jsonPayload += "\"timestamp\":\"" + getISOTime() + "\"";
    jsonPayload += "}";

    int httpCode = https.POST(jsonPayload);
    Serial.printf("[HTTPS] POST... code: %d\n", httpCode);

    if (httpCode > 0) {
      Serial.println("Server response:");
      Serial.println(https.getString());
    } else {
      Serial.printf("POST failed: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println("Failed to connect to MongoDB API.");
  }
}

String getISOTime() {
  unsigned long epoch = timeClient.getEpochTime();
  time_t rawtime = epoch;
  struct tm* ti = gmtime(&rawtime);
  char isoTime[25];
  sprintf(isoTime, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
          ti->tm_hour, ti->tm_min, ti->tm_sec);
  return String(isoTime);
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
