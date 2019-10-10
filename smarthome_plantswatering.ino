// RTC clock
#include <ThreeWire.h>  
#include <RtcDS1302.h>
// DHT11 sensor for temperature and humidity
#include <DHTesp.h>
#include <Ticker.h>
// LCD display
#include <LiquidCrystal_I2C.h>
// WiFi modules
#include <WiFi.h>
#include <WebServer.h>

// RTC clock instance
const uint8_t RTC_DATA_PIN=4;
const uint8_t RTC_CLK_PIN=5;
const uint8_t RTC_RESET_PIN=2;
ThreeWire myWire(RTC_DATA_PIN,RTC_CLK_PIN,RTC_RESET_PIN);
RtcDS1302<ThreeWire> Rtc(myWire);
unsigned long lastRTCTime=0;
const int RTC_CHECK_TIME=60000;
String hour;
String minute;

// LCD display instance
const uint8_t LCD_COLS = 16;
const uint8_t LCD_ROWS = 2;
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS); 
// LCD print data
boolean print=true;
unsigned long lastPrintTime=0;
const int PRINT_TIME=2000;

const uint8_t DHT_DATA_PIN=18;
// DHT11 sensor instance
DHTesp dht;
ComfortState cf;

// DHT data
String temperature;
String humidity;
String hIndex;
String dPoint;
String cfStatus;

unsigned long lastDHTTime=0;
const int DHT_CHECK_TIME=60000;

const uint8_t OUT_FLOWMETER_PIN=15;
boolean outPressed=false;
unsigned long outLastTime=0;
const uint8_t OUT_BUTTON_PIN=34;
const uint8_t OUT_BUTTON_STATUS_PIN=32;

const uint8_t WATER_MIN_VALUE=0;
const uint8_t WATER_MAX_VALUE=60;
const uint8_t WATERING_DOSE=20;
uint8_t flowPulses=WATER_MAX_VALUE;

const uint8_t SOLENDOIDVALVE_PIN=14;
const uint8_t PUMP_PIN=12;

const uint8_t IN_BUTTON_PIN=35;
boolean inPressed=false;
unsigned long inLastTime=0;
const uint8_t IN_BUTTON_STATUS_PIN=33;
const uint8_t IN_FLOWMETER_PIN=25;

/* Put your SSID & Password */
const char* ssid = "PVT";  // Enter SSID here
const char* password = "12345678";  //Enter Password here

/* Put IP Address details */
IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

WebServer server(80);

const boolean useSerial=true;


void initLCD() {
  lcd.init();
  lcd.backlight();
}

