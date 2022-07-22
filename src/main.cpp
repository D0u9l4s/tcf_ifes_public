/*
LISTA DE PORTAS:
1-PINO_CORRENTE         A0
2-PINO_CHUVA            D0
3-PINO_SCL_UV           D1
4-PINO_SDA_UV           D2
5-PINO_TEMPERATURA_AGUA D3
6-PINO_FLUXO            D4
7-PINO_CORRENTE_PWM     D5
8-PINO_DHT11            D6
*/
/*------------------------BIBLIOTECAS------------------------*/
// Bibliotecas - Web Server
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

// Bibliotecas - Envio JSON DADOS
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// Bibliotecas - Sensores Temperaturas - DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>

// Biblioteca - Sensor Umidade DHT11
#include "DHT.h"

// Bibliotecas - Sensor Solar/sensorUV - VEML6070
#include <Wire.h>
#include "Adafruit_VEML6070.h"

// Biblioteca - Telegram
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
/*------------------------FIM BIBLIOTECAS--------------------*/

/*------------------------PINAGEM E VARIÁVEIS----------------*/
// Sensor corrente - max471
#define PINO_CORRENTE A0
int RawValue = 0;
float Current = 0;
float Potencia = 0;

// Sensor chuva
#define PINO_CHUVA D0
int rainDigitalVal;

// Sensores de Temperatura
#define PINO_TEMPERATURA_AGUA D3
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(PINO_TEMPERATURA_AGUA);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
float tempSensor1, tempSensor2, tempSensor3, tempSensor4, tempSensor5;
uint8_t sensor1[8] = {0x28, 0x68, 0x36, 0x00, 0x00, 0x00, 0x00, 0x64}; // TempAguaOrigem
uint8_t sensor2[8] = {0x28, 0xB4, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x9F}; // TempAguaQuente
uint8_t sensor3[8] = {0x28, 0x01, 0x2E, 0x95, 0xF0, 0x01, 0x3C, 0x47}; // TempAguaNivelSuperior
uint8_t sensor4[8] = {0x28, 0xDD, 0x10, 0x00, 0x00, 0x00, 0x00, 0xEC}; // TempAguaNivelMediano
uint8_t sensor5[8] = {0x28, 0x07, 0xEC, 0x95, 0xF0, 0xFF, 0x3C, 0x11}; // TempAguaNivelInferior
// Quantidade de sensores de temperatura DS18B20 encontrados
int numberOfDevices;
// Armazenará o endereço do sensor de temperatura DS18B20
DeviceAddress tempDeviceAddress;

// Sensor fluxo
#define PINO_FLUXO D4
long currentMillis = 0;
long previousMillis = 0;
int interval = 1000;
boolean ledState = HIGH;
float calibrationFactor = 0.1; // 0.1 //4.5 default
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned long flowMilliLitres;
unsigned int totalMilliLitres;
float flowLitres;
float totalLitres;

// Controle PWM / step bomba
#define PINO_CORRENTE_PWM D5
unsigned long lastTimeCheckBomba = 0;
unsigned long intervaloCheckBomba = 20000;

unsigned long lastTimeGetStep = 0;
unsigned long intervaloGetStep = 200000;

unsigned long stepBombaAtual = 0;
unsigned long stepCalculado = 0;

/* Definições do servidor web */
const char *ssid = "NomeWifi";
const char *password = "SenhaWifi";
ESP8266WebServer server(80);
// Envio de dados JSON API
const char *serverName = "http://host/tcf_rec_dados.php";
// Recebimento de dados Step Bomba
const char *urlStep = "http://host/step.php";
const char *urlIntervaloPostData = "http://host/intervalo.php";

unsigned long lastTimePostSensorData = 0;
unsigned long intervaloPostSensorData = 300000;

unsigned long lastTimeGetIntervaloPostData = 0;
unsigned long intervaloGetIntervaloPostData = 360000;

/* Definições sensor umidade DHT 11 */
#define DHTTYPE DHT11 // DHT 22  (AM2302), AM2321
// DHT Sensor
#define PINO_DHT11 D6
// Initialize DHT sensor.
DHT dht(PINO_DHT11, DHTTYPE);
float Temperature;
float Humidity;

