#include <Arduino.h>
#include <WiFi.h>

enum ledmode_t : uint8_t { LED_OFF, LED_ON, LED_1HZ, LED_2HZ, LED_4HZ };
enum buttonstate_t : uint8_t { BTN_RELEASED, BTN_PRESSED, BTN_CLICK, BTN_LONGCLICK };

const uint8_t LED_PIN = 25; // LED_BUILTIN
const bool LED_LEVEL = HIGH;

const uint8_t BTN_PIN = 0;
const bool BTN_LEVEL = LOW;

const char WIFI_SSID[] = "******";
const char WIFI_PSWD[] = "******";

TaskHandle_t blink;
QueueHandle_t queue;

static void setBlink(ledmode_t mode) {
  if (xTaskNotify(blink, mode, eSetValueWithOverwrite) != pdPASS)
    Serial.println("Error setting LED mode!");
}

void blinkTask(void *pvParam) {
  const uint32_t LED_PULSE = 25; // 25 ms.

  ledmode_t ledmode = LED_OFF;

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ! LED_LEVEL);
  while (true) {
    uint32_t notifyValue;

    if (xTaskNotifyWait(0, 0, &notifyValue, ledmode < LED_1HZ ? portMAX_DELAY : 0) == pdTRUE) {
      ledmode = (ledmode_t)notifyValue;
      if (ledmode == LED_OFF)
        digitalWrite(LED_PIN, ! LED_LEVEL);
      else if (ledmode == LED_ON)
        digitalWrite(LED_PIN, LED_LEVEL);
    }
    if (ledmode >= LED_1HZ) {
      digitalWrite(LED_PIN, LED_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(LED_PULSE));
      digitalWrite(LED_PIN, ! LED_LEVEL);
      vTaskDelay(pdMS_TO_TICKS((ledmode == LED_1HZ ? 1000 : ledmode == LED_2HZ ? 500 : 250) - LED_PULSE));
    }
  }
}

void IRAM_ATTR btnISR() {
  const uint32_t CLICK_TIME = 20; // 20 ms.
  const uint32_t LONGCLICK_TIME = 500; // 500 ms.

  static uint32_t lastPressed = 0;

  uint32_t time = millis();
  buttonstate_t state;
  bool btn = digitalRead(BTN_PIN) == BTN_LEVEL;

  if (btn) {
    state = BTN_PRESSED;
    lastPressed = time;
  } else {
    if (time - lastPressed >= LONGCLICK_TIME)
      state = BTN_LONGCLICK;
    else if (time - lastPressed >= CLICK_TIME)
      state = BTN_CLICK;
    else
      state = BTN_RELEASED;
    lastPressed = 0;
  }
  xQueueSendFromISR(queue, &state, NULL);
}

void wifiTask(void *pvParam) {
  const uint32_t WIFI_TIMEOUT = 30000; // 30 sec.

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  while (true) {
    if (! WiFi.isConnected()) {
      uint32_t start = millis();

      WiFi.begin(WIFI_SSID, WIFI_PSWD);
      Serial.print("Connecting to WiFi");
      setBlink(LED_4HZ);
      while ((! WiFi.isConnected()) && (millis() - start < WIFI_TIMEOUT)) {
        Serial.print('.');
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      if (WiFi.isConnected()) {
        Serial.print(" OK (IP ");
        Serial.print(WiFi.localIP());
        Serial.println(')');
        setBlink(LED_1HZ);
      } else {
        WiFi.disconnect();
        Serial.println(" FAIL!");
        setBlink(LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(WIFI_TIMEOUT));
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

static void halt(const char *msg) {
  Serial.println(msg);
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (xTaskCreate(blinkTask, "blink", 1024, NULL, 1, &blink) != pdPASS)
    halt("Error creating blink task!");
  if (xTaskCreatePinnedToCore(wifiTask, "wifi", 4096, NULL, 1, NULL, 1) != pdPASS)
    halt("Error creating WiFi task!");
  queue = xQueueCreate(32, sizeof(buttonstate_t));
  if (! queue)
    halt("Error creating queue!");
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), btnISR, CHANGE);
}

void loop() {
  buttonstate_t state;

  if (xQueueReceive(queue, &state, portMAX_DELAY) == pdTRUE) {
    Serial.print("Button ");
    switch (state) {
      case BTN_RELEASED:
        Serial.println("released");
        break;
      case BTN_PRESSED:
        Serial.println("pressed");
        break;
      case BTN_CLICK:
        Serial.println("clicked");
        break;
      case BTN_LONGCLICK:
        Serial.println("long clicked");
        break;
    }
  }
}
