#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Audio.h>
#include "tft_setup.h"      //needs to be above TFT_eSPI?
#include <TFT_eSPI.h>                 // Include the graphics library (this includes the sprite functions)
//#include <FS.h>
//#include <SPIFFS.h>
#include <SPI.h>
#include <Preferences.h>
#include "jjj_80px.c"
#include "jj.c"
#include "abc_melb.c"
#include "jazz.c"
#include "kids.c"
#include "rn.c"
#include "secretagent80.c"
#include "news.c"
#include "wifi.c"
#include "wifi_medium.c"
#include "wifi_low.c"
#include "wifi_none.c"
#include "wifi_gone.c"


/*
TODO:
--------------

use a rotary encoder..
mute icon
local temperature - display and push to home assistant - mqtt
wifi strength indicator - better bmps
use sprites for overwriting text to avoid flicker - not enough memory to use whole screen
anti aliased fonts
program/song info - scrape data?
show input/battery voltage
disaply cleanup
display date/time - ntp is working - is ntp using lots of ram?

weather forecast - data from home assistant?
make more responsive, currently pauses when changing stations
	- make buttons change variables only, background service polls for changes and applys


DONE:
-------------------
backlight pwm control
use pot as a radio dial
use pushImage to draw images from c files instead of SPIFFS - might save enough ram to use sprites
volume as percentage 
save last station and volume
station logos
mute


Pins:
--------------
Buttons 15,4,14 & 12
POT (ADC) 35
Vin (ADC) 34
SDA 21
SCL 22
TFT/SD
  SCK   18
  MISO  19
  MOSI  23
  TCS   5
  RST   not connected
  DC    17
  SDCS  16 - unused for now
  Lite  32
MAX98367A
  LRCLK 26
  DCLK  27
  DIN   25
  SD    33


*/

//**************************************************************************************************
//                                         C O N S T A N T S                                       *
//**************************************************************************************************

//#define FS_NO_GLOBALS

// Digital I/O used
//#define SD_CS         16
//#define SPI_MOSI      23
//#define SPI_MISO      19
//#define SPI_SCK       18
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26
#define I2S_SD   33   // Shutdown pin, mute on LOW
#define TFT_PWM  32   // Display backlight PWM

//Setting PWM properties
const int freq = 5000;
const int pwmChannel = 0;
const int resolution = 8;

//Onboard LED
const int onboardled = 2;

//Buttons
const int Pin_vol_up = 15;
const int Pin_vol_down = 4;
const int Pin_mute = 14;
const int Pin_brightness = 12;

//Analoge inputs
const int Pin_POT_ADC = 35;
const int batteryPin_ADC = 34;

const float batteryVoltageDivider = 0.0017098445595855;
const float maxBatteryVoltage = 4.2; // replace with the maximum voltage of your LiPo battery
const float minBatteryVoltage = 3.0; // replace with the minimum voltage of your LiPo battery
const float maxBatteryCapacity = 1000; // replace with the maximum capacity of your LiPo battery in mAh
const float minBatteryCapacity = 0; // replace with the minimum capacity of your LiPo battery in mAh

// Lookup table mapping LiPo battery voltage to percentage of charge
const float voltageLookupTable[] = {
  3.0,  // 0%
  3.3,  // 10%
  3.44, // 20%
  3.55, // 30%
  3.66, // 40%
  3.77, // 50%
  3.88, // 60%
  3.99, // 70%
  4.1,  // 80%
  4.2   // 90-100%
};

const int voltageLookupTableSize = sizeof(voltageLookupTable) / sizeof(float);


//One time saving wifi details
//const char* ssid = "";
//const char* password = "";
//Once saved, declare as strings
String ssid;
String password;