// Sensor sensorUV
Adafruit_VEML6070 sensorUV = Adafruit_VEML6070();

// Wifi
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

void handle_Sensores();
void handle_pwm();
void handle_reboot();
void handle_grafico();
void handle_NotFound();
void handle_html_pwm();

void restServerRouting();
void sendDataURL();
void getSettings();
void getStepBomba();
void verificaBomba();
void getIntervaloPostData();
void sendLog();
String mensagemLog = "";
boolean permissaoLigarBomba();

/*--------------------FIM PINAGEM E VARIÁVEIS----------------*/

// Sensor Fluxo
void IRAM_ATTR pulseCounter()
{
  pulseCount++;
}

void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao wi-fi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  Serial.println("Reconectando ao wi-fi...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
}

void setup()
{
  // start serial port
  Serial.begin(115200);
  delay(50);

  /* Wifi */
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  initWiFi();

  /* Servidor web */
  restServerRouting(); // Rotas
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP Iniciado");

  /* Sensores de temperatura */
  sensors.begin();

  /* Controle tensão PWM - Bomba d'água */
  pinMode(PINO_CORRENTE_PWM, OUTPUT);
  analogWrite(PINO_CORRENTE_PWM, 0); // Inicia desligado

  /* Sensor Corrente - Bomba d'água */
  pinMode(PINO_CORRENTE, INPUT);

  /* Sensor DHT11 */
  pinMode(PINO_DHT11, INPUT);
  dht.begin();

  /* Sensor Chuva */
  pinMode(PINO_CHUVA, INPUT);

  /* Sensor sensorUV*/
  sensorUV.begin(VEML6070_1_T);

  /* Sensor Fluxo */
  pinMode(PINO_FLUXO, INPUT_PULLUP);
  pulseCount = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  previousMillis = 0;
  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), pulseCounter, RISING);

  /* Informar ligamento/reinicio do ESP */
  mensagemLog = "ESP ligado.";
  sendLog();

  getIntervaloPostData();
  delay(50);

  getStepBomba();
  delay(50);

  verificaBomba();
  delay(50);
}

void loop()
{
  /* Servidor web */
  server.handleClient();

  // Obtém step pra trabalho da bomba
  if ((millis() - lastTimeGetStep) > intervaloGetStep)
  {
    // Dados post api
    getStepBomba();
    lastTimeGetStep = millis();
  }

  // intervaloGetIntervaloPostData
  if ((millis() - lastTimeGetIntervaloPostData) > intervaloGetIntervaloPostData)
  {
    // Dados post api
    getIntervaloPostData();
    lastTimeGetIntervaloPostData = millis();
  }

  // Send an HTTP POST request
  if ((millis() - lastTimePostSensorData) > intervaloPostSensorData)
  {
    // Dados post api
    sendDataURL();
    lastTimePostSensorData = millis();
  }

  // Verifica temperaturas e atua no power da bomba dágua
  if ((millis() - lastTimeCheckBomba) > intervaloCheckBomba)
  {
    verificaBomba();
    lastTimeCheckBomba = millis();
  }
}

