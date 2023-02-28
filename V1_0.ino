#include <LiquidCrystal_I2C.h>
#include <ESP32Time.h>
#include "BluetoothSerial.h"
#include "WiFi.h" 
#include "time.h"

#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#include <Preferences.h>


//define the builtin led of the nodemcu
#define LED 2


//define the masks for the days of the week base on bites
#define SUN 0x40
#define MON 0x20
#define TUE 0x10
#define WED 0x08
#define THU 0x04
#define FRI 0x02
#define SAT 0x01


//Define ntp server config
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3*60*60;  //bs as time
const int daylightOffset_sec = 3600;


//Define the timer for checking Alarm
hw_timer_t *My_timer = NULL;

//Define the timer to print the screen in the LCD
hw_timer_t *Timer_LCD = NULL;

//Define preferences to be saved on the EEPROM
Preferences preferences;

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// if you don't know your display address, run an I2C scanner sketch
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);  

//Set RTC
ESP32Time rtc(0); 

//WIFI credentials
String ssid;
String password; 


//todo: verbose mode using serial

//Define Alarm struct
struct alarm_t {
    unsigned int week_day=MON&TUE&WED&THU&FRI&SAT&SUN;  // day of week
    unsigned int hour;      // hour
    unsigned int minute;    // minute
    unsigned int second;    //second
    unsigned long duration; //duration in seconds
    unsigned long progress=0;  //progress of the alarm in miliseconds
    void (*callback)(void);
    void *callback_arg;
};

//Declare Alarm
alarm_t alarm1;

//Set BT
BluetoothSerial SerialBT;



//Get the current time in the ntp
void getNTPTime(){
    struct tm t;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    if(getLocalTime(&t)) rtc.setTimeStruct(t); //the rtc will be set only if the getlocaltime worked
}
///////////////////////////////////////////
//Interrupt of the timer 
//   100ms

long lastState =0; //Aux variable for the last state of the alarmIsBeforeNow function

unsigned int alarm_int=0;
//callback function when timer is up
void IRAM_ATTR onTimer(){
    alarm_int++;
}
    
    //todo: The logic that checks the alarm needs to be improved. its not so clean
 

//this runs when the alarm interrupt is true  
void process_alarm_int(){
    //Starts the alarm
    if (!lastState && alarmIsBeforeNow(alarm1) && !alarm1.progress) alarm1.progress+=100;    
    
    //Keep progressing the alarm
    if (alarm1.progress < alarm1.duration*1000 && alarm1.progress) alarm1.progress+=100;
     
    //Reser the alam when done   
    if (alarm1.progress >= alarm1.duration*1000 && alarm1.progress) alarm1.progress=0;  
    
    //delete or comment after debug
    //Serial.println("debug: ");
    //Serial.println(alarm1.progress);
    //Serial.println(lastState);
    //Serial.println(alarmIsBeforeNow(alarm1));
    ////////  
    
    //set the currect state as the last state 
    lastState=alarmIsBeforeNow(alarm1);
    
    
    //call to action when alam is on 
    if(alarm1.progress) digitalWrite(LED,true);
    else digitalWrite(LED,false);
  
}


//Aux variable to print on the main loop
bool timeToPrint=false;

//Interrupt to graph on the LCD
void IRAM_ATTR onTimerLCD(){
    timeToPrint=true;  
}

//Function that prints on the lCD
void printLCD(){
  //lcd.clear();
  
  lcd.setCursor(0, 1);
  // print time
  lcd.print(rtc.getTime("%A, %H:%M:%S"));
    
  lcd.setCursor(0,0);
  lcd.print("Current Time");
// to do show the date
    
}


//Returns a true if the alam is before now, so if the alarm time has arrived 
bool alarmIsBeforeNow (struct alarm_t alarm_){

    if (!isAlarmToday(alarm1)) return false;
    
    //first the hours
    if (alarm_.hour>rtc.getHour(true)) return false;
    if (alarm_.hour<rtc.getHour(true)) return true;
   
    //then minutes
    if (alarm_.minute>rtc.getMinute()) return false;
    if (alarm_.minute<rtc.getMinute()) return true;    

    //then seconds
    if (alarm_.second>rtc.getSecond()) return false;
    if (alarm_.second<rtc.getSecond()) return true;
   
    
    //this happens when both are the same 
    return true;

}


//check if the alarm is in the planned day of the week
bool isAlarmToday(struct alarm_t alarm){    
       
        return SUN>>rtc.getDayofWeek()&alarm.week_day;  

}


