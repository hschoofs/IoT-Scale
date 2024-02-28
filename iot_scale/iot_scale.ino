#include "soc/rtc_cntl_reg.h"
#include "soc/rtc.h"
#include "driver/rtc_io.h"

#include <WiFi.h>
#include <PubSubClient.h>

#include "HX711.h"

#include <time.h>
#include <RTClib.h>

#define debug false

RTC_DS3231 rtc;

HX711 scale;

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

// load cell
// const int LOADCELL_DOUT_PIN = 25;
// const int LOADCELL_SCK_PIN = 26;
const int LOADCELL_DOUT_PIN = 3;
const int LOADCELL_SCK_PIN = 4;

// NTP
const char *NTP_SERVER = "ch.pool.ntp.org";
const char *TZ_INFO = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";
tm timeinfo;
time_t now;

// WiFi
const char *ssid = "";
const char *password = "";

// MQTT Broker
const char *mqtt_broker = "";
const char *topic = "tp1267";
// const char *mqtt_username = "";
// const char *mqtt_password = "";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  // Set software serial baud to 115200;
  pinMode(GPIO_NUM_44, OUTPUT);
  digitalWrite(GPIO_NUM_44, HIGH);
  delay(200);
  if (debug) {
    Serial.begin(115200);
  }
  // Connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (debug) {
      Serial.println("Connecting to WiFi..");
    }
  }
  pinMode(GPIO_NUM_43, OUTPUT);
  digitalWrite(GPIO_NUM_43, HIGH);


  // pinMode(GPIO_NUM_2, INPUT_PULLUP);
  // rtc_gpio_pullup_en(GPIO_NUM_2);

  //gpio_num_t pin = (gpio_num_t)PIN_DISABLE_5V;
  // rtc_gpio_set_direction(GPIO_NUM_2, RTC_GPIO_MODE_INPUT);
  // set the pin as pull-up
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0);  
  rtc_gpio_pulldown_dis(GPIO_NUM_2);
  rtc_gpio_pullup_en(GPIO_NUM_2);

  // rtc_gpio_hold_dis(GPIO_NUM_4);


  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(-100.29);
  scale.set_offset(121562);
  if (debug) {
    Serial.println("Connected to the Wi-Fi network");
  }
  //connecting to a mqtt broker
  client.setServer(mqtt_broker, mqtt_port);
  // client.setCallback(callback);
  while (!client.connected()) {
    String client_id = "esp32-client-";
    client_id += String(WiFi.macAddress());
    if (debug) {
      Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
    }
    if (client.connect(client_id.c_str())) {
      if (debug) {
        Serial.println("Public EMQX MQTT broker connected");
      }
    } else {
      if (debug) {
        Serial.print("failed with state ");
        Serial.print(client.state());
      }
      //delay(2000);
      sleep();
    }
  }
  configTime(0, 0, NTP_SERVER);
  setenv("TZ", TZ_INFO, 1);
  if (getNTPtime(10)) {  // wait up to 10sec to sync
  } else {
    if (debug) {
      Serial.println("Time not set");
    }
    // sleep();
    // ESP.restart();
  }

  if (!rtc.begin()) {
    if (debug) {
      Serial.println("Couldn't find RTC");
      Serial.flush();
    }
    //abort();
    sleep();
  }
  rtc.disable32K();                 //we don't need the 32K Pin, so disable it
  rtc.writeSqwPinMode(DS3231_OFF);  // stop oscillating signals at SQW Pin, otherwise setAlarm1 will fail
  rtc.clearAlarm(1);                // set alarm 1, 2 flag to false (so alarm 1, 2 didn't happen so far)
  rtc.clearAlarm(2);
  rtc.disableAlarm(2);
  rtc.adjust(DateTime(uint32_t(now + 7203)));
}

bool getNTPtime(int sec) {

  {
    uint32_t start = millis();
    do {
      time(&now);
      localtime_r(&now, &timeinfo);
      if (debug) {
        Serial.print(".");
      }
      delay(10);
    } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));
    if (timeinfo.tm_year <= (2016 - 1900)) return false;  // the NTP call was not successful
    if (debug) {
      Serial.print("now ");
      Serial.println(now);
    }
    char time_output[30];
    strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
    if (debug) {
      Serial.println(time_output);
      Serial.println();
    }
  }
  return true;
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    if (debug) {
      Serial.print("Attempting MQTT connection...");
    }
    // Create a random client ID
    String clientId = "scale-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      if (debug) {
        Serial.println("connected");
      }
    } else {
      if (debug) {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
      }
      sleep();
    }
  }
}

void sleep() {
  digitalWrite(GPIO_NUM_44, LOW);
  digitalWrite(GPIO_NUM_43, LOW);
  if (debug) {
    Serial.println("Going to sleep now");
    Serial.flush();
  }
  WiFi.mode(WIFI_OFF);  // important for low deepsleep current
  btStop();

  esp_deep_sleep_start();
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (scale.is_ready()) {
    //long reading = scale.read();
    long reading = scale.get_units(10);
    snprintf(msg, MSG_BUFFER_SIZE, "xvxvxvxvxvvvvvx");
    if (debug) {
      Serial.print("HX711 reading: ");
      Serial.println(reading);
      Serial.print("Publish message: ");
      Serial.println(String(reading));
    }
    client.publish("tp1267", String(reading).c_str());
    // scale.power_down();
    // rtc_gpio_hold_en(GPIO_NUM_4);
    // client.publish("tp1267", msg);
  }

  //if (!rtc.setAlarm1(DateTime(2024, 11, 15, 15, 0, 0), DS3231_A1_Minute))  // trigger alarm every hour
  if (!rtc.setAlarm1(DateTime(2024, 11, 15, 15, 0, 0), DS3231_A1_Second))  // trigger alarm every minute
                                                                           /**
  if (!rtc.setAlarm1(
        rtc.now() + TimeSpan(10),   // schedule an alarm 10 seconds in the future
        DS3231_A1_Second            // this mode triggers the alarm when the seconds match
      ))
      **/
  {

    if (debug) {
      Serial.println("Error, alarm wasn't set!");
    }
  } else {
    if (debug) {
      Serial.println("Alarm will happen every minute!");
    }
  }

  sleep();
}