/* HTML/JSON/DADOS SENSORES Página web */
//String SendHTMLTemp(float tempSensor1, float tempSensor2, float tempSensor3, float tempSensor4, float tempSensor5, float Temperaturestat, float Humiditystat, int Chuva, float flowRate, int totalMilliLitres, int totalLitres, float Potencia, int indiceUV, int stepBombaAtual, int intervaloPostSensorData, int stepCalculado)
String SendHTMLTemp(String jsonSensores)
{
  String ptr = "<!DOCTYPE html> <html>";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">";
  ptr += "<meta http-equiv=refresh content=30>";
  ptr += "<title>TCF-DOUGLAS | YAN</title>";
  ptr += "<style>html {font-size: large; font-family: Calibri,Arial; display: inline-block; margin: 0px auto; text-align: center;}";
  ptr += "body{margin-top: 5px;}";
  ptr += "p {color: #444444;margin-bottom: 2px;}";
  ptr += ".valor {color: blue;margin-bottom: 2px;font-weight:bold}";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "";
  ptr += "<div id=\"webpage\">\n";
  ptr += "<script>var timeleft = 10;";
  ptr += "var downloadTimer = setInterval(function(){";
  ptr += "if(timeleft <= 0){ clearInterval(downloadTimer)}";
  ptr += "document.getElementById('progress').innerText = 'Atualizando em ' + (30 - timeleft);";
  ptr += "timeleft -= 1;}, 1000);</script>";
  ptr += "<span id=progress></span>";
  ptr += "<p>Sensores:</p>";
  ptr += "<pre id='json'></pre>";
  ptr += "<script>document.getElementById('json').textContent = JSON.stringify("+(String)jsonSensores+", undefined, 2)</script>";
  //ptr += jsonSensores;
  ptr += "<p>";
  ptr += "Step calculado: (50-100) <span class=valor>";
  ptr += (int)stepCalculado;
  ptr += "</span></p>";
  ptr += "<p>intervaloPostSensorData: <span class=valor>";
  ptr += (int)intervaloPostSensorData;
  ptr += "</span></p>";
  ptr += "</div>\n";
  ptr += "";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
};

