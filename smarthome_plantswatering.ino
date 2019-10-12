// RTC clock
#include <ThreeWire.h>  
#include <RtcDS1302.h>
// DHT11 sensor for temperature and humidity
#include <DHTesp.h>
// LCD display
#include <LiquidCrystal_I2C.h>
// WiFi modules
#include <WiFi.h>
#include <WebServer.h>

// RTC clock instance
#define RTC_DATA_PIN 4
#define RTC_CLK_PIN 5
#define RTC_RESET_PIN 2

#define RTC_CHECK_TIME 30000
uint8_t month=-1;
uint8_t day=-1;
int year=-1;
uint8_t hour=-1;
uint8_t minute=-1;
uint8_t second=-1;
    
// LCD display instance
#define LCD_COLS 16
#define LCD_ROWS 2
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS); 
// LCD print data
boolean print=true;
#define LCD_PRINT_TIME 2000
#define DHT_DATA_PIN 19

// DHT data
int temperature=-1;
int humidity=-1;
float hIndex=-1;
float dPoint=-1;
String cfStatus="N/A";

#define DHT_CHECK_TIME 60000

#define OUT_FLOWMETER_PIN 15
boolean outPressed=false;

#define OUT_BUTTON_PIN 34
#define OUT_BUTTON_STATUS_PIN 32

const uint8_t WATER_MIN_VALUE=0;
const uint8_t WATER_MAX_VALUE=60;
const uint8_t WATERING_DOSE=20;
portMUX_TYPE muxFlow = portMUX_INITIALIZER_UNLOCKED;
volatile int flowPulses=WATER_MAX_VALUE;

#define SOLENDOIDVALVE_PIN 14
#define PUMP_PIN 12

#define  IN_BUTTON_PIN 35

boolean inPressed=false;
#define IN_BUTTON_STATUS_PIN 33
#define IN_FLOWMETER_PIN 25

/* Put your SSID & Password */
const char* ssid = "PVT";  // Enter SSID here
const char* password = "12345678";  //Enter Password here

/* Put IP Address details */
IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

WebServer server(80);

//const boolean useSerial=false;
const boolean useSerial=true;

// ------------------ RTC -----------------------//
/** Task handle for RTC value read task */
TaskHandle_t rtcTaskHandle = NULL;
ThreeWire myWire(RTC_DATA_PIN,RTC_CLK_PIN,RTC_RESET_PIN);
RtcDS1302<ThreeWire> Rtc(myWire);
RtcDateTime scheduledDT=NULL;

void loadRTC(const RtcDateTime& dt) {
  if (!dt.IsValid()) {
    month=-1;
    day=-1;
    year=-1;
    hour=-1;
    minute=-1;
    second=-1;
  } else {
    month=dt.Month();
    day=dt.Day();
    year=dt.Year();
    hour=dt.Hour();
    minute=dt.Minute();
    second=dt.Second();
  }
  if (useSerial) {
    Serial.println(String(month) + ":" + String(day) + ":" + String(year) + ":" + String(hour) + ":" + String(minute));
    Serial.println("RTC loaded");
  }
  print=true;
  if (scheduledDT != NULL && scheduledDT.TotalSeconds64() < dt.TotalSeconds64()) {
    outButtonImpl();
    scheduledDT=NULL;
  }
}

/**
 * Task to reads RTC data
 * @param pvParameters
 *    pointer to task parameters
 */
void rtcTask(void *pvParameters) {
  if (useSerial) {
    String taskMessage = "RTC(Time) retrieval task running on core ";
    taskMessage = taskMessage + xPortGetCoreID();
    Serial.println(taskMessage);
  }
  while (1) {
    loadRTC(Rtc.GetDateTime());
    vTaskDelay(RTC_CHECK_TIME);
  }
}

