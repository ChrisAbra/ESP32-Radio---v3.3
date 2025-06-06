#include "Arduino.h"
#include "Audio.h"
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <queue>

#include "OneButton.h"
#include "ESPmDNS.h"
#include "SPIFFS.h"

// ESP32 I2S digital output pins
#define I2S_BCLK 18 // GPIO 26 (CLOCK Output - serial clock. connects to BCLK pin on  I2S DAC)
#define I2S_DOUT 17 // GPIO 25 (DATA Output - the digital output. connects to DIN pin on I2S DAC)
#define I2S_LRC 16  // GPIO 27 (SELECT Output - left/right control. connects to LRC/LCK/WS/WSEL pin on I2S DAC)

#define USE_MONO false // Use mono audio

const char *DEVICE_NAME = "Internet Radio Reciever";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

OneButton RADIO_CHANNEL_0_BUTTON = OneButton(18, true, true);
// ezButton RADIO_CHANNEL_1_BUTTON = ezButton(11);
// ezButton RADIO_CHANNEL_3_BUTTON = ezButton(12);
// ezButton WIFI_RESET_BUTTON = ezButton(13);
OneButton BLUETOOTH_BUTTON = OneButton(19, true, true);

std::string activeChannel;

enum class PlayType
{
  NONE,
  INTERNET_URL,
  TTS
};

struct PlayRequest
{

  inline bool operator==(const PlayRequest &other)
  {
    return (type == other.type && value == other.value);
  }
  PlayType type;
  std::string value;
  bool isInterruptable()
  {
    switch (type)
    {
    case PlayType::NONE:
      return true;
    case PlayType::TTS:
      return false;
    case PlayType::INTERNET_URL:
      return true;
    }
    return true;
  }
  bool isTemporary()
  {
    switch (type)
    {
    case PlayType::NONE:
      return false;
    case PlayType::TTS:
      return true;
    case PlayType::INTERNET_URL:
      return false;
    }
    return true;
  }

  PlayRequest()
  {
    type = PlayType::NONE;
  };
  PlayRequest(PlayType playType, std::string value) : type(playType), value(value) {};
};

PlayRequest pausePlayRequest = PlayRequest{PlayType::NONE, ""};

PlayRequest activePlayRequest;

std::queue<PlayRequest> requestQueue;

WiFiManager wifiManager;
bool hasWifiConnection = false;

Audio audio;
int volume = 8;

void addToQueue(PlayRequest playRequest)
{
  if (requestQueue.size() > 0)
  {
    if (requestQueue.back() == playRequest)
    {
      return; // should stop duplicates
    }
  }
  if (playRequest.type != PlayType::NONE && playRequest.value.length() == 0)
  {
    return;
  }

  requestQueue.push(playRequest);
}

void checkButtons()
{
  // RADIO_CHANNEL_0_BUTTON.tick();
  // BLUETOOTH_BUTTON.tick();
}

void checkPots()
{
  // vTaskDelay(1);
  //  Serial.println("Check Pots");
}

std::string indexHtml;

void webSocketProcessor(std::string message)
{

  if (message == "play")
  {
    addToQueue(PlayRequest{PlayType::INTERNET_URL, activeChannel});
  }
  else if (message == "pause")
  {
    addToQueue(pausePlayRequest);
  }

  else if (message.substr(0, 5) == "play:")
  {
    std::string channelUrl = message.substr(5, message.length() - 5);
    Serial.println(channelUrl.c_str());
    addToQueue(PlayRequest{PlayType::INTERNET_URL, channelUrl});
  }
  else if (message.substr(0, 7) == "volume:")
  {
    std::string volumeText = message.substr(7, 2);
    Serial.println(volumeText.c_str());
    volume = atoi(volumeText.c_str());
    audio.setVolume(volume);
  }

  else if (message == "info")
  {
    if (activePlayRequest.type == PlayType::NONE)
    {
      ws.textAll("i:paused");
    }
    else
    {
      ws.textAll(("i:" + activePlayRequest.value).c_str());
    }
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    std::string message = (char *)data;
    // Serial.println(message.c_str());
    webSocketProcessor(message);
  }
}

void eventHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    // Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    // Serial.printf("WebSocket client #%u disconnected\n", client->id());
    ws.cleanupClients();
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void setupWebserver()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  File file = SPIFFS.open("/index.html");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available())
  {

    indexHtml += file.read();
  }
  file.close();

  if (!MDNS.begin("git-radio"))
  { // Set the hostname to "Rams-RT20.local"
    Serial.println("Error setting up MDNS responder!");
    while (1)
    {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");

  ws.onEvent(eventHandler);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html; charset=utf-8", indexHtml.c_str()); });

  server.begin();
}

void setupWifi()
{
  wifiManager.setConfigPortalTimeout(180);
  hasWifiConnection = wifiManager.autoConnect(DEVICE_NAME);
}

void setupRadio()
{
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.forceMono(USE_MONO); // Force mono for single speaker;
  // audio.setTone(20, 10, 0);
  audio.setVolume(volume);

  /*
  RADIO_CHANNEL_0_BUTTON.attachClick([]()
                                     {
                                    addToQueue(PlayRequest{PlayType::INTERNET_URL, CHANNELS[0]}); });

  BLUETOOTH_BUTTON.attachClick([]()
                               {
                                  addToQueue(PlayRequest{PlayType::INTERNET_URL, CHANNELS[1]}); });
  */
}

std::string defaultStream = "https://stream-relay-geo.ntslive.net/stream";

void setup()
{
  Serial.begin(115200);
  Serial.println("Boot");

  setupWifi();

  setupRadio();

  setupWebserver();

  addToQueue(PlayRequest{PlayType::INTERNET_URL, defaultStream});
}

void pauseRadio()
{
  audio.stopSong();
  ws.textAll("i:paused");
}

void startTTS()
{
  Serial.println("Announcing:");
  Serial.println(activePlayRequest.value.c_str());
  audio.connecttospeech(activePlayRequest.value.c_str(), "en");
}
void startStream()
{
  Serial.println("======== Starting radio stream at: ==========");
  Serial.println(activePlayRequest.value.c_str());

  bool successful = audio.connecttohost(activePlayRequest.value.c_str());
  activeChannel = activePlayRequest.value;
  ws.textAll(("i:" + activePlayRequest.value).c_str());
}

void checkQueue()
{
  if (requestQueue.size() == 0)
  {
    return;
  }

  Serial.println(requestQueue.size());

  if (!audio.isRunning() || activePlayRequest.isInterruptable())
  {

    PlayRequest nextPlayRequest = requestQueue.front();
    if (nextPlayRequest.isTemporary())
    {
      addToQueue(activePlayRequest);
    }

    if (activePlayRequest == nextPlayRequest)
    {
      requestQueue.pop();
      return;
    }
    activePlayRequest = nextPlayRequest;

    if (activePlayRequest.type == PlayType::INTERNET_URL)
    {
      startStream();
    }
    else if (activePlayRequest.type == PlayType::TTS)
    {
      startTTS();
    }
    else if (activePlayRequest.type == PlayType::NONE)
    {
      pauseRadio();
    }
  }
}

void loop()
{
  ws.cleanupClients();
  /*   checkButtons(); // sweeps the buttons for presses
    checkPots();    // sweeps the pots for new values
   */
  checkQueue();
  audio.loop();
}

void audio_showstreamtitle(const char *info)
{
  ws.textAll(((std::string)"title:"+(std::string)info).c_str());
}

void audio_info(const char *info)
{
  Serial.print("info        ");
  Serial.println(info);
}