String potenciaBomba()
{
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>TCF-DOUGLAS | YAN</title>\n";
  ptr += "<style>html {font-size: x-large; font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 10px;} h3 {color: #444444;margin: 10px auto 10px;}\n";
  ptr += "p {color: #444444;margin-bottom: 5px;}\n";
  ptr += ".slidecontainer { width: 80%; margin: auto;}";
  ptr += ".slider {  -webkit-appearance: none;";
  ptr += "  width: 100%;";
  ptr += "  height: 15px;";
  ptr += "  border-radius: 5px;";
  ptr += "  background: #d3d3d3;";
  ptr += "  outline: none;";
  ptr += "  opacity: 0.7;";
  ptr += "  -webkit-transition: .0s;";
  ptr += "  transition: opacity .2s;";
  ptr += "}";
  ptr += ".slider:hover { opacity: 1;}";
  ptr += ".slider::-webkit-slider-thumb {";
  ptr += "  -webkit-appearance: none;";
  ptr += "  appearance: none;";
  ptr += "  width: 25px;";
  ptr += "  height: 25px;";
  ptr += "  border-radius: 50%;";
  ptr += "  background: #326C88;";
  ptr += "  cursor: pointer; }";
  ptr += ".slider::-moz-range-thumb {";
  ptr += "  width: 25px;";
  ptr += "  height: 25px;";
  ptr += "  border-radius: 50%;";
  ptr += "  background: #326C88;";
  ptr += "  cursor: pointer;}";
  ptr += ".switch {";
  ptr += "  position: relative;";
  ptr += "  display: inline-block;";
  ptr += "  width: 60px;";
  ptr += "  height: 34px; }";
  ptr += ".switch input { ";
  ptr += "  opacity: 0;";
  ptr += "  width: 0;";
  ptr += "  height: 0; }";
  ptr += ".slider1 {";
  ptr += "  position: absolute;";
  ptr += "  cursor: pointer;";
  ptr += "  top: 0;";
  ptr += "  left: 0;";
  ptr += "  right: 0;";
  ptr += "  bottom: 0;";
  ptr += "  background-color: #ccc;";
  ptr += "  -webkit-transition: .4s;";
  ptr += "  transition: .4s;}";
  ptr += ".slider1:before {";
  ptr += "  position: absolute;";
  ptr += "  content: '';";
  ptr += "  height: 26px;";
  ptr += "  width: 26px;";
  ptr += "  left: 4px;";
  ptr += "  bottom: 4px;";
  ptr += "  background-color: white;";
  ptr += "  -webkit-transition: .2s;";
  ptr += "  transition: .2s;}";
  ptr += "input:checked + .slider1 {";
  ptr += "  background-color: #326C88;}";
  ptr += "input:focus + .slider1 {";
  ptr += "  box-shadow: 0 0 1px #326C88;}";
  ptr += "input:checked + .slider1:before {";
  ptr += "  -webkit-transform: translateX(26px);";
  ptr += "  -ms-transform: translateX(26px);";
  ptr += "  transform: translateX(26px);}";
  ptr += ".slider1.round {";
  ptr += "  border-radius: 34px;}";
  ptr += ".slider1.round:before {";
  ptr += "  border-radius: 50%;}";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "";
  ptr += "<div class='flex slidecontainer'>";
  ptr += "  <h4> Potencia bomba: <span id='slider_p'></span></h4>";
  ptr += "  <input type='range' min='0' max='250' value='0' class='slider' id='myRange' oninput='this.nextElementSibling.value = this.value'>";
  ptr += "<output>0</output>";
  ptr += "<!--p>Outro: <span id='led_state'>OFF</span></p> ";
  ptr += "<label class='switch'>";
  ptr += "  <input type='checkbox' onchange='state_change(this)'>";
  ptr += "  <span class='slider1 round'></span>";
  ptr += "</label -->";
  ptr += "</div>";
  ptr += "<script src='https://canvasjs.com/assets/script/canvasjs.min.js'></script>";
  ptr += "<script>";
  ptr += "var slider = document.getElementById('myRange');";
  ptr += "var output = document.getElementById('slider_p');";
  ptr += "output.innerHTML = slider.value;";
  ptr += "slider.onchange = function() {";
  ptr += "  output.innerHTML = this.value;";
  ptr += "  pwm_change(this.value);}";
  ptr += "</script>";
  ptr += "<script>";
  ptr += "function pwm_change(val) {";
  ptr += "  var xhttp = new XMLHttpRequest();";
  ptr += "  xhttp.open('GET', 'setPWM?PWMval='+val, true);";
  ptr += "  xhttp.send();}";
  ptr += "</script>";
  ptr += "<script>";
  ptr += "function state_change(element) {";
  ptr += "  var xhttp = new XMLHttpRequest(); ";
  ptr += "  if (element.checked){";
  ptr += "    xhttp.open('GET', 'setButton?button_state=1', true);";
  ptr += "    document.getElementById('led_state').innerHTML = 'ON';";
  ptr += "  } else if (!element.checked){";
  ptr += "    xhttp.open('GET', 'setButton?button_state=0', true);";
  ptr += "    document.getElementById('led_state').innerHTML = 'OFF';";
  ptr += "  }";
  ptr += "  xhttp.send();";
  ptr += "}";
  ptr += "</script>";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

// Servidor web - Temperatura
String dadosSensores()
{
  sensors.requestTemperatures();
  tempSensor1 = sensors.getTempC(sensor1);
  tempSensor2 = sensors.getTempC(sensor2);
  tempSensor3 = sensors.getTempC(sensor3);
  tempSensor4 = sensors.getTempC(sensor4);
  tempSensor5 = sensors.getTempC(sensor5);

  // DHT11
  Temperature = dht.readTemperature();
  Humidity = dht.readHumidity();

  // Sensor chuva
  int Chuva;
  Chuva = 1;
  int rainDigitalVal = digitalRead(PINO_CHUVA);
  if (rainDigitalVal)
  {
    Chuva = 0;
  }
  // Sensor fluxo
  currentMillis = millis();
  if (currentMillis - previousMillis > interval)
  {
    pulse1Sec = pulseCount;
    pulseCount = 0;

    /*Vazao L/min float(flowRate) */
    flowRate = ((1000.0 / (millis() - previousMillis)) * pulse1Sec) / calibrationFactor;
    previousMillis = millis();

    flowMilliLitres = (flowRate / 60) * 1000;
    flowLitres = (flowRate / 60);

    /* Acumulado:  mL / L*/
    totalMilliLitres += flowMilliLitres;
    totalLitres += flowLitres;
  }

  // Sensor corrente
  RawValue = analogRead(PINO_CORRENTE);
  Current = (RawValue * 3.3) / 1023.0; // scale the ADC
  if (Current < 1)
    Current = 0;
  Potencia = Current * 12;

  // Sensor sensorUV
  int indiceUV = sensorUV.readUV();
  Serial.println("UV: " + indiceUV);

  if (sensorUV.readUV() >= 0 && sensorUV.readUV() < 689)
  {
    //Serial.println(" Baixo");
  }
  else if (sensorUV.readUV() >= 690 && sensorUV.readUV() < 1379)
  {
    //Serial.println(" Moderado");
  }
  else if (sensorUV.readUV() >= 1380 && sensorUV.readUV() < 1839)
  {
    //Serial.println(" Alto");
  }
  else if (sensorUV.readUV() >= 1839 && sensorUV.readUV() < 2529)
  {
    //Serial.println(" Muito Alto");
  }
  else if (sensorUV.readUV() >= 2530 && sensorUV.readUV() < 2760)
  {
    //Serial.println(" Extremo");
  }
  else
  {
    Serial.println("Erro lendo sensorUV.");
  }

  DynamicJsonDocument doc(512);
  doc["TempAguaOrigem"] = tempSensor1;
  doc["TempAguaQuente"] = tempSensor2;
  doc["TempAguaNivelSuperior"] = tempSensor3;
  doc["TempAguaNivelMediano"] = tempSensor4;
  doc["TempAguaNivelInferior"] = tempSensor5;
  doc["TempAmbiente"] = Temperature;
  doc["UmidadeAmbiente"] = Humidity;
  doc["Chuva"] = Chuva;
  doc["VazaoAtual"] = flowRate;
  doc["VazaoAcumuladaML"] = totalMilliLitres;
  doc["VazaoAcumuladaL"] = totalLitres;
  doc["PotenciaBomba"] = Potencia;
  doc["IndiceUV"] = indiceUV;
  doc["Step"] = stepBombaAtual;

  String buf;
  serializeJson(doc, buf);
  return buf;
}

// Reboot ESP
void handle_reboot()
{
  String pass = server.arg("pass"); // reading from slider on html pagae
  if (pass == "Senh@")
  {
    server.send(200, "text/html", "<h1>Reiniciando.</h1>");
    mensagemLog = "ESP reboot via browser.";
    sendLog();
    ESP.restart();
  }
  else
    server.send(200, "text/html", "<h1>Senha invalida.</h1>");
}

// Exibe JSON dos dados dos sensores ou Settings do NodeMCU
void restServerRouting()
{
  server.on("/", handle_Sensores);
  server.on("/settings", getSettings);
  server.on("/setPWM", handle_pwm);
  server.on("/reboot", handle_reboot);
  server.on("/bomba", handle_html_pwm);
}

// Controle pwm
void handle_pwm()
{
  String pwm_val = server.arg("PWMval");
  analogWrite(PINO_CORRENTE_PWM, pwm_val.toInt()); // chaning the value on the NodeMCU board
  mensagemLog = "Step bomba definido via browser.";
  sendLog();
}

void handle_html_pwm()
{
  server.send(200, "text/html", potenciaBomba()); // handling the webpage update
}

void handle_Sensores()
{
  server.send(200, "text/html", SendHTMLTemp(dadosSensores()));
}

// POST dados para URL Cloud
void sendDataURL()
{
  // Check WiFi connection status
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClient client;
    HTTPClient http;

    http.begin(client, serverName);
    http.addHeader("Content-Type", "application/json");
    String httpRequestData = dadosSensores();
    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode == 200)
    {
      Serial.println("Dados enviados.");
    }
    else
    {
      mensagemLog = "Erro enviando dados.";
      sendLog();
    }
    http.end();
  }
  else
  {
    Serial.println("WiFi desconectado.");
    mensagemLog = "WiFi desconectado.";
    sendLog();
  }
}