//Split param of the input
String splitString(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

//For dayWeek it extracts the days from the string and applies masks
int extractDayWeek (String days){
    if(!days) return MON&TUE&WED&THU&FRI&SAT&SUN;    
    int aux=0;
    for(int i=0; i<7 ; i++){
        if(splitString(days,',',i).toInt()) aux=SUN>>i|aux;
    }
    return aux;

}



//it prints in the bt bus the alarm time
void BTSerialPrintAlarm(){

    SerialBT.println("Alarm:");
    
    SerialBT.print("Hour: ");
    SerialBT.print(alarm1.hour);
    SerialBT.print("  Min: ");
    SerialBT.print(alarm1.minute);
    SerialBT.print("  Sec: ");
    SerialBT.println(alarm1.second);     
    SerialBT.print("Duration: ");
    SerialBT.println(alarm1.duration);
    SerialBT.print("Days of the week: ");
    if (alarm1.week_day&MON) SerialBT.print("Mon ");
    if (alarm1.week_day&TUE) SerialBT.print("Tue ");
    if (alarm1.week_day&WED) SerialBT.print("Wed ");
    if (alarm1.week_day&THU) SerialBT.print("Thu ");
    if (alarm1.week_day&FRI) SerialBT.print("Fri ");
    if (alarm1.week_day&SAT) SerialBT.print("Sat ");
    if (alarm1.week_day&SUN) SerialBT.print("Sun ");
    SerialBT.println("");
}  

//It prints the network credentials in the BTSerial Bus
void printNetworkCredentials(){
                   SerialBT.println("Network Credentials: ");
                   SerialBT.print("SSID: ");
                   SerialBT.println(ssid);
                   SerialBT.print("Password: ");
                   SerialBT.println(password);
    }

// TODO: move to the BT serial bus
//Scan Wifi networks and print them in the Bluetooth Serial
//void scanWifi(BluetoothSerial Serial){
void scanWifi(){
  SerialBT.println("scan start");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  SerialBT.println("scan done");
  if (n == 0) {
      SerialBT.println("no networks found");
  } else {
    SerialBT.print(n);
    SerialBT.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      SerialBT.print(i + 1);
      SerialBT.print(": ");
      SerialBT.println(WiFi.SSID(i));
      SerialBT.print(" (");
      SerialBT.print(WiFi.RSSI(i));
      SerialBT.println(")");
      SerialBT.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
    }
  }
  SerialBT.println("");
         
}      

//Connects to Wifi network using the saved credentials
void startWifi(){
     
    WiFi.disconnect();
    preferences.begin("credentials", false);
     
    ssid = preferences.getString("ssid", ""); 
    password = preferences.getString("password", "");
    
    preferences.end();
     
    if (ssid == "" || password == ""){ 
        Serial.println("No values saved for ssid or password"); 
        SerialBT.println("No values saved for ssid or password"); 
    } 
    else { // Connect to Wi-Fi 
        WiFi.mode(WIFI_STA); 
        WiFi.begin(ssid.c_str(), password.c_str()); 
 
        Serial.println("Connecting to WiFi .."); 
        SerialBT.println("No values saved for ssid or password");    
        //It checkes 5 times if it's correctly connected
        for (unsigned int i= 0 ; WiFi.status() != WL_CONNECTED && i < 5 ; i++ ) { 
            Serial.println("Attempt to connect");
            SerialBT.println("Attempt to connect");  
            delay(100); 
        } 
        Serial.println(WiFi.localIP());
        SerialBT.println(WiFi.localIP()); 
     }
}



//LCD Light Status
bool LCDLight= false;

//Switch LCD Light
void switchLCDLight(){
    if (LCDLight){
        LCDLight=false;
        lcd.noBacklight();
    } else { 
       LCDLight=true;
       lcd.backlight();
   }
}

void setup(){
    // initialize Bluetooth
    SerialBT.begin("ESP32_v10"); //Bluetooth device name  
    
    // initialize LCD
    lcd.init();
    lcd.clear();
    
    lcd.setCursor(0,0);
    lcd.print("Starting ...");
    
    // turn off LCD backlight                  
    lcd.noBacklight();
    
    //Start Serial
    Serial.begin(115200);
    
    //Set RTC   --- todo: this should be deleted once sntp is established
    rtc.setTime(0, 0, 12, 1, 1, 2023);   
    
    //Print in LCD the BT
    //lcd.setCursor(0, 0);
    //lcd.print(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
    //delay(1000);
    
    //Initialize Alarm using saved params
    
    // Initialize Preferences with alarm namespace. Each application module, library, etc
    // has to use a namespace name to prevent key name collisions. We will open storage in
    // RW-mode (second parameter has to be false).
    // Note: Namespace name is limited to 15 chars.
    preferences.begin("alarm", false);
    alarm1.hour=preferences.getUInt("alarm_hour", 0);
    alarm1.minute=preferences.getUInt("alarm_minute", 0);
    alarm1.second=preferences.getUInt("alarm_second", 0);
    alarm1.week_day=preferences.getUInt("alarm_week_day", 0);
    alarm1.duration=preferences.getULong("alarm_duration",0);
    preferences.end();
    
    
    
    // Intialize the WIFI Preferences   
    startWifi();
    
    //set the timer that checks the alarm
    pinMode(LED, OUTPUT);   //Set the led as the outut for the timer
    My_timer = timerBegin(0, 80, true);
    timerAttachInterrupt(My_timer, &onTimer, true);
    timerAlarmWrite(My_timer, 100000, true);
    timerAlarmEnable(My_timer); //Just Enable

    //set the timer that graphs the LCD
    Timer_LCD = timerBegin(1, 80, true);
    timerAttachInterrupt(Timer_LCD, &onTimerLCD, true);
    timerAlarmWrite(Timer_LCD, 1000000, true);
    timerAlarmEnable(Timer_LCD); //Just Enable   

    //get and set the time from the ntp
    getNTPTime();
     
     //Initialize the value of the aux flag lastState
    lastState=alarmIsBeforeNow(alarm1);

    //Start OTA       
    ArduinoOTA.begin();

} 


