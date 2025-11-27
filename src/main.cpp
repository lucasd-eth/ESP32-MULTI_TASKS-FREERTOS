// Refactored for FreeRTOS multitasking: Task 1 (blink LED), Task 2 (read DHT22 & PIR), Task 3 (send data to server)
#include <Arduino.h>

#include <DHTesp.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>



// Pin mapping for Wokwi simulation
#define DHT_PIN     13      // DHT22 data pin connected to GPIO13
#define PIR_PIN     15      // PIR OUT pin connected to GPIO15
#define LED_PIR     32      // LED for PIR, connected to GPIO32 (see diagram.json)
#define LED_STATUS  2       // Status LED, connected to GPIO2 (see diagram.json)

const char* ssid = "Wokwi-GUEST";
const char* password = "";
String serverName = "https://postman-echo.com/post";



DHTesp dht;

// Shared sensor data
float g_temperature = 0;
float g_humidity = 0;
int g_motion = 0;
SemaphoreHandle_t xSensorDataMutex;

// Task handles (optional)
TaskHandle_t blinkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t sendTaskHandle = NULL;

// Task 1: Blink LED (blink both GPIO2 and GPIO32 for Wokwi compatibility)
void TaskBlink(void *pvParameters) {
  (void) pvParameters;
  pinMode(LED_STATUS, OUTPUT);
  pinMode(LED_PIR, OUTPUT); // Also blink LED_PIR for Wokwi
  while (1) {
    digitalWrite(LED_STATUS, HIGH);
    digitalWrite(LED_PIR, HIGH); // Blink both
    vTaskDelay(500 / portTICK_PERIOD_MS);
    digitalWrite(LED_STATUS, LOW);
    digitalWrite(LED_PIR, LOW);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// Task 2: Read DHT22 & PIR (do not control LED_PIR here to avoid conflict with blink task)
void TaskSensorRead(void *pvParameters) {
  (void) pvParameters;
  pinMode(PIR_PIN, INPUT);
  // pinMode(LED_PIR, OUTPUT); // Already set in blink task
  dht.setup(DHT_PIN, DHTesp::DHT22);
  while (1) {
    TempAndHumidity data = dht.getTempAndHumidity();
    int motion = digitalRead(PIR_PIN);
    // Do not control LED_PIR here
    if (!isnan(data.temperature) && !isnan(data.humidity)) {
      if (xSensorDataMutex != NULL && xSemaphoreTake(xSensorDataMutex, (TickType_t)10) == pdTRUE) {
        g_temperature = data.temperature;
        g_humidity = data.humidity;
        g_motion = motion;
        xSemaphoreGive(xSensorDataMutex);
      }
      Serial.printf("[SENSOR] Temp: %.1f*C | Humidity: %.1f%%\n", data.temperature, data.humidity);
    } else {
      Serial.println("[SENSOR][ERROR] DHT22 read failed!");
    }
    Serial.printf("[SENSOR] PIR Motion: %s\n", motion ? "DETECTED" : "NONE");
    vTaskDelay(5000 / portTICK_PERIOD_MS); // 5 seconds
  }
}

// Task 3: Send sensor data to server using HTTP POST (JSON)
void TaskSendingData(void *pvParameters) {
  (void) pvParameters;
  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SEND] Waiting for WiFi connection...");
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
  while (1) {
    float temp = 0, hum = 0;
    int motion = 0;
    if (xSensorDataMutex != NULL && xSemaphoreTake(xSensorDataMutex, (TickType_t)10) == pdTRUE) {
      temp = g_temperature;
      hum = g_humidity;
      motion = g_motion;
      xSemaphoreGive(xSensorDataMutex);
    }
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/json");
      // Prepare JSON
      StaticJsonDocument<200> doc;
      doc["temp"] = temp;
      doc["humid"] = hum;
      doc["motion"] = motion;
      String httpRequestData;
      serializeJson(doc, httpRequestData);
      Serial.println(httpRequestData);
      int httpResponseCode = http.POST(httpRequestData);
      if (httpResponseCode > 0) {
        Serial.print("HTTP Response code: "); Serial.println(httpResponseCode);
        String payload = http.getString();
        Serial.println(payload);
      } else {
        Serial.print("Error code: "); Serial.println(httpResponseCode);
      }
      http.end();
    } else {
      Serial.println("WiFi Disconnected");
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS); // 3 seconds
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 FreeRTOS Multi-tasking Example");

  // WiFi setup
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" WiFi Connected!");

  // Mutex for sensor data
  xSensorDataMutex = xSemaphoreCreateMutex();

  // Create tasks
  xTaskCreatePinnedToCore(TaskBlink, "TaskBlink", 1024, NULL, 1, &blinkTaskHandle, 1);
  xTaskCreatePinnedToCore(TaskSensorRead, "TaskSensorRead", 2048, NULL, 2, &sensorTaskHandle, 1);
  xTaskCreatePinnedToCore(TaskSendingData, "TaskSendingData", 8192, NULL, 1, &sendTaskHandle, 1);
}

void loop() {
  // Nothing here, all work is done in tasks
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