// Obtém step para trabalho com bomba
void getStepBomba()
{
  WiFiClient client2;
  HTTPClient http2;

  http2.begin(client2, urlStep);
  int httpCodeGet2 = http2.POST("");
  Serial.print("response : ");
  Serial.println(httpCodeGet2);
  if (httpCodeGet2 == 200)
  {
    String payloadGet2 = http2.getString();
    int stepRecebido = payloadGet2.toInt();
    Serial.print("Step recebido server : ");
    Serial.println(stepRecebido);

    stepBombaAtual = stepRecebido;
    if (stepRecebido > 0)
    {
      stepCalculado = ((stepRecebido * 50) / 100) + 50;
    }
    else stepCalculado = 0;
    Serial.println("Step calculado: ");
    Serial.println(stepCalculado);

    // Calcular porcentagem vinda da nuvem 0-25-50-75-100 25 % Bomba  (0%)50 (25%)? (50%)75 (75%)? 100(100%)
    if (permissaoLigarBomba())
    {
      analogWrite(PINO_CORRENTE_PWM, stepCalculado);
      Serial.print("Definido step bomba: " + stepCalculado);
    }
    else
    {
      Serial.print("Permissão negada para step bomba: " + stepCalculado);
    }
  }
  else
  {
    stepBombaAtual = 999;
    mensagemLog = "Step bomba = 999.";
    sendLog();
  }

  http2.end();
}

