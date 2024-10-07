#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <String.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include "./DNSServer.h"
#include "coap-simple.h"

WiFiUDP udp;
Coap coap(udp);

const char *AP_ssid = "Theos_DNS";
const char *AP_password = "123654789";

bool IS_CONNECTED_TO_WIFI = false;

struct {
  char WIFI_ssid[32]     = "";
  char WIFI_password[32] = "";
  char token[32]         = "";
  char server_auth[32]   = "";  // with port, example: 10.10.10.10:82
  char server_whoami[32] = "";  // with port, example: 10.10.10.10:81
  char server_coap[32]   = "";  // with port, example: 10.10.10.10:85
  char are_data_ok[3]    = "ok";
} _settings;

#define RESET_PIN 19


DNSServer _dnsServer;

IPAddress _lastClinetIp;

#define SCREEN_WIDTH  128 
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1 
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WebServer _server(80);

void show_splash_screen();
void show_wifi_details();
void show_dns_server_address(String ipAddress);
void show_token_is_invalid();

void handle_index();
void handle_saveSettings();
void handle_notFound();

bool has_wifi_or_connect();

IPAddress localIp();

String announceServerMyIp();

void registerMyIp( void * pvParameters );

TaskHandle_t registerMyIp_task;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  delay(100);

  pinMode(RESET_PIN, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) { 
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.setTextColor(WHITE);
  show_splash_screen();

  EEPROM.begin(sizeof(_settings));
  unsigned int addr = 0;
  EEPROM.get(addr, _settings);

  Serial.println("auth server: ");
  Serial.println(_settings.server_auth);

  Serial.println("whoami server: ");
  Serial.println(_settings.server_whoami);

  Serial.println("coap server: ");
  Serial.println(_settings.server_coap);

  if (!has_wifi_or_connect()) {
    show_wifi_details();

    IPAddress local_ip(10, 90, 90, 10);
    IPAddress gateway(10, 90, 90, 10);
    IPAddress subnet(255, 255, 255, 0);

    Serial.println("Setting AP (Access Point)…");
    WiFi.softAP(AP_ssid, AP_password);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    delay(100);

    _server.on("/", HTTP_GET, handle_index);
    _server.on("/saveSettings", HTTP_POST, handle_saveSettings);

    _server.onNotFound(handle_notFound);

    _server.begin();
    Serial.println("HTTP server started");

  } else {
    Serial.println("wifi founded");
    IPAddress coapServer;
    uint16_t coapPort;

    String serverCoap = String(_settings.server_coap);

    coapServer.fromString(serverCoap.substring(0, serverCoap.indexOf(":")));
    coapPort = atoi(serverCoap.substring(serverCoap.indexOf(":")+1, serverCoap.length()).c_str());

    Serial.println(coapServer.toString());
    Serial.println(serverCoap.substring(serverCoap.indexOf(":")+1, serverCoap.length()));
    Serial.println(coapPort);

    Serial.println("-------------------------");
    if (_dnsServer.start((const byte)53, coapServer, coapPort)) {
      Serial.println("DNS server started");
    }
    Serial.println("-------------------------");

    _dnsServer.setCOAP(&coap);
  }


    xTaskCreatePinnedToCore(
                    registerMyIp,        /* Task function. */
                    "registerMyIp",      /* name of task. */
                    10000,               /* Stack size of task */
                    NULL,                /* parameter of the task */
                    1,                   /* priority of the task */
                    &registerMyIp_task,  /* Task handle to keep track of created task */
                    0);                  /* pin task to core 0 */   

}

void loop() {

  _dnsServer.checkToResponse();

  if (!has_wifi_or_connect()) {
    _server.handleClient();
  }

  if(digitalRead(RESET_PIN) == LOW){
    strncpy(_settings.are_data_ok, "no", sizeof("no"));
    unsigned int addr = 0;
    EEPROM.put(addr, _settings);
    EEPROM.commit();
    Serial.println("RESETTING!, settings has been saved");

    ESP.restart();
  }

  // sleep(1);
  // Serial.print("getFreeHeap: ");
  // Serial.println(ESP.getFreeHeap());
  // Serial.print("getMinFreeHeap: ");
  // Serial.println(ESP.getMinFreeHeap());
}