void loop() {
    
    //todo: all this back and forth of the bt bus should be habdled trough interrupts
  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }
  if (SerialBT.available()) {
    String input=SerialBT.readString();
    switch(input.charAt(0)){
              //Print time
              case 'p':
                   SerialBT.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
                   break;
              //Set time
              case 's':
                // Format setTime(sc, mn, hr, dy, mt, yr
                  rtc.setTime(
                      splitString(input, ' ', 1).toInt(),
                      splitString(input, ' ', 2).toInt(),
                      splitString(input, ' ', 3).toInt(),
                      splitString(input, ' ', 4).toInt(),
                      splitString(input, ' ', 5).toInt(),
                      splitString(input, ' ', 6).toInt()
                   ); 
                   SerialBT.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
              //Update value of the aux flag lastState after the time change
              lastState=alarmIsBeforeNow(alarm1);
                   break;
              //Print Alarm
              case 'a':
                   BTSerialPrintAlarm();
                   break;
              //Print network credentials
              case 'y':
                   printNetworkCredentials();
                   break;
              //Set credentials:
              case 'x':
                   ssid=splitString(input, ' ', 1),
                   password=splitString(input, ' ', 2);
                   
                   preferences.begin("credentials", false); 
                   preferences.putString("ssid", ssid); 
                   preferences.putString("password", password); 
                   SerialBT.println("Network Credentials Saved"); 
                   preferences.end(); 
                   
                   printNetworkCredentials();
                   break;
              //Set Alarm
              case 'q':
            
                   //Set structure
                   alarm1.hour=splitString(input, ' ', 1).toInt();
                   alarm1.minute=splitString(input, ' ', 2).toInt();
                   alarm1.second=splitString(input, ' ', 3).toInt();
                   alarm1.duration=splitString(input,' ',4).toInt();
                   alarm1.week_day=extractDayWeek(splitString(input,' ',5));
                  
                   //Save in the EEPROM
                   preferences.begin("alarm", false);
                   
                   preferences.putUInt("alarm_hour", alarm1.hour);
                   preferences.putUInt("alarm_minute", alarm1.minute);
                   preferences.putUInt("alarm_second", alarm1.second);
                   preferences.putUInt("alarm_week_day", alarm1.week_day);
                   preferences.putULong("alarm_duration", alarm1.duration);
           
                   preferences.end();       
            
                   BTSerialPrintAlarm();  //it prints the alam on the BTSerial

                  //Update value of the aux flag lastState after the alarm time change
                   lastState=alarmIsBeforeNow(alarm1);
                   break;
              //Restart the ESP
              case 'r':
                   ESP.restart();
              //Scan Wifi Networks
              case 'd':
                   //scanWifi(SerialBT);
                   scanWifi(); // to print in the usb serial bus
                   break;
              case'c':
                   //startWifi
                   startWifi();  //TODO Bug: It restarts the BTSerial
                   break;
              case 'i':
                   //prints IP
                   SerialBT.println("Local IP: ");
                   SerialBT.println(WiFi.localIP());
                   break;
              //Switch dispñay light on off
              case 'l':
                   switchLCDLight();
                   break;
              //Update date with ntp time
              case 'u':
                   getNTPTime();
                   SerialBT.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
                    //Update value of the aux flag lastState after the time change
                   lastState=alarmIsBeforeNow(alarm1);
              //Help Section 
            
                   break;       
              case 'h': 
              default:
                   SerialBT.println(             
                       "Usage:\n"
                       "p - Print current time\n"
                       "a - Print alarm\n"
                       "s %s %m %h %D %M %Y - Set current time\n"
                       "q %h %m %s %duration - Set alarm\n"
                       "d - Scan and display all the available WiFi networks\n"
                       "y - Print network credentials\n"
                       "x %ssid %pass - Set network credentials (NOTE: please add an extra space after password)\n" //TODO we need to fix this
                       "i - Print local IP\n"
                       "c - Connect to Wifi\n"
                       "r - Restart ESP32\n"  
                       "l - Turn on/off LCD Light\n" 
                       "u - Update Date with NTP time\n"         
            );
    }
  }
  if (timeToPrint){
        timeToPrint=false;
        printLCD();
  }
    
  if (alarm_int>0){ 
       alarm_int--;
       process_alarm_int(); 
  }
  ArduinoOTA.handle();    
  delay(20);
}