void initRTC() {
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    loadRTC(compiled);
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

    // Start task to get RTC data
    xTaskCreatePinnedToCore(
        rtcTask,                       /* Function to implement the task */
        "rtcTask ",                    /* Name of the task */
        4000,                           /* Stack size in words */
        NULL,                           /* Task input parameter */
        5,                              /* Priority of the task */
        &rtcTaskHandle,                /* Task handle. */
        0);                             /* Core where the task should run */
}

// ------------------ DHT11 -----------------------//
/** Task handle for the light value read task */
TaskHandle_t dht11TaskHandle = NULL;
/** Comfort profile */
ComfortState cf;
DHTesp dht;

void loadDHTData() {
  // Reading temperature for humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
  TempAndHumidity newValues = dht.getTempAndHumidity();
  // Check if any reads failed and exit early (to try again).
  if (dht.getStatus() != 0) {
    if (useSerial) {
      Serial.println("DHT11 error status: " + String(dht.getStatusString()));
    }
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

  temperature=(int)newValues.temperature;
  if (temperature == 0) {
    temperature = -2;
  }
  humidity=(int)newValues.humidity;
  hIndex=heatIndex;
  dPoint=dewPoint;
  cfStatus=comfortStatus;
  if (useSerial) {
    Serial.println(" T:" + String(temperature) + " H:" + String(humidity) + " I:" + String(hIndex) + " D:" + String(dPoint) + " " + cfStatus);
    Serial.println("DHT11 data loaded");
  }
  print=true;
}

/**
 * Task to reads temperature from DHT11 sensor
 * @param pvParameters
 *    pointer to task parameters
 */
void dht11Task(void *pvParameters) {
  if (useSerial) {
    String taskMessage = "DHT11(Temperature, Humidity) task running on core ";
    taskMessage = taskMessage + xPortGetCoreID();
    Serial.println(taskMessage);
  }
  while (1) {
    loadDHTData();
    vTaskDelay(DHT_CHECK_TIME);
  }
}

void initDHT11() {
  pinMode(DHT_DATA_PIN, INPUT);
  dht.setup(DHT_DATA_PIN, DHTesp::DHT11);
  if (useSerial) {
    Serial.println("DHT11 initiated");
  }

  // Start task to get temperature
  xTaskCreatePinnedToCore(
      dht11Task,                       /* Function to implement the task */
      "dht11Task ",                    /* Name of the task */
      4000,                           /* Stack size in words */
      NULL,                           /* Task input parameter */
      5,                              /* Priority of the task */
      &dht11TaskHandle,                /* Task handle. */
      1);                             /* Core where the task should run */
}

// ------------------ LCD -----------------------//
/** Task handle for LCD print task */
TaskHandle_t lcdTaskHandle = NULL;
void printLCD() {
  if (print) {
    if (useSerial) {
      Serial.println("printLCD");
    }
    int waterings = (WATER_MAX_VALUE-flowPulses)/WATERING_DOSE;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(hour);
    lcd.print(":");
    if (minute < 10) {
      lcd.print("0");
    }
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
    print=false;
  }
}

/**
 * Task to update LCD data
 * @param pvParameters
 *    pointer to task parameters
 */
void lcdTask(void *pvParameters) {
  if (useSerial) {
    String taskMessage = "LCD print task running on core ";
    taskMessage = taskMessage + xPortGetCoreID();
    Serial.println(taskMessage);
  }
  while (1) {
    printLCD();
    vTaskDelay(LCD_PRINT_TIME);
  }
}

void initLCD() {
  lcd.init();
  lcd.backlight();
  
  // Start task to get RTC data
  xTaskCreatePinnedToCore(
      lcdTask,                       /* Function to implement the task */
      "lcdTask ",                    /* Name of the task */
      4000,                           /* Stack size in words */
      NULL,                           /* Task input parameter */
      5,                              /* Priority of the task */
      &lcdTaskHandle,                /* Task handle. */
      0);                             /* Core where the task should run */

}
// ----------------- Out Flowmeter --------------------------------//
void IRAM_ATTR outFlowmeterInterrupt() {
  portENTER_CRITICAL_ISR(&muxFlow);
  if (outPressed && flowPulses > 0) {
    flowPulses--;
    if (flowPulses % WATERING_DOSE == 0) {
      outPressed=false;
      digitalWrite(SOLENDOIDVALVE_PIN, LOW);
      digitalWrite(OUT_BUTTON_STATUS_PIN, LOW);
      if (flowPulses == 0) {
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
    print=true;
  }
  portEXIT_CRITICAL_ISR(&muxFlow);
}

void initOutFlowMeter() {
  pinMode(OUT_FLOWMETER_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(OUT_FLOWMETER_PIN), outFlowmeterInterrupt, FALLING);
}

// ----------------- In Flowmeter --------------------------------//
void IRAM_ATTR inFlowmeterInterrupt() {
  portENTER_CRITICAL_ISR(&muxFlow);
  if (inPressed && flowPulses < WATER_MAX_VALUE) {
    flowPulses++;
    if (flowPulses == WATER_MAX_VALUE) {
      inPressed=false;
      digitalWrite(PUMP_PIN, LOW);
      digitalWrite(IN_BUTTON_STATUS_PIN, LOW);
    }
    print=true;
  }
  portEXIT_CRITICAL_ISR(&muxFlow);
}

void initInFlowMeter() {
  pinMode(IN_FLOWMETER_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(IN_FLOWMETER_PIN), inFlowmeterInterrupt, FALLING);
}

// ----------------- Pins mode --------------------------------//
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
  
  server.on("/status", handle_OnConnect);
  server.on("/wateron", handle_wateron);
  server.on("/wateroff", handle_wateroff);
  server.on("/pumpon", handle_pumpon);
  server.on("/pumpoff", handle_pumpoff);
  server.on("/schedule", handle_schedule);
  server.onNotFound(handle_NotFound);
  
  server.begin();
}

void handle_OnConnect() {
  digitalWrite(LED_BUILTIN, HIGH);
  server.send(200, "text/html", SendHTML());
  digitalWrite(LED_BUILTIN, LOW);
}
void handle_wateron() {
  digitalWrite(LED_BUILTIN, HIGH);
  if (!outPressed) {
    outButtonImpl();
  }
  server.send(200, "text/html", SendHTML());
  digitalWrite(LED_BUILTIN, LOW);
}
void handle_wateroff() {
  digitalWrite(LED_BUILTIN, HIGH);
  if (outPressed) {
    outButtonImpl();
  }
  server.send(200, "text/html", SendHTML());
  digitalWrite(LED_BUILTIN, LOW);
}
void handle_pumpon() {
  digitalWrite(LED_BUILTIN, HIGH);
  if (!inPressed) {
    inButtonImpl();
  }
  server.send(200, "text/html", SendHTML());
  digitalWrite(LED_BUILTIN, LOW);
}
void handle_pumpoff() {
  digitalWrite(LED_BUILTIN, HIGH);
  if (inPressed) {
    inButtonImpl();
  }
  server.send(200, "text/html", SendHTML());
  digitalWrite(LED_BUILTIN, LOW);
}
void handle_NotFound(){

  digitalWrite(LED_BUILTIN, HIGH);
  String message = "Received parameters\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(LED_BUILTIN, LOW);
}



void handle_schedule() {
  digitalWrite(LED_BUILTIN, HIGH);
  String dateS="";
  String timeS="";
  for (uint8_t i = 0; i < server.args(); i++) {
    String a=server.argName(i);
    String v=server.arg(i);
    if (strcmp("Date", a.c_str()) == 0) {
      dateS=v;
    } else if (strcmp("Time", a.c_str()) == 0) {
      timeS=v;
    }
  }

  if (dateS == "" && timeS == "") {
    scheduledDT=NULL;
  } else {
    scheduledDT=RtcDateTime(dateS.c_str(), timeS.c_str());
  }
  //server.sendHeader("Location","/status");
  server.send(200, "text/html", SendHTML());
  digitalWrite(LED_BUILTIN, LOW);
}

char months[12][4] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

String SendHTML(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>Watering System Control</title>\n";
  ptr +="<meta http-equiv=\"refresh\" content=\"10\">";
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

  String timeS=String(months[month-1])+" "+(day<10 ? "0":"")+String(day)+" "+String(year)+"-"+(hour<10 ? "0":"")+String(hour)+":"+(minute<10 ? "0":"")+String(minute)+":"+(second<10 ? "0":"")+String(second);
  ptr +="<h1><b>" + timeS + "</b></h1>\n";
  String weatherData="Temp:"+String(temperature)+" Hum:"+String(humidity)+" HotIndex:"+String(hIndex)+" DewPoint:"+String(dPoint)+" Status:" + cfStatus;
  ptr +="<h1><b>" + weatherData + "</b></h1>\n";
  ptr +="<h1>Tank capacity(litres) <b>" + String(WATER_MAX_VALUE) + "</b></h1>\n";
  ptr +="<h1>Tank current status(litres) <b>" + String(WATER_MAX_VALUE-flowPulses) + "</b></h1>\n";
  ptr +="<h1>Tank watering capacity(count) <b>" + String(WATER_MAX_VALUE/WATERING_DOSE) + "</b></h1>\n";
  
  int waterings = (WATER_MAX_VALUE-flowPulses)/WATERING_DOSE;
  ptr +="<h1>Current waterings(count) <b>" + String(waterings) + "</b></h1>\n";

  int precentValue = (WATER_MAX_VALUE - waterings * WATERING_DOSE - flowPulses)*100/WATERING_DOSE;
  ptr +="<h3>Current watering status <b>" + String(precentValue) + "%</b></h3>\n";
  
  if(outPressed) {
    ptr +="<p>Watering: ON</p><a class=\"button button-off\" href=\"/wateroff\">OFF</a>\n";
  } else {
     ptr +="<p>Watering: OFF</p><a class=\"button button-on\" href=\"/wateron\">ON</a>\n";
  }

  if(inPressed) {
    ptr +="<p>Pump: ON</p><a class=\"button button-off\" href=\"/pumpoff\">OFF</a>\n";
  } else {
     ptr +="<p>Pump: OFF</p><a class=\"button button-on\" href=\"/pumpon\">ON</a>\n";
  }

  if (scheduledDT != NULL) {
    uint8_t m=scheduledDT.Month();
    uint8_t d=scheduledDT.Day();
    int y=scheduledDT.Year();
    uint8_t h=scheduledDT.Hour();
    uint8_t mnt=scheduledDT.Minute();
    uint8_t s=scheduledDT.Second();
    
    String scheduledTime=String(months[m-1])+" "+(d<10 ? "0":"")+String(d)+" "+String(y)+"-"+(h<10 ? "0":"")+String(h)+":"+(mnt<10 ? "0":"")+String(mnt)+":"+(s<10 ? "0":"")+String(s);
    ptr +="<h1>Watering scheduled at <b>" + scheduledTime + "</b></h1>\n";
  }
  ptr +="<form action=\"/schedule\">\n";
  ptr +="Date: <input type=\"text\" name=\"Date\"><br>\n";
  ptr +="Time: <input type=\"text\" name=\"Time\"><br>\n";
  ptr +="<input type=\"submit\" value=\"Schedule\">\n";
  ptr +="</form>\n";
  
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

#define DEBOUNCETIME 50

// ----------- Out button interrupt section ----------- //
volatile int outButtonInterrupts = 0;
volatile bool outButtonLastState;
volatile uint32_t outButtonDebounceTimeout = 0;

// For setting up critical sections (enableinterrupts and disableinterrupts not available)
// used to disable and interrupt interrupts

portMUX_TYPE muxOut = portMUX_INITIALIZER_UNLOCKED;

void outButtonImpl() {
  // Out button business logic
  if (flowPulses > 0) {
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

// Interrupt Service Routine - Keep it short!
void IRAM_ATTR handleOutButtonInterrupt() {
  portENTER_CRITICAL_ISR(&muxOut);
  outButtonInterrupts++;
  outButtonLastState = digitalRead(OUT_BUTTON_PIN);
  outButtonDebounceTimeout = xTaskGetTickCount(); //version of millis() that works from interrupt
  portEXIT_CRITICAL_ISR(&muxOut);
}

//
// RTOS Task for reading button pushes (debounced)
//
void taskOutButtonRead( void * parameter) {
  if (useSerial) {
    String taskMessage = "Debounced ButtonRead Task running on core ";
    taskMessage = taskMessage + xPortGetCoreID();
    Serial.println(taskMessage);
  }
  // set up button Pin
  pinMode (OUT_BUTTON_PIN, INPUT);
  pinMode(OUT_BUTTON_PIN, INPUT_PULLUP);  // Pull up to 3.3V on input - some buttons already have this done

  attachInterrupt(digitalPinToInterrupt(OUT_BUTTON_PIN), handleOutButtonInterrupt, FALLING);
  
  uint32_t saveDebounceTimeout;
  bool saveOutButtonLastState;
  int save;

  // Enter RTOS Task Loop
  while (1) {

    portENTER_CRITICAL_ISR(&muxOut); // so that value of outButtonInterrupts,l astState are atomic - Critical Section
    save  = outButtonInterrupts;
    saveDebounceTimeout = outButtonDebounceTimeout;
    saveOutButtonLastState  = outButtonLastState;
    portEXIT_CRITICAL_ISR(&muxOut); // end of Critical Section

    bool currentState = digitalRead(OUT_BUTTON_PIN);

    // This is the critical IF statement
    // if Interrupt Has triggered AND Button Pin is in same state AND the debounce time has expired THEN you have the button push!
    //
    if ((save != 0) //interrupt has triggered
        && (currentState == saveOutButtonLastState) // pin is still in the same state as when intr triggered
        && (millis() - saveDebounceTimeout > DEBOUNCETIME ))
    { // and it has been low for at least DEBOUNCETIME, then valid keypress

      
      if (currentState == LOW) {
        outButtonImpl();
        if (useSerial) {
          Serial.printf("Button is pressed and debounced, current tick=%d\n", millis());
        }
      } else {
        if (useSerial) {
          Serial.printf("Button is released and debounced, current tick=%d\n", millis());
        }
      }
      if (useSerial) {
        Serial.printf("Button Interrupt Triggered %d times, current State=%u, time since last trigger %dms\n", save, currentState, millis() - saveDebounceTimeout);
      }

      portENTER_CRITICAL_ISR(&muxOut); // can't change it unless, atomic - Critical section
      outButtonInterrupts = 0; // acknowledge keypress and reset interrupt counter
      portEXIT_CRITICAL_ISR(&muxOut);

      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/** Task handle for out button press events read task */
TaskHandle_t outButtonTaskHandle = NULL;
void initOutButton() {
  // Start task to get out button press events
  xTaskCreatePinnedToCore(
      taskOutButtonRead,              /* Function to implement the task */
      "outButtonTask",                /* Name of the task */
      4000,                           /* Stack size in words */
      NULL,                           /* Task input parameter */
      5,                              /* Priority of the task */
      &outButtonTaskHandle,           /* Task handle. */
      0);                             /* Core where the task should run */
}

// ----------- In button interrupt section ----------- //
volatile int inButtonInterrupts = 0;
volatile bool inButtonLastState;
volatile uint32_t inButtonDebounceTimeout = 0;

// For setting up critical sections (enableinterrupts and disableinterrupts not available)
// used to disable and interrupt interrupts

portMUX_TYPE muxIn = portMUX_INITIALIZER_UNLOCKED;

void inButtonImpl() {
  // In button business logic
  if (flowPulses < WATER_MAX_VALUE) {
  //if (flowPulses == 0) {
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

// Interrupt Service Routine - Keep it short!
void IRAM_ATTR handleInButtonInterrupt() {
  portENTER_CRITICAL_ISR(&muxIn);
  inButtonInterrupts++;
  inButtonLastState = digitalRead(IN_BUTTON_PIN);
  inButtonDebounceTimeout = xTaskGetTickCount(); //version of millis() that works from interrupt
  portEXIT_CRITICAL_ISR(&muxIn);
}

//
// RTOS Task for reading button pushes (debounced)
//
void taskInButtonRead( void * parameter) {
  if (useSerial) {
    String taskMessage = "Debounced ButtonRead Task running on core ";
    taskMessage = taskMessage + xPortGetCoreID();
    Serial.println(taskMessage);
  }
  // set up button Pin
  pinMode (IN_BUTTON_PIN, INPUT);
  pinMode(IN_BUTTON_PIN, INPUT_PULLUP);  // Pull up to 3.3V on input - some buttons already have this done

  attachInterrupt(digitalPinToInterrupt(IN_BUTTON_PIN), handleInButtonInterrupt, FALLING);
  
  uint32_t saveDebounceTimeout;
  bool saveInButtonLastState;
  int save;

  // Enter RTOS Task Loop
  while (1) {

    portENTER_CRITICAL_ISR(&muxIn); // so that value of outButtonInterrupts,l astState are atomic - Critical Section
    save  = inButtonInterrupts;
    saveDebounceTimeout = inButtonDebounceTimeout;
    saveInButtonLastState  = inButtonLastState;
    portEXIT_CRITICAL_ISR(&muxIn); // end of Critical Section

    bool currentState = digitalRead(IN_BUTTON_PIN);

    // This is the critical IF statement
    // if Interrupt Has triggered AND Button Pin is in same state AND the debounce time has expired THEN you have the button push!
    //
    if ((save != 0) //interrupt has triggered
        && (currentState == saveInButtonLastState) // pin is still in the same state as when intr triggered
        && (millis() - saveDebounceTimeout > DEBOUNCETIME ))
    { // and it has been low for at least DEBOUNCETIME, then valid keypress

      if (currentState == LOW) {
        inButtonImpl();
        if (useSerial) {
          Serial.printf("Button is pressed and debounced, current tick=%d\n", millis());
        }
      } else {
        if (useSerial) {
          Serial.printf("Button is released and debounced, current tick=%d\n", millis());
        }
      }
      if (useSerial) {
        Serial.printf("Button Interrupt Triggered %d times, current State=%u, time since last trigger %dms\n", save, currentState, millis() - saveDebounceTimeout);
      }
      
      portENTER_CRITICAL_ISR(&muxIn); // can't change it unless, atomic - Critical section
      inButtonInterrupts = 0; // acknowledge keypress and reset interrupt counter
      portEXIT_CRITICAL_ISR(&muxIn);

      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/** Task handle for in button press events read task */
TaskHandle_t inButtonTaskHandle = NULL;

void initInButton() {
  // Start task to get in button press events
  xTaskCreatePinnedToCore(
      taskInButtonRead,               /* Function to implement the task */
      "inButtonTask",                 /* Name of the task */
      4000,                           /* Stack size in words */
      NULL,                           /* Task input parameter */
      5,                              /* Priority of the task */
      &inButtonTaskHandle,           /* Task handle. */
      1);                             /* Core where the task should run */
}

void setup() {
  if (useSerial) {
    Serial.begin(9600);
  }
  initIO();
  initRTC(); // core 0
  initDHT11(); // core 0
  initOutFlowMeter();
  initOutButton(); // core 1
  initInFlowMeter();
  initInButton(); // core 1
  initAP(); // core 1
  initLCD(); // core 0
}

void setup1() {
  if (useSerial) {
    Serial.begin(9600);
  }
  
  initAP(); // core 1
}

void loop() {
  server.handleClient();
  delay(500);
}