void registerMyIp( void * pvParameters ){
  Serial.print("registerMyIp loop core =>");
  Serial.println(xPortGetCoreID());
  for(;;){
    if(has_wifi_or_connect()){
      IPAddress askedIp;
      askedIp = localIp();

      if (_lastClinetIp != askedIp) {
        String res = announceServerMyIp(askedIp);
        if (res == "already added" || res == "added") {
          for(int i = 0; i < 4; i++){
            _lastClinetIp[i] = askedIp[i];
          }
          Serial.println("my ip is Saved and is authorized");
        }else{
          Serial.println(res);
          show_token_is_invalid();
        }
      }
      Serial.print("my external ip is: ");
      Serial.println(askedIp.toString());
    }
    delay(30000);
  } 
}

String announceServerMyIp(IPAddress &newIp) {
  String pathAndParams = "/tap-in?token=" + String(_settings.token) + "&ip=" + newIp.toString();
  String serverName = "http://" + String(_settings.server_auth) + pathAndParams;

  HTTPClient http;
  http.begin(serverName.c_str());

  int httpResponseCode = http.GET();

  String payload = "";
  if (httpResponseCode > 0) {
    payload = http.getString();
  } else {
    Serial.print("Error code to announce new ip: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  return payload;
}


IPAddress localIp() {
  IPAddress ip ;

  HTTPClient http;
  http.begin("http://" + String(_settings.server_whoami));

  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    ip.fromString(http.getString());
  } else {
    Serial.print("Error code to get local ip: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  return ip;
}


// ---------------  web server stuff ----------------
void handle_notFound() {
  _server.send(404, "text/plain", "Not found");
}

void handle_index() {
  String index = "<!DOCTYPE html> <html lang='en'> <head> <meta charset='UTF-8'> <title>Theos DNS</title> <style> html { font-family: Helvetica, sans-serif; display: inline-block; margin: 0 auto; text-align: center; color: #a9a9a9; } body { font-size: 3rem; display: flex; flex-direction: column; align-items: center; background-color: #222222; overflow-x: hidden; } h1 { color: #a9a9a9; margin: 50px auto 30px; } h3 { color: #a9a9a9; margin-bottom: 50px; } p { font-size: 14px; color: #888; margin-bottom: 10px; } .button { display: block; width: 200px; background-color: #1abc9c; border: none; color: white; padding: 13px 30px; text-decoration: none; font-size: 2rem; margin: 0 auto 35px; cursor: pointer; border-radius: 4px; } input[type=text] { border: 2px solid #aaa; border-radius: 4px; margin: 8px 0; outline: none; padding: 8px; box-sizing: border-box; transition: .3s; background: #222222; color: #a9a9a9; font-size: 2.5rem; max-width: 500px; width: 500px; } input[type=text]:focus { border-color: #1abc9c; box-shadow: 0 0 8px 0 #1abc9c; } .content-center-row { display: flex; flex-direction: row; align-content: center; justify-content: center; } label { white-space: nowrap; text-align: center; display: flex; align-items: center; width: 100%; font-size: 2rem; } .space-x-4 > * { margin-right: 1rem; } a{ color: #1abc9c; } </style> </head> <body> <h1>Theos DNS</h1> <h3> <span>Support: </span> <a target='_blank' href='https://t.me/theos_dns'>@theos_dns</a> </h3> <div style='width: 90%;margin-bottom: 100px'> <div class='content-center-row space-x-4' style='margin-bottom: 10px'> <label for='share_link'>Share Link:</label> <input id='share_link' type='text'> </div> <button class='button' onclick='onParseLink()'>parse link</button> <div class='content-center-row space-x-4' style='margin-bottom: 10px'> <label for='auth_server'>Auth Server:</label> <input id='auth_server' type='text' placeholder='111.111.111.111:82' required> </div> <div class='content-center-row space-x-4' style='margin-bottom: 10px'> <label for='whoami_server'>Whoami Server:</label> <input id='whoami_server' type='text' placeholder='111.111.111.111:81' required> </div> <div class='content-center-row space-x-4' style='margin-bottom: 10px'> <label for='coap_server'>COAP Server:</label> <input id='coap_server' type='text' placeholder='111.111.111.111:85' required> </div> <div class='content-center-row space-x-4' style='margin-bottom: 10px'> <label for='token'>Token:</label> <input id='token' type='text' placeholder='xxxxxxxxxxxxxx' required> </div> <div class='content-center-row space-x-4' style='margin-bottom: 10px'> <label for='wifi_ssid'>Wifi ssid:</label> <input id='wifi_ssid' type='text' placeholder='my wifi' required> </div> <div class='content-center-row space-x-4' style=''> <label for='wifi_password'>Wifi Password:</label> <input id='wifi_password' type='text' placeholder='p@ssw0rd' required> </div> </div> <button class='button' onclick='onSubmit()'>Submit</button> <script> function onParseLink(){ [ document.getElementById('auth_server').value, document.getElementById('whoami_server').value, document.getElementById('coap_server').value, document.getElementById('token').value, document.getElementById('wifi_ssid').value, document.getElementById('wifi_password').value ] = document.getElementById('share_link').value.split('@#@') } function onSubmit() { let formData = new FormData(); formData.append('serverAuth', document.getElementById('auth_server').value); formData.append('serverWhoami', document.getElementById('whoami_server').value); formData.append('serverCoap', document.getElementById('coap_server').value); formData.append('token', document.getElementById('token').value); formData.append('ssid', document.getElementById('wifi_ssid').value); formData.append('password', document.getElementById('wifi_password').value); fetch('/saveSettings', { method: 'POST', body: formData }).then((res)=>{ res.text().then(result=>{ alert(result); if(result === 'ok'){ document.body.innerHTML = 'Ok'; } }) }); } </script> </body> </html>";
  _server.send(200, "text/html", index);
}

void handle_saveSettings() {
  if (
    !_server.hasArg("token") ||
    !_server.hasArg("serverAuth") ||
    !_server.hasArg("serverWhoami") ||
    !_server.hasArg("serverCoap") ||
    !_server.hasArg("ssid") ||
    !_server.hasArg("ssid") ||
    _server.arg("token") == NULL ||
    _server.arg("serverAuth") == NULL ||
    _server.arg("serverWhoami") == NULL ||
    _server.arg("serverCoap") == NULL ||
    _server.arg("ssid") == NULL ||
    _server.arg("password") == NULL ||
    sizeof(_server.arg("token")) >= 32 ||
    sizeof(_server.arg("serverAuth")) >= 32 ||
    sizeof(_server.arg("serverWhoami")) >= 32 ||
    sizeof(_server.arg("serverCoap")) >= 32 ||
    sizeof(_server.arg("ssid")) >= 32 || 
    sizeof(_server.arg("password")) >= 32) {
    _server.send(400, "text/plain", "400: Invalid Request");
    return;
  }
  strcpy(_settings.server_auth, _server.arg("serverAuth").c_str());
  strcpy(_settings.server_whoami, _server.arg("serverWhoami").c_str());
  strcpy(_settings.server_coap, _server.arg("serverCoap").c_str());
  strcpy(_settings.token, _server.arg("token").c_str());
  strcpy(_settings.WIFI_ssid, _server.arg("ssid").c_str());
  strcpy(_settings.WIFI_password, _server.arg("password").c_str());

  strncpy(_settings.are_data_ok, "ok", sizeof("ok"));

  unsigned int addr = 0;
  EEPROM.put(addr, _settings);
  EEPROM.commit();
  Serial.println("settings has been saved");

  _server.send(200, "text/plain", "ok");
  delay(1000);

  ESP.restart();
}
// --------------------------------------------------------


bool has_wifi_or_connect() {
  if (!(_settings.are_data_ok[0] == 'o' && _settings.are_data_ok[1] == 'k')) {
    return false;
  }
  if (IS_CONNECTED_TO_WIFI) {
    return true;
  }

  WiFi.begin(_settings.WIFI_ssid, _settings.WIFI_password);
  Serial.print("Connecting to ");
  Serial.print(_settings.WIFI_ssid);
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 15) {
    delay(1000);
    Serial.print(++i);
    Serial.print(' ');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println('\n');
    Serial.println("Connection established!");
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP());
    IS_CONNECTED_TO_WIFI = true;
    show_dns_server_address(WiFi.localIP().toString());
    return true;
  } else {
    Serial.println('\n');
    Serial.println("Couldn't establish connection!");

    strncpy(_settings.are_data_ok, "no", sizeof("no"));
  }

  return false;
}

// --------------------- LCD --------------------
void show_dns_server_address(String ipAddress) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Set below address as your DNS server:");
  display.setCursor(0, 25);
  display.print("==> ");
  display.print(ipAddress);
  display.println(" <==");
  display.display(); 
}

void show_splash_screen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.println("Theos DNS");
  display.display();
}


void show_wifi_details() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SSID: ");
  display.println(AP_ssid);
  display.setCursor(0, 12);
  display.print("PASS: ");
  display.println(AP_password);
  display.setCursor(0, 24);
  display.println("-> http://10.90.90.10");
  display.display();
}

void show_token_is_invalid() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Token is invalid!");
  display.println("more details in:");
  display.println("");
  display.println("     @theos_dns");
  display.display(); 
}