// List of stations
String stations[] = {
    "https://mediaserviceslive.akamaized.net/hls/live/2038308/triplejnsw/master.m3u8",//--------------------------- 1
    "https://mediaserviceslive.akamaized.net/hls/live/2038315/doublejnsw/master.m3u8",//--------------------------- 2
    "www.abc.net.au/res/streaming/audio/aac/local_melbourne.pls",//------------------------------------------------ 3
    "http://www.abc.net.au/res/streaming/audio/aac/radio_national.pls", //using aac as hls at 32khz  skips heaps -- 4
    "http://www.abc.net.au/res/streaming/audio/aac/news_radio.pls", //using aac as hls at 32khz  skips heaps ------ 5
    "https://mediaserviceslive.akamaized.net/hls/live/2038319/abcjazz/master.m3u8",//------------------------------ 6
    "https://mediaserviceslive.akamaized.net/hls/live/2038321/abcextra/master.m3u8",//----------------------------- 7
    "http://somafm.com/secretagent130.pls"};//--------------------------------------------------------------------- 8
int station_index = 0;
int station_count = sizeof(stations) / sizeof(stations[0]);

// Settings for NTP Client
const long utcOffsetInSeconds = 39600;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// For pot smoothing
const int num_readings = 10; // number of readings to average

// Drawing positions
const int wifiLogoXpos = 0;
const int stationLogoXpos = 240;


//**************************************************************************************************
//                                         V A R I A B L E S                                       *
//**************************************************************************************************

// For pot smoothing
int readings[num_readings];  // array to store readings
int pot_index = 0;              // index of current reading

// Variable to store volume (0-21)
int volume = 11;
int mute_state = 0; //0 means mute
int brightness = 128; //(0-255)

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;

//Declare some variables for tracking timing 
uint button_time = 0;
uint display_time = 0;
uint last_pot_change = 0; //Track the last change to delay the station change
boolean station_changed = 0;



//**************************************************************************************************
//                                           O B J E C T S                                         *
//**************************************************************************************************

// Invoke TFT library, create tft and sprite objects
TFT_eSPI tft = TFT_eSPI(); // Create object "tft"
//TFT_eSprite sprite = TFT_eSprite(&tft); // Create Sprite object "sprite" with pointer to "tft" object

// Create audio object
Audio audio;

// Create preferences object
Preferences preferences;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);



//**************************************************************************************************
//                                        F U N C T I O N S                                        *
//**************************************************************************************************

void draw_wifi() {
  // Get the current wifi signal strength (RSSI)
  long rssi = WiFi.RSSI();

  // Determine which wifi icon to display based on RSSI
  if (rssi >= -55) { 
    // If RSSI is stronger than -55, display the strongest wifi icon
    tft.pushImage(wifiLogoXpos, 0, 32, 32, wifi);
  } else if (rssi >= -65) {
    tft.pushImage(wifiLogoXpos, 0, 32, 32, wifi_medium);
  } else if (rssi >= -75) {
    tft.pushImage(wifiLogoXpos, 0, 32, 32, wifi_low);
  } else if (rssi >= -85) {
    tft.pushImage(wifiLogoXpos, 0, 32, 32, wifi_none);
  } else if (rssi >= -96) {
    tft.pushImage(wifiLogoXpos, 0, 32, 32, wifi_gone);
  } else {
    Serial.print("Unknown RSSI value");
  }
}

void open_new_radio(int station_index, int mute_state)
{
    //Serial.println("**********Open new URL************");
    //Serial.print("URL:        ");
    //Serial.println(stations[station_index].c_str());

    // Mute the output
    digitalWrite(I2S_SD, LOW);
    // Change station
    audio.connecttohost(stations[station_index].c_str());

    //Save station_index
    //preferences.putInt("station_index", station_index);

    //delay(200); // Stay muted till audio is good
    // Unmute the output if it should be
    digitalWrite(I2S_SD, mute_state);
}

