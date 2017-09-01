// main.cpp
// Copyright 2017 Lukas Friedrichsen
// License: Modified BSD-License
//
// 2017-02-28

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include "../lib/user_config.h"

#define OLED_RESET 16
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
  #error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

#define onboardLED 2
int32_t rssi;

// Measures and returns the wireless-networks RSSI-value
int32_t getRSSI(String target_ssid) {
  uint8_t available_networks = WiFi.scanNetworks();

  String network_ssid;
  for (int network = 0; network < available_networks; network++) {
    network_ssid = WiFi.SSID(network);
    Serial.println(target_ssid+"   "+network_ssid+"   "+network_ssid.compareTo(target_ssid));
    if (network_ssid.compareTo(target_ssid) == 0 && network_ssid.length() == target_ssid.length()) {
      return WiFi.RSSI(network);
    }
  }

  // Return 0 if the specified network wasn't found
  return 0;
}

void setup() {
  // Initializing...
  Serial.begin(115200);
  Serial.println("Initializing...");

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Initialize with the I2C addr 0x3C (for the 128x64)

  pinMode(onboardLED, OUTPUT);
  digitalWrite(onboardLED, HIGH);

  // Set the textsize and -color
  display.setTextColor(WHITE);
}

void loop() {
  rssi = getRSSI(TARGET_SSID);

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(0,0);
  display.printf("SSID: %s\n",TARGET_SSID);
  display.setCursor(0,12);
  display.print("Status: ");
  if (rssi) {
    display.println("found!");
  }
  else {
    display.println("not found!");
  }
  display.setTextSize(2);
  display.setCursor(0,36);
  display.printf("RSSI: %d\n",rssi);
  display.display();

  if (rssi > BLINK_THRESHOLD && rssi != 0) {
    digitalWrite(onboardLED, LOW);
  }
  else {
    digitalWrite(onboardLED, HIGH);
  }
}
