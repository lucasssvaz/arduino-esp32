// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Matter Manager
#include <Matter.h>
#if !CONFIG_ENABLE_CHIPOBLE
// if the device can be commissioned using BLE, WiFi is not used - save flash space
#include <WiFi.h>
#endif
#include <Preferences.h>

// List of Matter Endpoints for this Node
// Color Light Endpoint
MatterEnhancedColorLight EnhancedColorLight;

// CONFIG_ENABLE_CHIPOBLE is enabled when BLE is used to commission the Matter Network
#if !CONFIG_ENABLE_CHIPOBLE
// WiFi is manually set and started
const char *ssid = "your-ssid";          // Change this to your WiFi SSID
const char *password = "your-password";  // Change this to your WiFi password
#endif

// It will use HSV color to control all Matter Attribute Changes
HsvColor_t currentHSVColor = {0, 0, 0};

// it will keep last OnOff & HSV Color state stored, using Preferences
Preferences matterPref;
const char *onOffPrefKey = "OnOff";
const char *hsvColorPrefKey = "HSV";

// set your board RGB LED pin here
#ifdef RGB_BUILTIN
const uint8_t ledPin = RGB_BUILTIN;
#else
const uint8_t ledPin = 2;  // Set your pin here if your board has not defined LED_BUILTIN
#warning "Do not forget to set the RGB LED pin"
#endif

// set your board USER BUTTON pin here
const uint8_t buttonPin = BOOT_PIN;  // Set your pin here. Using BOOT Button.

// Button control
uint32_t button_time_stamp = 0;                // debouncing control
bool button_state = false;                     // false = released | true = pressed
const uint32_t debouceTime = 250;              // button debouncing time (ms)
const uint32_t decommissioningTimeout = 5000;  // keep the button pressed for 5s, or longer, to decommission

// Set the RGB LED Light based on the current state of the Enhanced Color Light
bool setLightState(bool state, espHsvColor_t colorHSV, uint8_t brighteness, uint16_t temperature_Mireds) {

  if (state) {
#ifdef RGB_BUILTIN
    // currentHSVColor keeps final color result
    espRgbColor_t rgbColor = espHsvColorToRgbColor(currentHSVColor);
    // set the RGB LED
    rgbLedWrite(ledPin, rgbColor.r, rgbColor.g, rgbColor.b);
#else
    // No Color RGB LED, just use the HSV value (brightness) to control the LED
    analogWrite(ledPin, colorHSV.v);
#endif
  } else {
#ifndef RGB_BUILTIN
    // after analogWrite(), it is necessary to set the GPIO to digital mode first
    pinMode(ledPin, OUTPUT);
#endif
    digitalWrite(ledPin, LOW);
  }
  // store last HSV Color and OnOff state for when the Light is restarted / power goes off
  matterPref.putBool(onOffPrefKey, state);
  matterPref.putUInt(hsvColorPrefKey, currentHSVColor.h << 16 | currentHSVColor.s << 8 | currentHSVColor.v);
  // This callback must return the success state to Matter core
  return true;
}