void verificaBomba()
{
  /* Desliga bomba em caso de temperatura acumulada superior à externa
  tempSensor1 = sensors.getTempC(sensor1);TempAguaOrigem
  tempSensor2 = sensors.getTempC(sensor2);TempAguaQuente
  tempSensor3 = sensors.getTempC(sensor3);TempAguaNivelSuperior
  tempSensor4 = sensors.getTempC(sensor4);TempAguaNivelMediano
  tempSensor5 = sensors.getTempC(sensor5);TempAguaNivelInferior
  */
  if (sensors.getTempC(sensor2) < sensors.getTempC(sensor3))
  {
    analogWrite(PINO_CORRENTE_PWM, 0); // Desliga bomba
  }
}

boolean permissaoLigarBomba()
{
  if (sensors.getTempC(sensor2) > sensors.getTempC(sensor3))
  {
    return true;
  }
  else
  {
    return false;
  }
}

void getIntervaloPostData()
{
  WiFiClient client1;
  HTTPClient http1;

  http1.begin(client1, urlIntervaloPostData);
  int httpCodeGet1 = http1.POST("");

  Serial.print("response : ");
  Serial.println(httpCodeGet1);

  if (httpCodeGet1 == 200)
  {
    String payloadGet1 = http1.getString();
    int intervaloRecebido = payloadGet1.toInt();
    Serial.print("intervaloPostSensorData recebido server : ");
    Serial.println(payloadGet1);

    if (intervaloRecebido > 60000)
    {
      intervaloPostSensorData = intervaloRecebido;
    }
    else
    {
      mensagemLog = "IntervaloPostSensorData recebido inválido.";
      sendLog();
    }
  }
  else
  {
    mensagemLog = "Erro requisição do intervaloPostData.";
    sendLog();
  }

  http1.end();
}

// Envia alertas ao telegram
void sendLog()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClient client4;
    HTTPClient http4;
    http4.begin(client4, serverName);
    http4.addHeader("Content-Type", "application/json");
    http4.POST("{\"EspLog\":\"" + mensagemLog + "\"}");
    http4.end();
  }
}

// Servidor web - Rota inexistente
void handle_NotFound()
{
  server.send(404, "text/plain", "Nada por aqui.");
}

// Retorna as configurações de rede do NODEMCU
void getSettings()
{
  DynamicJsonDocument doc(512);

  doc["ip"] = WiFi.localIP().toString();
  doc["gw"] = WiFi.gatewayIP().toString();
  doc["nm"] = WiFi.subnetMask().toString();
  doc["signalStrengh"] = WiFi.RSSI();
  doc["chipId"] = ESP.getChipId();
  doc["CoreVersio"] = ESP.getCoreVersion();
  doc["flashChipId"] = ESP.getFlashChipId();
  doc["flashChipSize"] = ESP.getFlashChipSize();
  doc["flashChipRealSize"] = ESP.getFlashChipRealSize();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["Vcc"] = ESP.getVcc();
  doc["ResetReaso"] = ESP.getResetReason();

  String buf;
  serializeJson(doc, buf);
  server.send(200, F("application/json"), buf);
}
