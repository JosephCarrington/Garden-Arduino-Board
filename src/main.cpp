#include <ezButton.h>
#include <ArduinoJson.h>

#include <WiFi.h>
#include <Wire.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <arduino-timer.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define VS0_PIN 25
#define VS1_PIN 26
#define VS2_PIN 27
#define VS3_PIN 14
#define VSIGNAL_PIN 12

#define SS0_PIN 23
#define SS1_PIN 13
#define SS2_PIN 4
#define SS3_PIN 17
#define SSIGNAL_PIN 36 // input!

// All below are INPUT_PULLUP
#define BUTTON1_PIN 19
#define BUTTON2_PIN 18
#define BUTTON3_PIN 5

ezButton leftButton(BUTTON1_PIN);
ezButton rightButton(BUTTON2_PIN);
ezButton actionButton(BUTTON3_PIN);

#define LED_RED_PIN 15
#define LED_YELLOW_PIN 2
#define LED_GREEN_PIN 16

#define DO_NOT_TOUCH1_PIN 33
#define DO_NOT_TOUCH2_PIN 32

// WIFI Stuff
const char *ssid = "CARRINGTON";
const char *password = "3522335773";

// Averaging values from sensors
const int numReadings = 8;

const int totalSensors = 16;
int readings[totalSensors][numReadings];
int readIndexes[totalSensors];
int totals[totalSensors];
int averages[totalSensors];

// UI
enum screen
{
  homeScreen,
  individualSensorScreen,
  allSensorsScreen,
  individualValveScreen,
  allValvesScreen,
  setupScreen
};
screen currentScreen = homeScreen;

int sensorScreenSensor = 0;
int valveScreenValve = 0;

void selectSensorChannel(int channel)
{
  digitalWrite(SS0_PIN, bitRead(channel, 0));
  digitalWrite(SS1_PIN, bitRead(channel, 1));
  digitalWrite(SS2_PIN, bitRead(channel, 2));
  digitalWrite(SS3_PIN, bitRead(channel, 3));
}

void setNextSensor()
{
  sensorScreenSensor++;
  if (sensorScreenSensor > 15)
  {
    sensorScreenSensor = 0;
  }
  selectSensorChannel(sensorScreenSensor);
}
void setNextValve()
{
  valveScreenValve++;
  if (valveScreenValve > 15)
    valveScreenValve = 0;
}

int averageAndReadFromChannel(int channel)
{
  selectSensorChannel(channel);
  totals[channel] -= readings[channel][readIndexes[channel]];
  readings[channel][readIndexes[channel]] = analogRead(SSIGNAL_PIN);
  totals[channel] += readings[channel][readIndexes[channel]];
  readIndexes[channel]++;
  if (readIndexes[channel] >= numReadings)
    readIndexes[channel] = 0;

  averages[channel] = totals[channel] / numReadings;
  delay(1);
  return averages[channel];
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    //  handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}
// Websocket stuff
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
StaticJsonDocument<512> doc;
auto timer = timer_create_default();

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void notifyClients(String message)
{
  digitalWrite(LED_GREEN_PIN, HIGH);
  ws.textAll(message);
  timer.in(1000, [](void *) -> bool
           { digitalWrite(LED_GREEN_PIN, LOW); return true; });
}

// Map electrons to dry / wet numbers
const int dryState = 3387;
const int wetState = 1620;
int mapValueToPercentage(int v)
{
  return map(v, wetState, dryState, 100, 0);
}
JsonArray sensorData = doc.createNestedArray("sensors");

bool sendWebsocket(void *)
{

  for (int i = 0; i <= 15; i++)
  {
    sensorData[i] = mapValueToPercentage(averageAndReadFromChannel(i));
  }
  String json;
  serializeJson(doc, json);
  Serial.println(json);
  notifyClients(json);
  return true;
}

void setup()
{
  // Safety first!
  pinMode(DO_NOT_TOUCH1_PIN, INPUT);
  pinMode(DO_NOT_TOUCH2_PIN, INPUT);

  // Buttons
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);

  leftButton.setDebounceTime(100);
  rightButton.setDebounceTime(100);
  actionButton.setDebounceTime(100);

  // Sensor Demux
  pinMode(SS0_PIN, OUTPUT);
  gpio_pad_select_gpio(SS1_PIN);
  pinMode(SS1_PIN, OUTPUT);
  pinMode(SS2_PIN, OUTPUT);
  pinMode(SS3_PIN, OUTPUT);
  pinMode(SSIGNAL_PIN, INPUT);

  // LEDS
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);

  // UI
  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  delay(2000);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);

  display.println("Connecting to ");
  display.println(ssid);
  display.display();
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    display.print(".");
    display.display();
  }

  initWebSocket();
  server.begin();

  // Timer to send websocket
  timer.every(1000 * 30, sendWebsocket);
}

int button1State, button2State, button3State;
void loop()
{
  // Cleanup
  ws.cleanupClients();

  // Tick!
  timer.tick();
  leftButton.loop();
  rightButton.loop();
  actionButton.loop();

  // LEDS can show data!
  if (WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(LED_YELLOW_PIN, HIGH);
  }
  else
  {
    digitalWrite(LED_YELLOW_PIN, LOW);
  }

  // Handle buttons
  if (leftButton.isPressed())
  {
    switch (currentScreen)
    {
    case homeScreen:
      currentScreen = individualSensorScreen;
      break;
    case individualSensorScreen:
      currentScreen = allSensorsScreen;
      break;
    default:
      currentScreen = homeScreen;
    }
  }

  if (rightButton.isPressed())
  {
    switch (currentScreen)
    {
    case individualSensorScreen:
      setNextSensor();
      break;
    default:
      break;
    }
  }

  // Display stuff on display
  display.clearDisplay();
  display.setCursor(0, 0);
  switch (currentScreen)
  {
  case homeScreen:
    display.setCursor(0, 10);
    display.println("Connected with IP:");
    display.println(WiFi.localIP());
    break;
  case individualSensorScreen:
    display.setCursor(0, 10);
    display.print("Sensor ");
    display.println(sensorScreenSensor);
    display.println(mapValueToPercentage(averageAndReadFromChannel(sensorScreenSensor)));
    break;
  case allSensorsScreen:
    for (int i = 1; i <= 16; i++)
    {
      byte length = display.print(mapValueToPercentage(averageAndReadFromChannel(i - 1)));
      // Pad right
      for (byte l = length; l <= 3; l++)
      {
        display.print(" ");
      }
      if (i % 4 == 0)
        display.println("");
    }
    break;
  default:
    display.println("No screen to display :(");
  }
  display.display();
}