void setup() {
  // Initialize the USER BUTTON (Boot button) GPIO that will act as a toggle switch
  pinMode(buttonPin, INPUT_PULLUP);
  // Initialize the LED (light) GPIO and Matter End Point
  pinMode(ledPin, OUTPUT);

  Serial.begin(115200);

// CONFIG_ENABLE_CHIPOBLE is enabled when BLE is used to commission the Matter Network
#if !CONFIG_ENABLE_CHIPOBLE
  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // Manually connect to WiFi
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\r\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  delay(500);
#endif

  // Initialize Matter EndPoint
  matterPref.begin("MatterPrefs", false);
  // default OnOff state is ON if not stored before
  bool lastOnOffState = matterPref.getBool(onOffPrefKey, true);
  // default HSV color is (21, 216, 25) - Warm White Color at 10% intensity
  uint32_t prefHsvColor = matterPref.getUInt(hsvColorPrefKey, 21 << 16 | 216 << 8 | 25);
  currentHSVColor = {uint8_t(prefHsvColor >> 16), uint8_t(prefHsvColor >> 8), uint8_t(prefHsvColor)};
  EnhancedColorLight.begin(lastOnOffState, currentHSVColor);
  // set the callback function to handle the Light state change
  EnhancedColorLight.onChange(setLightState);

  // lambda functions are used to set the attribute change callbacks
  EnhancedColorLight.onChangeOnOff([](bool state) {
    Serial.printf("Light OnOff changed to %s\r\n", state ? "ON" : "OFF");
    return true;
  });
  EnhancedColorLight.onChangeColorTemperature([](uint16_t colorTemperature) {
    Serial.printf("Light Color Temperature changed to %d\r\n", colorTemperature);
    // get correspondent Hue and Saturation of the color temperature
    HsvColor_t hsvTemperature = espRgbColorToHsvColor(espCTToRgbColor(colorTemperature));
    // keep previous the brightness and just change the Hue and Saturation
    currentHSVColor.h = hsvTemperature.h;
    currentHSVColor.s = hsvTemperature.s;
    return true;
  });
  EnhancedColorLight.onChangeBrightness([](uint8_t brightness) {
    Serial.printf("Light brightness changed to %d\r\n", brightness);
    // change current brightness (HSV value)
    currentHSVColor.v = brightness;
    return true;
  });
  EnhancedColorLight.onChangeColorHSV([](HsvColor_t hsvColor) {
    Serial.printf("Light HSV Color changed to (%d,%d,%d)\r\n", hsvColor.h, hsvColor.s, hsvColor.v);
    // keep the current brightness and just change Hue and Saturation
    currentHSVColor.h = hsvColor.h;
    currentHSVColor.s = hsvColor.s;
    return true;
  });

  // Matter beginning - Last step, after all EndPoints are initialized
  Matter.begin();
  // This may be a restart of a already commissioned Matter accessory
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
    Serial.printf(
      "Initial state: %s | RGB Color: (%d,%d,%d) \r\n", EnhancedColorLight ? "ON" : "OFF", EnhancedColorLight.getColorRGB().r,
      EnhancedColorLight.getColorRGB().g, EnhancedColorLight.getColorRGB().b
    );
    // configure the Light based on initial on-off state and its color
    EnhancedColorLight.updateAccessory();
  }
}

void loop() {
  // Check Matter Light Commissioning state, which may change during execution of loop()
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("");
    Serial.println("Matter Node is not commissioned yet.");
    Serial.println("Initiate the device discovery in your Matter environment.");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code URL: %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());
    // waits for Matter Light Commissioning.
    uint32_t timeCount = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((timeCount++ % 50) == 0) {  // 50*100ms = 5 sec
        Serial.println("Matter Node not commissioned yet. Waiting for commissioning.");
      }
    }
    Serial.printf(
      "Initial state: %s | RGB Color: (%d,%d,%d) \r\n", EnhancedColorLight ? "ON" : "OFF", EnhancedColorLight.getColorRGB().r,
      EnhancedColorLight.getColorRGB().g, EnhancedColorLight.getColorRGB().b
    );
    // configure the Light based on initial on-off state and its color
    EnhancedColorLight.updateAccessory();
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
  }

  // A button is also used to control the light
  // Check if the button has been pressed
  if (digitalRead(buttonPin) == LOW && !button_state) {
    // deals with button debouncing
    button_time_stamp = millis();  // record the time while the button is pressed.
    button_state = true;           // pressed.
  }

  // Onboard User Button is used as a Light toggle switch or to decommission it
  uint32_t time_diff = millis() - button_time_stamp;
  if (button_state && time_diff > debouceTime && digitalRead(buttonPin) == HIGH) {
    button_state = false;  // released
    // Toggle button is released - toggle the light
    Serial.println("User button released. Toggling Light!");
    EnhancedColorLight.toggle();  // Matter Controller also can see the change
  }

  // Onboard User Button is kept pressed for longer than 5 seconds in order to decommission matter node
  if (button_state && time_diff > decommissioningTimeout) {
    Serial.println("Decommissioning the Light Matter Accessory. It shall be commissioned again.");
    EnhancedColorLight = false;  // turn the light off
    Matter.decommission();
    button_time_stamp = millis();  // avoid running decommissining again, reboot takes a second or so
  }
}