char ds[20]="";
void loadDT(const RtcDateTime& dt) {
  if (!dt.IsValid()) {
    bzero(ds,20);
    strcat(ds, "RTC Error");
  } else {
    snprintf_P(ds, 
            (sizeof(ds) / sizeof(ds[0])),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    hour=String(dt.Hour());
    minute=String(dt.Minute());
  }
}

void initRTC() {
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    loadDT(compiled);
    if (!Rtc.IsDateTimeValid()) {
        // Common Causes:
        //    1) first time you ran and the device wasn't running yet
        //    2) the battery on the device is low or even missing

        if (useSerial) {
          Serial.println("RTC lost confidence in the DateTime!");
        }
        Rtc.SetDateTime(compiled);
    }

    if (Rtc.GetIsWriteProtected()) {
        if (useSerial) {
          Serial.println("RTC was write protected, enabling writing now");
        }
        Rtc.SetIsWriteProtected(false);
    }

    if (!Rtc.GetIsRunning()) {
        if (useSerial) {
          Serial.println("RTC was not actively running, starting now");
        }
        Rtc.SetIsRunning(true);
    }

    RtcDateTime now = Rtc.GetDateTime();
    if (now < compiled) {
        if (useSerial) {
          Serial.println("RTC is older than compile time!  (Updating DateTime)");
        }
        Rtc.SetDateTime(compiled);
    } else if (now > compiled) {
        if (useSerial) {
          Serial.println("RTC is newer than compile time. (this is expected)");
        }
    } else if (now == compiled) {
        if (useSerial) {
          Serial.println("RTC is the same as compile time! (not expected but all is fine)");
        }
    }
}

void initDHT() {
  pinMode(DHT_DATA_PIN, INPUT);
  dht.setup(DHT_DATA_PIN, DHTesp::DHT11);
}



void loadDHTData() {
  // Reading temperature for humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
  TempAndHumidity newValues = dht.getTempAndHumidity();
  // Check if any reads failed and exit early (to try again).
  if (dht.getStatus() != 0) {
    Serial.println("DHT11 error status: " + String(dht.getStatusString()));
    return;
  }

  float heatIndex = dht.computeHeatIndex(newValues.temperature, newValues.humidity);
  float dewPoint = dht.computeDewPoint(newValues.temperature, newValues.humidity);
  float cr = dht.getComfortRatio(cf, newValues.temperature, newValues.humidity);

  String comfortStatus;
  switch(cf) {
    case Comfort_OK:
      comfortStatus = "Comfort_OK";
      break;
    case Comfort_TooHot:
      comfortStatus = "Comfort_TooHot";
      break;
    case Comfort_TooCold:
      comfortStatus = "Comfort_TooCold";
      break;
    case Comfort_TooDry:
      comfortStatus = "Comfort_TooDry";
      break;
    case Comfort_TooHumid:
      comfortStatus = "Comfort_TooHumid";
      break;
    case Comfort_HotAndHumid:
      comfortStatus = "Comfort_HotAndHumid";
      break;
    case Comfort_HotAndDry:
      comfortStatus = "Comfort_HotAndDry";
      break;
    case Comfort_ColdAndHumid:
      comfortStatus = "Comfort_ColdAndHumid";
      break;
    case Comfort_ColdAndDry:
      comfortStatus = "Comfort_ColdAndDry";
      break;
    default:
      comfortStatus = "Unknown:";
      break;
  };

  temperature=String((int)newValues.temperature);
  humidity=String((int)newValues.humidity);
  hIndex=String(heatIndex);
  dPoint=String(dewPoint);
  cfStatus=comfortStatus;
  Serial.println(" T:" + temperature + " H:" + humidity + " I:" + hIndex + " D:" + dPoint + " " + cfStatus);
}

void IRAM_ATTR_outFlowmeterISR() {
  if (outPressed && flowPulses > 0) {
    flowPulses--;
    if (flowPulses % WATERING_DOSE == 0) {
      digitalWrite(SOLENDOIDVALVE_PIN, LOW);
      if (flowPulses == 0) {
        inLastTime = millis();
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
    print=true;
  }
}

void initOutFlowMeter() {
  pinMode(OUT_FLOWMETER_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(OUT_FLOWMETER_PIN), IRAM_ATTR_outFlowmeterISR, RISING);
}

void IRAM_ATTR_outButtonISR() {
  if (flowPulses > 0 && millis() - outLastTime > 50) {
      outLastTime = millis();
      outPressed = !outPressed;
      if (outPressed) {
        digitalWrite(SOLENDOIDVALVE_PIN, HIGH);
        digitalWrite(OUT_BUTTON_STATUS_PIN, HIGH);
      } else {
        digitalWrite(SOLENDOIDVALVE_PIN, LOW);
        digitalWrite(OUT_BUTTON_STATUS_PIN, LOW);
      }
      print=true;
  }  
}
void initOutButton() {
  pinMode(OUT_BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(OUT_BUTTON_PIN), IRAM_ATTR_outButtonISR, RISING);
}

void IRAM_ATTR_inFlowmeterISR() {
  if (inPressed && flowPulses < WATER_MAX_VALUE) {
    flowPulses++;
    if (flowPulses == WATER_MAX_VALUE) {
      digitalWrite(PUMP_PIN, LOW);
      digitalWrite(IN_BUTTON_STATUS_PIN, LOW);
    }
    print=true;
  }
}

void initInFlowMeter() {
  pinMode(IN_FLOWMETER_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(IN_FLOWMETER_PIN), IRAM_ATTR_inFlowmeterISR, RISING);
}

void IRAM_ATTR_inButtonISR() {
  if (flowPulses == 0 && millis() - inLastTime > 50) {
      inLastTime = millis();
      inPressed = !inPressed;
      if (inPressed) {
        digitalWrite(PUMP_PIN, HIGH);
        digitalWrite(IN_BUTTON_STATUS_PIN, HIGH);
        digitalWrite(LED_BUILTIN, LOW);
      } else {
        digitalWrite(PUMP_PIN, LOW);
        digitalWrite(IN_BUTTON_STATUS_PIN, LOW);
      }
  }  
}
void initInButton() {
  pinMode(IN_BUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(IN_BUTTON_PIN), IRAM_ATTR_inButtonISR, RISING);
}

void printLCD(unsigned long t) {
  if (print && (t - lastPrintTime) > PRINT_TIME) {
    int waterings = (WATER_MAX_VALUE-flowPulses)/WATERING_DOSE;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(hour);
    lcd.print(":");
    lcd.print(minute);
    lcd.print(" ");
    lcd.print("T:");
    lcd.print(temperature);
    lcd.print(" H:");
    lcd.print(humidity);
    lcd.setCursor(0, 1);
    lcd.print("W(");
    lcd.print(WATERING_DOSE);
    lcd.print(")=");
    lcd.print(waterings);
    lcd.print(" Tank=");    
    lcd.print(flowPulses);
//    lcd.print("|");
//    lcd.print(hIndex);
//    lcd.print("|");
//    lcd.print(dPoint);
    print=false;
    lastPrintTime = t;
  }
}

void initIO() {
  pinMode(OUT_BUTTON_STATUS_PIN, OUTPUT);
  pinMode(SOLENDOIDVALVE_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(IN_BUTTON_STATUS_PIN, OUTPUT);
}

void initAP() {
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  delay(100);
  
  server.on("/", handle_OnConnect);
  server.on("/wateron", handle_wateron);
  server.on("/wateroff", handle_wateroff);
  //server.on("/pumpon", handle_pumpon);
  //server.on("/pumpoff", handle_pumpoff);
  server.onNotFound(handle_NotFound);
  
  server.begin();
}

void handle_OnConnect() {
  //digitalWrite(CONNECTION_STATUS_PIN, HIGH);
  server.send(200, "text/html", SendHTML());
  //digitalWrite(CONNECTION_STATUS_PIN, LOW);
}
void handle_wateron() {
  //digitalWrite(CONNECTION_STATUS_PIN, HIGH);
  //IRAM_ATTR_buttonISR();
  server.send(200, "text/html", SendHTML());
  //digitalWrite(CONNECTION_STATUS_PIN, LOW);
}
void handle_wateroff() {
  //digitalWrite(CONNECTION_STATUS_PIN, HIGH);
  //IRAM_ATTR_buttonISR();
  server.send(200, "text/html", SendHTML());
  //digitalWrite(CONNECTION_STATUS_PIN, LOW);
}
void handle_NotFound(){
  //digitalWrite(CONNECTION_STATUS_PIN, HIGH);
  server.send(404, "text/plain", "Not found");
  //digitalWrite(CONNECTION_STATUS_PIN, LOW);
}
String SendHTML(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>Watering System Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #3498db;}\n";
  ptr +=".button-on:active {background-color: #2980b9;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";

//  if (ltrue) {
//    uint8_t percentValue = flowPulses * 100 / ONE_LITER_PULSES;
//    ptr +="<h1>Total waterings <b>" + String(wateringCount) + "</b></h1>\n";
//    ptr +="<h3>Current watering status <b>" + String(percentValue) + "%</b></h3>\n";
//  } else {
//    ptr +="<h1>Filling the tank.</h1>\n";
//  }
  
   if(true) {
      ptr +="<p>Watering: ON</p><a class=\"button button-off\" href=\"/wateroff\">OFF</a>\n";
   } else {
      ptr +="<p>Watering: OFF</p><a class=\"button button-on\" href=\"/wateron\">ON</a>\n";
   }

  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

void setup() {
  if (useSerial) {
    Serial.begin(9600);
  }
  initIO();
  initLCD();
  initRTC();
  initDHT();
  initOutFlowMeter();
  initOutButton();
  initInFlowMeter();
  initInButton();
//  initAP();  
}

void loop() {

  unsigned long t=millis();
  if(t-lastRTCTime>RTC_CHECK_TIME || lastRTCTime == 0) {
    loadDT(Rtc.GetDateTime());
    lastRTCTime=t;
    print=true;
  }
  if(t-lastDHTTime>DHT_CHECK_TIME || lastDHTTime == 0) {
    if (lastDHTTime == 0) {
      delay(2000);//just for the first time
    }
    loadDHTData();
    lastDHTTime=t;
    print=true;
  }
  
  printLCD(t);
  delay(1000);
}