void write_new_logo(int station_index)
{
  //clear name and title area
  tft.fillRect(0, 90, 320, 70, TFT_BLACK);

  //write station number
  tft.fillRect(180, 40, 60, 21, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(180, 40, 4);
  tft.println((String)"St:"+(station_index + 1));
  //sprite.pushSprite(0, 0);

  //draw station logo
  switch(station_index){
    case 0: {tft.pushImage(stationLogoXpos, 0, 80, 80, jjj_80px); break;}
    case 1: {tft.pushImage(stationLogoXpos, 0, 80, 80, jj); break;}
    case 2: {tft.pushImage(stationLogoXpos, 0, 80, 80, abc_melb); break;}
    case 3: {tft.pushImage(stationLogoXpos, 0, 80, 80, rn); break;}
    case 4: {tft.pushImage(stationLogoXpos, 0, 80, 80, news); break;}
    case 5: {tft.pushImage(stationLogoXpos, 0, 80, 80, jazz); break;}
    case 6: {tft.pushImage(stationLogoXpos, 0, 80, 80, kids); break;}
    case 7: {tft.pushImage(stationLogoXpos, 0, 80, 80, secretagent80); break;}
  }
}

void write_volume(int vol)
{
    int volpercent = (map(vol, 0, 21, 0, 100 ) );
    tft.fillRect(10, 40, 160, 21, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 40, 4);
    tft.println((String)"Vol:"+volpercent+"%");
    //sprite.pushSprite(0, 0);
    //Save volume
    preferences.putInt("volume", vol);
}

void write_stationName(String sName)
{
    tft.fillRect(0, 90, 320, 30, TFT_BLACK);
    tft.setTextWrap(false, false); // Wrap on width and height switched off
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(0, 90, 4);
    tft.print(sName);
    //sprite.pushSprite(0, 0);
}

void write_streamTitle(String sTitle)
{
    tft.fillRect(0, 130, 320, 30, TFT_BLACK);
    tft.setTextWrap(false, false); // Wrap on width and height switched off
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(0, 130, 4);
    tft.print(sTitle);
}

// function to read and smooth potentiometer value - written by chatgpt :)
int read_smoothed_pot() {
  // read the raw value from the potentiometer
  int raw_value = analogRead(Pin_POT_ADC);
  
  // add the current reading to the readings array
  readings[pot_index] = raw_value;
  
  // increment the index and wrap around if necessary
  pot_index = (pot_index + 1) % num_readings;
  
  // calculate the average of the last num_readings values
  int smoothed_value = 0;
  for (int i = 0; i < num_readings; i++) {
    smoothed_value += readings[i];
  }
  smoothed_value /= num_readings;
  
  // map the smoothed value to a station index
  int station_index = map(smoothed_value, 0, 4096, 0, station_count);
  
  return station_index;
}

// Function to map LiPo battery voltage to percentage of charge using a lookup table
float mapBatteryVoltageToPercentage(float batteryVoltage) {
  if (batteryVoltage <= voltageLookupTable[0]) {
    return 0;
  }
  if (batteryVoltage >= voltageLookupTable[voltageLookupTableSize - 1]) {
    return 100;
  }
  for (int i = 0; i < voltageLookupTableSize - 1; i++) {
    if (batteryVoltage >= voltageLookupTable[i] - 0.2 && batteryVoltage < voltageLookupTable[i + 1] + 0.2) {
      float percentage = map(batteryVoltage, voltageLookupTable[i], voltageLookupTable[i + 1], i * 10, (i + 1) * 10);
      return percentage;
    }
  }
  // Default return statement in case no value is returned from the for loop
  return -1;
}


//**************************************************************************************************
//                                           S E T U P                                             *
//**************************************************************************************************
void setup() {
    
    //Serial
    Serial.begin(115200);
    //Serial.print("Setup");
    //Serial.print("Pins init");

    analogReadResolution(12); // set the ADC resolution to 12 bits (4096 levels)

    //IO mode init
    pinMode(onboardled,OUTPUT);
    pinMode(Pin_vol_up, INPUT_PULLUP);
    pinMode(Pin_vol_down, INPUT_PULLUP);
    pinMode(Pin_mute, INPUT_PULLUP);
    pinMode(Pin_brightness, INPUT_PULLUP);
    pinMode(I2S_SD, OUTPUT);

    // Mute the output until ready
    digitalWrite(I2S_SD, LOW);

    // configure LED PWM functionalitites
    ledcSetup(pwmChannel, freq, resolution);
  
    // attach the channel to the GPIO to be controlled
    ledcAttachPin(TFT_PWM, pwmChannel);
    
    //Now initialise the TFT
    tft.init();
    tft.setRotation(1);  // 0 & 2 Portrait. 1 & 3 landscape
    tft.setSwapBytes(true); // Required when using pushImage
    tft.fillScreen(TFT_BLACK);
    
    //sprite.createSprite(tft.width(), tft.height());

    //Splash screen
    tft.setCursor(140, 0, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("ESP32!");
    //sprite.pushSprite(0, 0);
    
    //Use preferences to save config
    preferences.begin("internet_radio", false); 

    //Retreive last volume, station_index and brightness
    volume = preferences.getInt("volume", 7); //if the key does not exist, return a default value of 11
    //station_index = preferences.getInt("station_index", 0); //if the key does not exist, return a default value of 0 - the pot is a form of memory.. no need to store this :)
    mute_state = preferences.getInt("mute_state", 0); //if the key does not exist, return a default value of 0
    brightness = preferences.getInt("brightness", 128); //if the key does not exist, return a default value of 128

    //Set brightness
    ledcWrite(pwmChannel, brightness);
    
    //One time saving wifi details
    //preferences.putString("ssid", ssid); 
    //preferences.putString("password", password);
    
    //Retreive wifi details
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");

    //WiFi
    if (ssid == "" || password == ""){
      Serial.println("No values saved for ssid or password");
    }else {
      //Connect to Wi-Fi
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      //Serial.printf("Connecting to %s ", ssid.c_str());
      WiFi.begin(ssid.c_str(), password.c_str());
      while (WiFi.status() != WL_CONNECTED)
      {
          delay(100);
          Serial.print(".");
      }
      //Serial.println(" Connected");
      //Serial.println(WiFi.localIP()); 
    }

    //Initialize a NTPClient to get time
    timeClient.begin();
    timeClient.update();
        
    //Audio(I2S)
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume); // 0...21

    //Show volume on the screen
    write_volume(volume);

    //Get the station_index from the pot
    int station_index = map(analogRead(Pin_POT_ADC), 0, 4096, 0, station_count);

    //Show the logo
    write_new_logo(station_index);

    //Start the audio stream
    open_new_radio(station_index, mute_state);     

}



//**************************************************************************************************
//                                            L O O P                                              *
//**************************************************************************************************
void loop()
{
  audio.loop();

  //Display logic - update stuff once per second
  if (millis() - display_time > 1000)
  {

    //Serial.print("display loop");

    draw_wifi();

    //tft.fillRect(32, 0, 208, 21, TFT_BLACK);
    //tft.setTextColor(TFT_WHITE, TFT_BLACK);
    //tft.setCursor(38, 0, 4);
    //tft.println((String)timeClient.getHours()+":"+timeClient.getMinutes());

    /*
    // Show the current time
    Serial.print(daysOfTheWeek[timeClient.getDay()]);
    Serial.print(", ");
    Serial.print(timeClient.getHours());
    Serial.print(":");
    Serial.print(timeClient.getMinutes());
    Serial.print(":");
    Serial.println(timeClient.getSeconds());
    //Serial.println(timeClient.getFormattedTime());
    */

    float batteryVoltage = analogRead(batteryPin_ADC) * batteryVoltageDivider; // read the battery voltage and scale it using the voltage divider factor
    float batteryPercentage = mapBatteryVoltageToPercentage(batteryVoltage); // map the battery voltage to a percentage of charge using the lookup table
    float batteryCapacity = batteryPercentage / 100 * maxBatteryCapacity; // calculate the battery capacity in mAh based on the percentage and maximum capacity
    batteryCapacity = constrain(batteryCapacity, minBatteryCapacity, maxBatteryCapacity); // limit the battery capacity to the range of minimum to maximum capacity
    Serial.print("Battery Voltage: ");
    Serial.print(batteryVoltage, 2); // print the battery voltage with 2 decimal places
    Serial.print(" V, Battery Percentage: ");
    Serial.print(batteryPercentage, 1); // print the battery percentage with 1 decimal place
    Serial.print("%, Battery Capacity: ");
    Serial.print(batteryCapacity, 0); // print the battery capacity without decimal places
    Serial.println(" mAh");

    tft.fillRect(40, 0, 180, 21, TFT_BLACK);
    tft.setCursor(40, 0, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println((String)batteryVoltage+"V");   
    tft.setCursor(100, 0, 4);
    tft.println((String)", "+batteryPercentage+"%");

    
    display_time = millis(); //Update time display was updated
  }

  //Button logic
  if (millis() - button_time > 150)
  {

    //Brightness (bottom right) and up
    if ((digitalRead(Pin_brightness) == 0) && (digitalRead(Pin_vol_up) == 0))
    {
      //Serial.println("Pin_brightness");
      if (brightness < 250)
        brightness = brightness + 5;
      ledcWrite(pwmChannel, brightness);
      preferences.putInt("brightness", brightness);
      button_time = millis();
    }

    //Brightness (bottom right) and down
    else if ((digitalRead(Pin_brightness) == 0) && (digitalRead(Pin_vol_down) == 0))
    {
      if (brightness > 5)
        brightness = brightness - 5;
      ledcWrite(pwmChannel, brightness);
      preferences.putInt("brightness", brightness);
      button_time = millis();
    }

    //Mute (bottom middle)
    else if (digitalRead(Pin_mute) == 0)
    {
      //Serial.println("Pin_mute");
      mute_state = !mute_state;
      digitalWrite(I2S_SD, mute_state);
      preferences.putInt("mute_state", mute_state);
      button_time = millis();
    }

    //Both vol buttons (used to be mute)
    else if ((digitalRead(Pin_vol_up) == 0) && (digitalRead(Pin_vol_down) == 0))
    {
        Serial.println("Both vol buttons");
    }
      
    //Vol up
    else if (digitalRead(Pin_vol_up) == 0)
    {
        if (volume < 21)
          volume++;
        audio.setVolume(volume);
        write_volume(volume);
        mute_state = 1;
        digitalWrite(I2S_SD, mute_state);
        preferences.putInt("mute_state", mute_state);
        button_time = millis();
    }
    
    //Vol down
    else if (digitalRead(Pin_vol_down) == 0)
    {
      //Serial.println("Pin_vol_down");
      if (volume > 0)
        volume--;
      audio.setVolume(volume);
      write_volume(volume);
      button_time = millis();
    }
  }


  // read and smooth the potentiometer value
  int pot_station = read_smoothed_pot();

  // check if the dial has changed
  if (pot_station != station_index) {
    station_index = pot_station;
    write_new_logo(station_index);
    last_pot_change = millis();
    station_changed = 1;
  }  

  if ((millis() - last_pot_change > 500) && (station_changed == 1))             //The goal is to only change the station when required and 500ms after the last dial input
  {                                                                             //The display is updated almost instantly so this will make the dial feel responsive
    open_new_radio(station_index, mute_state);
    station_changed = 0;
  }

    
}




//**************************************************************************************************
//                                           E V E N T S                                           *
//**************************************************************************************************
void audio_info(const char *info)
{
    Serial.print("audio_info        ");
    Serial.println(info);
}
void audio_id3data(const char *info)
{ //id3 metadata
    Serial.print("id3data     ");
    Serial.println(info);
}
 
void audio_eof_mp3(const char *info)
{ //end of file
    Serial.print("eof_mp3     ");
    Serial.println(info);
}
void audio_showstation(const char *info)
{
    Serial.print("station     ");
    Serial.println(info);
    write_stationName(String(info));
}
void audio_showstreaminfo(const char *info)
{
    Serial.print("streaminfo  ");
    Serial.println(info);
}
void audio_showstreamtitle(const char *info)
{
    Serial.print("streamtitle ");
    Serial.println(info);
    String sinfo=String(info);
    sinfo.replace("|", "\n");
    write_streamTitle(sinfo);
}
void audio_bitrate(const char *info)
{
    Serial.print("bitrate     ");
    Serial.println(info);
}
void audio_commercial(const char *info)
{ //duration in sec
    Serial.print("commercial  ");
    Serial.println(info);
}
void audio_icyurl(const char *info)
{ //homepage
    Serial.print("icyurl      ");
    Serial.println(info);
}
void audio_lasthost(const char *info)
{ //stream URL played
    Serial.print("lasthost    ");
    Serial.println(info);
}
void audio_eof_speech(const char *info)
{
    Serial.print("eof_speech  ");
    Serial.println(info);
}
