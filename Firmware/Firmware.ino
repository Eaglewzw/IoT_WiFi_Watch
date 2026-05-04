#include <ESP8266WiFi.h>

/***SNTP网络时间获取****/
#include <coredecls.h>                  // settimeofday_cb()
#include <Schedule.h>
#include <PolledTimeout.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <sntp.h>                       // sntp_servermode_dhcp()
#include <TZ.h>

/*****闹钟时间存储区****/
#include <EEPROM.h>

/*****解析天气有关的库*****/
#include <ArduinoJson.h>

/****0.96寸OLED显示屏库***/
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"

/***图片有关库****/
#include "WeatherStationImages.h"
#include "font.h"

/***MQTT相关库*****/
#include <PubSubClient.h>

/*采用SmartConfig功能后，这部分注释
 *  WIFI账号和密码
 * const char* WIFI_SSID = "CMCC-9Nkm";
 * const char* WIFI_PWD = "xxxxxxxxxxx";
 */

const char* WIFI_SSID = "CMCC-9Nkm";
const char* WIFI_PWD = "xxxxxxxx";

int WIFINumber = 0;//附近WiFi个数
String WIFIName[20]={};//存放WIFI的名字
int WifiConnect=0;//是否重新连接WiFi标志标志

/***访问心知天气的网站，获取天气数据***/
const int httpPort = 80;
const char* host = "api.seniverse.com";
int WeatherFlag=0;

/****定义当日天气变量********/
char *location_name = "Lanzhou";
char *now_temperature = "24";

/************未来三天预报**********/
typedef struct{
  char forcast_date[11];
  char forcast_code[3];
  char forcast_temperaturerange[7];
}WeatherForcast;

WeatherForcast ForcastContext[3]={
{"0000-00-00","99","00/-00"},
{"0000-00-00","99","00/-00"},
{"0000-00-00","99","00/-00"},
};

int UPDATE_INTERVAL_SECS = 60* 60; //每隔30分钟更新一次外部信息
long timeSinceLastWUpdate = 0;
int UpdateFlag=0;

/********MQTT部分*********/
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_server = "39.105.5.215";//服务器地址
const char*  topic_name = "Eagle_SmartHome";//订阅的主题
/*******MQTT目录选择*******/
int Mqttflag=0;

/*****MQTT数据获得*********/
String Humt="Humt:17.0 %";
String Temp="Temp:26.0°C";

/*******闹钟***********/
typedef struct{
  int hour;//闹钟时
  int minute;//闹钟分
  bool IsOpen;//闹钟开关标志
  int hour_address;//时地址
  int minute_address;//分地址
  int IsOpen_address;//闹钟开关地址

}CLOCK;

CLOCK Clock[3]={
{6,0,0,0,1,6},
{7,0,0,2,3,7},
{8,0,0,4,5,8},
};

int SwitchClock=0;
int Beep_Flag=0,Stop_Flag=0; //闹钟响标志位

/****设置标志*****/
int SetFlag=0;

// 网络时间
#define RTC_UTC_TEST 1510592825 // 1510592825 = Monday 13 November 2017 17:07:05 UTC
#define MYTZ TZ_Asia_Shanghai

// Adjust according to your language
const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static timeval tv;
static time_t now;

static int time_machine_days = 0; // 0 = now
static bool time_machine_running = false;

/**
 *  显示屏设置
 *  初始化OLED管脚 地址为0x3c
 *  sda-pin=GPIO5(D1) and sdc-pin=GPIO4(D2)
*/
int uiFrameIndex = 0;//界面目录
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = D1;
const int SDC_PIN = D2;
SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi   ui( &display );

//数据更新条
void drawProgress(OLEDDisplay *display, int percentage, String label);
//初始数据获取
void updateData(OLEDDisplay *display);
//时间日期获取
void time_is_set_scheduled(void);
//获取当天气数据
void GetCurrentWeather(void);
//获取未来三天的天气
void GetForecastWeather(void);
//闹钟检查
void ClockCheck(void);
//MQTT
void callback(char* topic, byte* payload, unsigned int length);

//五个界面框架
void draw_MeunFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void draw_ClockFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void draw_WeatherFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void draw_MqttFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void draw_SetFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);

bool waitButtonRelease(int pin, int activeState = LOW, int debounceMs = 5, long maxCount = 500000) {
  if (digitalRead(pin) == activeState) {
    delay(debounceMs);
    if (digitalRead(pin) == activeState) {
      long count = 0;
      while (digitalRead(pin) == activeState) {
        if (++count >= maxCount) break;
      }
      return true;
    }
  }
  return false;
}

/**
 *  Add frames
 *  this array keeps function pointers to all frames
 *  frames are the single views that slide from right to left
 */
FrameCallback frames[] = { draw_MeunFram,draw_ClockFram,draw_WeatherFram,draw_MqttFram,draw_SetFram};
int numberOfFrames = 5;
OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;
int DisplayFlag=0;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(10);//申请10个字节的内存,用于存放闹钟信息
  Serial.println();
  Serial.println();

  // setup RTC time
  // it will be used until NTP server will send us real current time
  time_t rtc = RTC_UTC_TEST;
  timeval tv = { rtc, 0 };
  timezone tz = { 0, 0 };
  settimeofday(&tv, &tz);
  settimeofday_cb(time_is_set_scheduled);
  configTime(MYTZ, "pool.ntp.org");
  //sntp_servermode_dhcp(0);//关闭

  // initialize dispaly
  display.init();
  display.clear();
  display.display();
  //设置为垂直显示模式
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

  pinMode(D0,OUTPUT);//LED灯管脚
  pinMode(D3,INPUT); //Back按键
  pinMode(D5,INPUT);//Down按键
  pinMode(D6,INPUT);//Up按键
  pinMode(D7,INPUT);//Set按键
  pinMode(BUILTIN_LED, OUTPUT);  //WIFI模块上的蓝灯

  display.drawXbm(25, 0, 80, 64, LOT_Watch_Logo_bits);
  display.display();

  int counter=0;//计数器
  int auto_connect_flag=0;//自动连接标志
  WiFi.mode(WIFI_STA);
  WIFINumber = WiFi.scanNetworks();//附近WiFi个数
  for(int i=0;i< WIFINumber;i++)
  {
    if(WiFi.SSID()==WiFi.SSID(i))  //如果扫描列表中存在已经记录的WiFi，这直接连接
    {
      auto_connect_flag=1;
      break;
    }
  }

  if(auto_connect_flag)
  {
      WiFi.begin(WiFi.SSID(), WiFi.psk());
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        display.clear();
        display.drawString(64, 10, "Connecting to WiFi");
        display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
        display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
        display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
        display.display();
        counter++;
    }
  }
  else{
  WiFi.beginSmartConfig();          //开启SmartConfig服务
  delay(500);
  uint32_t WiFiTimer=millis();
  while(!WiFi.smartConfigDone())        //连接不成功标志
  {
    delay(500);
    display.clear();
    Serial.print("SmartConfig Connecting...");
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();
    counter++;
    //若3分钟还未连接，判断无法连接WiFi
    if(millis()-WiFiTimer>1000L*(3*60)){
      WiFi.stopSmartConfig();//关闭SmartConfig
      Serial.println("SmartConfig Connecting  Failed");
      display.drawString(64, 10, "WiFi Connect ERROR!!!");
      delay(1000);
      break;
    }
  }
 }

  if(WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WIFI Connected");
    Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
    Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
  }

  digitalWrite(D0, LOW); // 亮灯
  delay(200);           // 延时500ms
  digitalWrite(D0, HIGH);// 灭灯
  delay(200);          // 延时500ms
  digitalWrite(D0, LOW); // 亮灯
  delay(200);           // 延时500ms
  digitalWrite(D0, HIGH);// 灭灯
  delay(200);

  //将上次掉电的数据读取出来
  for(int i=0;i<3;i++)
  {
    Clock[i].hour=EEPROM.read(Clock[i].hour_address);
    if(Clock[i].hour >=24  || Clock[i].hour<0)  Clock[i].hour = 7; //第一次读数据赋值
    Clock[i].minute=EEPROM.read(Clock[i].minute_address);
    if(Clock[i].minute >=60 || Clock[i].minute<0)  Clock[i].minute = 0; //第一次读数据赋值
    Clock[i].IsOpen=EEPROM.read(Clock[i].IsOpen_address);
  }

  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);

  ui.setIndicatorPosition(BOTTOM);//设置指示圈位置，该出为底部，还有TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorDirection(LEFT_RIGHT);//指示圈方向模式
  ui.setFrameAnimation(SLIDE_LEFT);//滑动模式，这里为左滑动模式，还有SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrames(frames, numberOfFrames);
  ui.setOverlays(overlays, numberOfOverlays);
  ui.disableAutoTransition();//取消自动模式

  ui.switchToFrame(0);//选择第一个开机画面

  ui.init();  //Inital UI takes care of initalising the display too.
  display.flipScreenVertically();
  Serial.println("");
  Serial.println("");
  updateData(&display);
  timeSinceLastWUpdate = millis();

}

void loop() {
    ui.update();//刷新显示屏
    /*
     * 主界面共两个任务
     *1、选择所在目录
     *2、定时查询天气
     **/
    if (waitButtonRelease(D7, HIGH, 3)) {
      uiFrameIndex++;
      if(uiFrameIndex == 5)
        uiFrameIndex = 0;
    }
    ui.switchToFrame(uiFrameIndex);
    //检测闹钟是否到了
    ClockCheck();
    //MQTT接收数据
    client.loop();//MUC接收数据的主循环函数。

    if ( (millis() - timeSinceLastWUpdate > (1000L*UPDATE_INTERVAL_SECS)) || UpdateFlag==1 ) {
        Serial.println("Update Weather...");
        timeSinceLastWUpdate = millis();
        client.disconnect();//先断开MQTT的client,因为一次只能只能连接一个端口
        GetCurrentWeather();  //更新当前天气信息
        GetForecastWeather(); //更新未来三天天气信息
        UpdateFlag=0;
      }

}

//主界面
//主要显示日期、时间、天气
void draw_MeunFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  int i=0;
  static int count=0;
  static int key=1;//不支持连按标志
  SetFlag=0;//设置功能归零
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  //显示日期
  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon+1, timeInfo->tm_year + 1900);
  display->drawString(44 + x, 0 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  //显示当前时间
  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(45 + x, 10 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);

   //选择按键 不支持连按
   if(digitalRead(D3) == LOW&&key){
      delay(3);
      key=0;//不支持连按
      if(digitalRead(D3) == LOW){
        DisplayFlag++;
        if(DisplayFlag==2) {DisplayFlag=0;}
        while(digitalRead(D3) == LOW){
          i++;//防止进入死循环
          if(i>=300000){
            i=0;
            break;
          }
        }
      }
    }else if(digitalRead(D3) == HIGH) {key=1;}

    if(DisplayFlag==1&&uiFrameIndex==0)      {display->displayOff();digitalWrite(D0, LOW);Beep_Flag=0;Stop_Flag=1; }
    else if(DisplayFlag==0&&uiFrameIndex==0) {display->displayOn(); digitalWrite(D0, HIGH);}

  //显示所在地区
  if(Beep_Flag==0){
    display->drawXbm(0, 36, 16, 16, hz16[53]);//位置:
    display->drawXbm(16, 36, 16, 16, hz16[54]);//
    display->drawXbm(32, 36, 16, 16, hz16[55]);//

    display->drawXbm(48, 36, 16, 16, hz16[56]);//兰州
    display->drawXbm(64, 36, 16, 16, hz16[57]);//
  }
  else{
    //该起床了！ 闪烁效果
    count++;
    if(count%2){
      display->drawXbm(0, 36, 16, 16, hz16[70]);
      display->drawXbm(16, 36, 16, 16, hz16[71]);
      display->drawXbm(32, 36, 16, 16, hz16[72]);
      display->drawXbm(48, 36, 16, 16, hz16[73]);
      display->drawXbm(64, 36, 16, 16, hz16[74]);
    }
    else{
      display->drawXbm(0, 36, 16, 16, hz16[85]);
      display->drawXbm(16, 36, 16, 16, hz16[85]);
      display->drawXbm(32, 36, 16, 16, hz16[85]);
      display->drawXbm(48, 36, 16, 16, hz16[85]);
      display->drawXbm(64, 36, 16, 16, hz16[85]);
    }

  }
  //显示WiFi是否连接标志
  display->drawXbm(98, 0, 10, 8,WIFI);
  //显示电量
  display->drawXbm(112, 0, 16, 9,Power[0]);

  //显示所获取的气温
  String temperature = String(now_temperature);
  int temp=temperature.toInt();
  if (temp<0){
    temp=-temp;
    if(temp>0 && temp<10) display->drawXbm(100, 12, 10, 40, Figure[10]);
    if(temp>=10)          display->drawXbm(90, 12, 10, 40, Figure[10]);
    }
  if(temp>=0 && temp<10)
      display->drawXbm(110, 12, 10, 40, Figure[temp]);
    else if(temp>=10){
      display->drawXbm(100, 12, 10, 40,Figure[temp/10]);
      display->drawXbm(110, 12, 10, 40, Figure[temp%10]);
    }
    display->drawXbm(120, 14, 8, 8, thermometer);

}

void ClockCheck(void){
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);

  int Hour=timeInfo->tm_hour, Minute=timeInfo->tm_min;
  //检测三个闹钟是否到了
  for(int i=0;i<3;i++)
  {
      if(Clock[i].IsOpen==1 && Hour==Clock[i].hour && Minute == Clock[i].minute)
      {
       if(Stop_Flag==0)  Beep_Flag=1;
       else Beep_Flag=0;
      }
      else Stop_Flag=0;
  }
  if(Beep_Flag==1){
    digitalWrite(D0, LOW); // 亮灯
    delay(200);           // 延时500ms
    digitalWrite(D0, HIGH);// 灭灯
    delay(200);          // 延时500ms
    digitalWrite(D0, LOW); // 亮灯
    delay(200);           // 延时500ms
    digitalWrite(D0, HIGH);// 灭灯
    delay(200);
   //选择按下
   if (waitButtonRelease(D3, LOW, 3, 90000)) {
          Beep_Flag=0;
          Stop_Flag=1;
    }
  }
}

//事务闹钟界面
void draw_ClockFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  //char Clock_0[10],Clock_1[10],Clock_2[10];
  char ClockString[3][10];
  int WriteFlag=0;
  display->drawVerticalLine(42, 0, 52);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(DialogInput_bold_12);
  display->drawXbm(0, 8, Icon_width, Icon_height, Clock_Icon_bits);//闹钟图标

  //选择按下
   if (waitButtonRelease(D3)) {
        SwitchClock++;
        if(SwitchClock==10)
          SwitchClock=0;
    }
  /********设置三个事务闹钟**********/
  for(int i=0;i<3;i++)
  {
    sprintf_P(ClockString[i], PSTR("%02d:%02d"), Clock[i].hour,Clock[i].minute);
    display->drawString(72, 18*i,  String(ClockString[i]));
    if(Clock[i].IsOpen ==0)
      display->drawXbm(94, 18*i, 32, 16,Close_Icon);//关
    else
      display->drawXbm(94, 18*i, 32, 16,Open_Icon);//开
  }

  switch(SwitchClock){

    case 0:
      //将修改的数据保存到EEPROM
      for(int i=0;i<3;i++)
      {
         EEPROM.write(Clock[i].hour_address, Clock[i].hour);
         EEPROM.write(Clock[i].minute_address, Clock[i].minute);
         EEPROM.write(Clock[i].IsOpen_address, Clock[i].IsOpen);
      }
      EEPROM.commit();
    break;

    //第一个事务闹钟"时"控制
    case 1:
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[0].hour++;
        if(Clock[0].hour>=24) Clock[0].hour=0;
    }
     //Down按键被按下
     if (waitButtonRelease(D5)) {
        Clock[0].hour--;
        if(Clock[0].hour<=-1) Clock[0].hour=23;
    }
      display->drawRect(54, 0, 17, 15);
      display->drawXbm(45, 4, 8, 8,Pointer_Icon);//指针
    break;

    case 2:
   //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[0].minute++;
        if(Clock[0].minute>=60) Clock[0].minute=0;
    }
        //Down按键被按下
     if (waitButtonRelease(D5)) {
        Clock[0].minute--;
        if(Clock[0].minute<=-1) Clock[0].minute=59;
    }
      display->drawRect(75, 0, 17, 15);
      display->drawXbm(45, 4, 8, 8,Pointer_Icon);//指针
    break;

    case 3:
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[0].IsOpen=1;
    }
     //Down按键被按下
     if (waitButtonRelease(D5)) {
          Clock[0].IsOpen=0;
    }
      display->drawRect(94, 0, 32, 16);
      display->drawXbm(45, 4, 8, 8,Pointer_Icon);//指针
    break;

    //第二个事务闹钟
    case 4:
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[1].hour++;
        if(Clock[1].hour>=24) Clock[1].hour=0;
    }
     //Down按键被按下
     if (waitButtonRelease(D5)) {
        Clock[1].hour--;
        if(Clock[1].hour<=-1) Clock[1].hour=23;
    }
      display->drawRect(54, 18, 17, 15);
      display->drawXbm(45, 22, 8, 8,Pointer_Icon);//指针
    break;

    case 5:
    //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[1].minute++;
        if(Clock[1].minute>=60) Clock[1].minute=0;
    }
     //Down按键被按下
     if (waitButtonRelease(D5)) {
        Clock[1].minute--;
        if(Clock[1].minute<=-1) Clock[1].minute=59;
    }
      display->drawRect(75, 18, 17, 15);
      display->drawXbm(45, 22, 8, 8,Pointer_Icon);//指针
    break;

    case 6:
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[1].IsOpen=1;
    }
        //Down按键被按下
     if (waitButtonRelease(D5)) {
          Clock[1].IsOpen=0;
    }
      display->drawRect(94, 18, 32, 16);
      display->drawXbm(45, 22, 8, 8,Pointer_Icon);//指针
    break;

    case 7:
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[2].hour++;
        if(Clock[2].hour>=24) Clock[2].hour=0;
    }
    //Down按键被按下
     if (waitButtonRelease(D5)) {
        Clock[2].hour--;
        if(Clock[2].hour<=-1) Clock[2].hour=23;
    }
      display->drawRect(54, 36, 17, 15);
      display->drawXbm(45, 40, 8, 8,Pointer_Icon);//指针
    break;

    case 8:
    //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[2].minute++;
        if(Clock[2].minute>=60) Clock[2].minute=0;
    }

        //Down按键被按下
     if (waitButtonRelease(D5)) {
        Clock[2].minute--;
        if(Clock[2].minute<=-1) Clock[2].minute=59;
    }
      display->drawRect(75, 36, 17, 15);
      display->drawXbm(45, 40, 8, 8,Pointer_Icon);//指针
    break;

    case 9:
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        Clock[2].IsOpen=1;
    }
     //Down按键被按下
     if (waitButtonRelease(D5)) {
          Clock[2].IsOpen=0;
    }
      display->drawRect(94, 36, 32, 16);
      display->drawXbm(45, 40, 8, 8,Pointer_Icon);//指针
      WriteFlag=1;
    break;
  }
}

//天气告知界面
void draw_WeatherFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  SwitchClock=0;
  int WeatherCode=99;//默认为未知

  String temp="";

  display->setColor(WHITE);
  display->setFont(DialogInput_bold_12);//设置字体

      //Up按键被按下
     if (waitButtonRelease(D6)) {
        WeatherFlag++;
        if(WeatherFlag==3)
            WeatherFlag=0;
    }

        //Down按键被按下
     if (waitButtonRelease(D5)) {
          WeatherFlag--;
        if(WeatherFlag<0)
            WeatherFlag=2;
    }
    //截取日期 格式为2018-02-14  截取后面5位
    temp = String(ForcastContext[WeatherFlag].forcast_code);
    display->drawString(122, 1, "("+String(ForcastContext[WeatherFlag].forcast_date).substring(5)+")");
    display->drawString(110, 38, String(ForcastContext[WeatherFlag].forcast_temperaturerange));
  if(WeatherFlag==0){
    display->drawXbm(2, 0, 8, 8, activeSymbole);
    display->drawXbm(14, 0, 8, 8,inactiveSymbole);
    display->drawXbm(26, 0, 8, 8,inactiveSymbole);
    display->drawXbm(40, 0, 16, 16, hz16[0]);//今
  }

  else if(WeatherFlag==1){

    display->drawXbm(2, 0, 8, 8, inactiveSymbole);
    display->drawXbm(14, 0, 8, 8,activeSymbole);
    display->drawXbm(26, 0, 8, 8,inactiveSymbole);
    display->drawXbm(40, 0, 16, 16, hz16[1]);//明
  }
  else if(WeatherFlag==2){
    display->drawXbm(2, 0, 8, 8, inactiveSymbole);
    display->drawXbm(14, 0, 8, 8,inactiveSymbole);
    display->drawXbm(26, 0, 8, 8,activeSymbole);
    display->drawXbm(40, 0, 16, 16, hz16[2]);//后
  }

  //显示预报的日期、天气情况、温度范围
  display->drawXbm(56, 0, 16, 16, hz16[3]);//天
  display->drawXbm(40, 36, 16, 16, hz16[4]);//温
  display->drawXbm(56, 36, 16, 16, hz16[5]);//度

  //display->drawXbm(55, 30, 10, 20, thermometer_Icon_bits); //显示温度计计图标

  WeatherCode = temp.toInt();//转成成整形
  switch(WeatherCode){
    case 0:
        display->drawString(100,18,"(Sunny)");
        display->drawXbm(100, 18, 16, 16, hz16[6]);//晴
        display->drawXbm(0, 10, Icon_width, Icon_height, Sunny_Icon_bits);
    break;

    case 1:
        display->drawString(100,18,"(Clear)");
        display->drawXbm(100, 18, 16, 16, hz16[6]);//晴
        display->drawXbm(0, 10, Icon_width, Icon_height, Sunny_Icon_bits);
    break;

    case 2:
    case 3:
        display->drawString(100,18,"(Fair)");
        display->drawXbm(100, 18, 16, 16, hz16[6]);//晴
        display->drawXbm(0, 10, Icon_width, Icon_height, Sunny_Icon_bits);
        break;

    case 4:
        display->drawXbm(56, 18, 16, 16, hz16[8]);//多
        display->drawXbm(72, 18, 16, 16, hz16[9]);//云
        display->drawXbm(0, 10, Icon_width, Icon_height, Partly_Cloudy_Icon_bits);
        break;

    case 5: case 6:
      display->drawXbm(50, 18, 16, 16, hz16[6]);//晴
      display->drawXbm(66, 18, 16, 16, hz16[7]);//间
      display->drawXbm(82, 18, 16, 16, hz16[8]);//多
      display->drawXbm(98, 18, 16, 16, hz16[9]);//云
      display->drawXbm(0, 10, Icon_width, Icon_height, Partly_Cloudy_Icon_bits);
    break;

    case 7:
    case 8:
      display->drawXbm(50, 18, 16, 16, hz16[10]);//大
      display->drawXbm(66, 18, 16, 16, hz16[13]);//部
      display->drawXbm(82, 18, 16, 16, hz16[8]);//多
      display->drawXbm(98, 18, 16, 16, hz16[9]);//云
      display->drawXbm(0, 10, Icon_width, Icon_height, Partly_Cloudy_Icon_bits);
    break;

    case 9:
      display->drawString(110,18,"(Overcast)");
      display->drawXbm(110, 18, 16, 16, hz16[14]);//阴
      display->drawXbm(0, 10, Icon_width, Icon_height, Overcast_Icon_bits);
      break;

    case 10:
      display->drawXbm(56, 18, 16, 16, hz16[16]);//阵
      display->drawXbm(72, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 11:
      display->drawXbm(60, 18, 16, 16, hz16[15]);//雷
      display->drawXbm(76, 18, 16, 16, hz16[16]);//阵
      display->drawXbm(92, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Thundershower_Icon_bits);
    break;

    case 12:
      display->drawXbm(40, 18, 16, 16, hz16[15]);//雷
      display->drawXbm(54, 18, 16, 16, hz16[16]);//阵
      display->drawXbm(68, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(82, 18, 16, 16, hz16[18]);//加
      display->drawXbm(96, 18, 16, 16, hz16[19]);//冰
      display->drawXbm(110, 18, 16, 16, hz16[20]);//雹
      display->drawXbm(0, 10, Icon_width, Icon_height, Hail_Icon_bits);
    break;

    case 13:
      display->drawXbm(56, 18, 16, 16, hz16[12]);//小
      display->drawXbm(72, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 14:
      display->drawXbm(56, 18, 16, 16, hz16[11]);//中
      display->drawXbm(72, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 15:
      display->drawXbm(56, 18, 16, 16, hz16[10]);//大
      display->drawXbm(72, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 16:
      display->drawXbm(56, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(72, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 17:
      display->drawXbm(60, 18, 16, 16, hz16[10]);//大
      display->drawXbm(76, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(92, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 18:
      display->drawXbm(50, 18, 16, 16, hz16[41]);//特
      display->drawXbm(60, 18, 16, 16, hz16[10]);//大
      display->drawXbm(76, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(92, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 19:
      display->drawXbm(56, 18, 16, 16, hz16[34]);//冻
      display->drawXbm(72, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(0, 10, Icon_width, Icon_height, Rain_Icon_bits);
    break;

    case 20:
      display->drawXbm(60, 18, 16, 16, hz16[17]);//雨
      display->drawXbm(76, 18, 16, 16, hz16[35]);//夹
      display->drawXbm(92, 18, 16, 16, hz16[36]);//雪
      display->drawXbm(0, 10, Icon_width, Icon_height, Sleet_Icon_bits);
    break;

    case 21:
      display->drawXbm(56, 18, 16, 16, hz16[16]);//阵
      display->drawXbm(72, 18, 16, 16, hz16[36]);//雪
      display->drawXbm(0, 10, Icon_width, Icon_height, Snow_Icon_bits);
    break;

    case 22:
      display->drawXbm(56, 18, 16, 16, hz16[12]);//小
      display->drawXbm(72, 18, 16, 16, hz16[36]);//雪
      display->drawXbm(0, 10, Icon_width, Icon_height, Snow_Icon_bits);
    break;

    case 23:
      display->drawXbm(56, 18, 16, 16, hz16[11]);//中
      display->drawXbm(72, 18, 16, 16, hz16[36]);//雪
      display->drawXbm(0, 10, Icon_width, Icon_height, Snow_Icon_bits);
    break;

    case 24:
      display->drawXbm(56, 18, 16, 16, hz16[10]);//大
      display->drawXbm(72, 18, 16, 16, hz16[36]);//雪
      display->drawXbm(0, 10, Icon_width, Icon_height, Snow_Icon_bits);
    break;

    case 25:
      display->drawXbm(56, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(72, 18, 16, 16, hz16[36]);//雪
      display->drawXbm(0, 10, Icon_width, Icon_height, Snow_Icon_bits);
    break;

    case 26:
      display->drawXbm(56, 18, 16, 16, hz16[21]);//浮
      display->drawXbm(72, 18, 16, 16, hz16[22]);//尘
      display->drawXbm(0, 10, Icon_width, Icon_height, Duststorm_Icon_bits);
    break;

    case 27:
      display->drawXbm(56, 18, 16, 16, hz16[23]);//扬
      display->drawXbm(72, 18, 16, 16, hz16[24]);//沙
      display->drawXbm(0, 10, Icon_width, Icon_height, Duststorm_Icon_bits);
    break;

    case 28:
      display->drawXbm(60, 18, 16, 16, hz16[24]);//沙
      display->drawXbm(76, 18, 16, 16, hz16[22]);//尘
      display->drawXbm(92, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(0, 10, Icon_width, Icon_height, Duststorm_Icon_bits);
    break;

    case 29:
      display->drawXbm(50, 18, 16, 16, hz16[25]);//强
      display->drawXbm(60, 18, 16, 16, hz16[24]);//沙
      display->drawXbm(76, 18, 16, 16, hz16[22]);//尘
      display->drawXbm(92, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(0, 10, Icon_width, Icon_height, Duststorm_Icon_bits);
    break;

    case 30:
      display->drawString(100,18,"(Foggy)");
      display->drawXbm(100, 18, 16, 16, hz16[28]);//雾
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 31:
      display->drawString(100,18,"(Haze)");
      display->drawXbm(100, 18, 16, 16, hz16[29]);//霾
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 32:
      display->drawString(100,18,"(Windy)");
      display->drawXbm(100, 18, 16, 16, hz16[27]);//风
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 33:
      display->drawXbm(56, 18, 16, 16, hz16[10]);//大
      display->drawXbm(72, 18, 16, 16, hz16[27]);//风
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 34:
      display->drawXbm(56, 18, 16, 16, hz16[30]);//飓
      display->drawXbm(72, 18, 16, 16, hz16[27]);//风
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 35:
      display->drawXbm(50, 18, 16, 16, hz16[38]);//热
      display->drawXbm(60, 18, 16, 16, hz16[26]);//带
      display->drawXbm(76, 18, 16, 16, hz16[27]);//风
      display->drawXbm(92, 18, 16, 16, hz16[33]);//暴
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 36:
      display->drawXbm(60, 18, 16, 16, hz16[31]);//龙
      display->drawXbm(76, 18, 16, 16, hz16[32]);//卷
      display->drawXbm(92, 18, 16, 16, hz16[27]);//风
      display->drawXbm(0, 10, Icon_width, Icon_height, Haze_Icon_bits);
    break;

    case 37:
      display->drawString(100,18,"(Cold)");
      display->drawXbm(100, 18, 16, 16, hz16[37]);//冷
      display->drawXbm(0, 10, Icon_width, Icon_height, Cold_Icon_bits);
    break;

    case 38:
      display->drawString(100,18,"(Hot)");
      display->drawXbm(100, 18, 16, 16, hz16[38]);//热
      display->drawXbm(0, 10, Icon_width, Icon_height, Sunny_Icon_bits);
    break;

    case 99:
      display->drawXbm(0, 8, Icon_width, Icon_height, Unkonwn_Icon_bits);
      display->drawXbm(56, 18, 16, 16, hz16[39]);//未
      display->drawXbm(72, 18, 16, 16, hz16[40]);//知
      break;
  }
  display->drawXbm(112, 36, 16, 16, Temperature_Icon_bits);

}

void draw_SetFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  int temp;
  static int Brightness=100;//屏幕亮度，默认100%
  static int BeepVoice=100;//蜂鸣器响度 默认100%
  static int Oled_Close=0;//息屏时间
  static int counter = 0;

  Mqttflag =0;//归位前一个界面的标志

  display->setFont(ArialMT_Plain_10);

 //选择按下
   if (waitButtonRelease(D3)) {
        SetFlag++;
        if(SetFlag==5) SetFlag=0;
    }

     //Up按键被按下
     if (waitButtonRelease(D6)) {
          if(SetFlag==0){
              Brightness=Brightness+25;
              if(Brightness==125) Brightness=0;
          }
         else if(SetFlag==1){
              BeepVoice=BeepVoice+25;
              if(BeepVoice==125) BeepVoice=0;
          }
         else if(SetFlag==2){
             Oled_Close++;
              if(Oled_Close==4) Oled_Close=0;
          }
          else if(SetFlag==4){
              WifiConnect=1;//选择连接WiFi
          }
    }

     //Down按键被按下
     if (waitButtonRelease(D5)) {
        if(SetFlag==0){
          Brightness=Brightness-25;
            if(Brightness<0) Brightness=100;
          }
        else if(SetFlag==1){
              BeepVoice=BeepVoice-25;
              if(BeepVoice<0) BeepVoice=100;
          }
         else if(SetFlag==2){
             Oled_Close--;
              if(Oled_Close<0) Oled_Close=3;
          }
        else if(SetFlag==4){
              WifiConnect=0;//不选择连接WiFi
          }
    }
  display->drawXbm(0, 8, Icon_width, Icon_height, Set_Icon_bits);//设置图标
  display->drawVerticalLine(42, 0, 52);//图标右侧竖线

  if(SetFlag==0){
    display->drawXbm(58, 0, 16, 16, hz16[58]);//屏幕亮度
    display->drawXbm(72, 0, 16, 16, hz16[59]);//
    display->drawXbm(88, 0, 16, 16, hz16[60]);//
    display->drawXbm(104, 0, 16, 16, hz16[61]);//
    display->drawString(128 , 22 ,"%");
    display->setFont(ArialMT_Plain_24);
    display->drawString(120 , 22 ,String(Brightness));
    if(Brightness==0){
          display->drawXbm(46, 18, 32, 32, menu_brightness[4]);
          display->setBrightness(5);
        }
    else if(Brightness==25){
          display->drawXbm(46, 18, 32, 32, menu_brightness[3]);

          display->setBrightness(40);
        }
    else if(Brightness==50){
          display->drawXbm(46, 18, 32, 32, menu_brightness[2]);
          display->setBrightness(80);
        }
    else if(Brightness==75){
          display->drawXbm(46, 18, 32, 32, menu_brightness[1]);
          display->setBrightness(127);
        }
    else{
          display->drawXbm(46, 18, 32, 32, menu_brightness[0]);
          display->setContrast(100, 241, 64);
        }
    }
    else if(SetFlag==1){
    display->drawXbm(46, 18, 32, 32,Voice);
    display->drawString(128 , 22 ,"%");//显示显示百分比
    display->drawXbm(56, 0, 16, 16, hz16[69]);//声音控制
    display->drawXbm(72, 0, 16, 16, hz16[75]);//
    display->drawXbm(88, 0, 16, 16, hz16[76]);//
    display->drawXbm(104, 0, 16, 16, hz16[77]);//
    display->setFont(ArialMT_Plain_24);
    display->drawString(120 , 22 ,String(BeepVoice));

  }
  else if(SetFlag==2){
    display->drawXbm(46, 18, 32, 32,ScreenShot);
    display->drawString(128 , 22 ,"min");
    display->drawXbm(56, 0, 16, 16, hz16[62]);//自动息屏
    display->drawXbm(72, 0, 16, 16, hz16[63]);//
    display->drawXbm(88, 0, 16, 16, hz16[64]);//
    display->drawXbm(104, 0, 16, 16, hz16[58]);//
    display->setFont(ArialMT_Plain_24);
    switch(Oled_Close)
    {
      case 0:
       temp=1;
       display->drawString(100 , 22 ,String(temp));
      break;
      case 1:
        temp=2;
        display->drawString(100 , 22 ,String(temp));
      break;
      case 2:
        temp=5;
        display->drawString(100 , 22 ,String(temp));
      break;
      case 3:
        temp=10;
        display->drawString(110 , 22 ,String(temp));
      break;
    }
  }

   else if(SetFlag==3){
    display->drawXbm(46, 18, 32, 32,Battery);
    display->drawXbm(56, 0, 16, 16, hz16[65]);//电池性能
    display->drawXbm(72, 0, 16, 16, hz16[66]);//
    display->drawXbm(88, 0, 16, 16, hz16[67]);//
    display->drawXbm(104, 0, 16, 16, hz16[68]);//

  }
   else if(SetFlag==4){
    display->drawXbm(56, 0, 16, 16, hz16[78]);//网络连接
    display->drawXbm(72, 0, 16, 16, hz16[79]);//
    display->drawXbm(88, 0, 16, 16, hz16[80]);//
    display->drawXbm(104, 0, 16, 16, hz16[81]);//

    display->drawXbm(46, 36, 16, 16, hz16[83]);//
    display->drawXbm(62, 36, 16, 16, hz16[78]);//
    display->drawXbm(78, 36, 16, 16, hz16[84]);//

    if(WifiConnect==1){

      display->drawXbm(94, 36, 32, 16,Open_Icon);//开
       //如果连接就显示连接的WiFi名称
      if(WiFi.status() == WL_CONNECTED) {
        String Name="";
        Name=WiFi.SSID();
        display->drawXbm(46, 18, 16, 16,WIFI_Connect); //显示WiFi图标
        display->drawString(128, 20,Name);//显示已经连接的WiFi名称(右对齐)
      }
      else if(WiFi.status()!= WL_CONNECTED) {
        display->drawXbm(46, 18, 16, 16,WIFI_DisConnect); //显示未连接WiFi图标
        display->drawXbm(72, 18, 16, 16, hz16[80]);//连接中
        display->drawXbm(88, 18, 16, 16, hz16[81]);//
        display->drawXbm(104, 18, 16, 16, hz16[11]);//
        WiFi.beginSmartConfig();          //开启SmartConfig服务
        delay(500);
        while(!WiFi.smartConfigDone())        //连接不成功标志
        {
          counter++;
          delay(500);
          //若半分钟还未连接，判断无法连接WiFi
          if(counter==120){
              display->drawXbm(94, 36, 32, 16,Open_Icon);//开
              WifiConnect=0;
              break;
          }
        }
        UpdateFlag=1;
    }

    }else{
       //如果连接就显示连接的WiFi
       display->drawXbm(94, 36, 32, 16,Close_Icon);//关
       if(WiFi.status() == WL_CONNECTED) {
          String Name="";
          Name=WiFi.SSID();
          display->drawXbm(46, 18, 16, 16,WIFI_Connect); //显示WiFi图标
          display->drawString(128, 20,Name);//显示已经连接的WiFi名称(右对齐)
       }else{
         display->drawXbm(46, 18, 16, 16,WIFI_DisConnect); //显示未连接WiFi图标
         display->drawXbm(72, 18, 16, 16, hz16[39]);//未连接
         display->drawXbm(88, 18, 16, 16, hz16[80]);//
         display->drawXbm(104, 18, 16, 16, hz16[81]);//
         WiFi.stopSmartConfig();//关闭SmartConfig
       }
    }
  }
}

//电脑端测试
//服务器端IP：39.105.5.215
//端口：1883
void draw_MqttFram(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y){
  static uint32_t changeTime=millis();
  static uint32_t MQTTTimer=millis();
  //定义控制变量
  static int LED_flag=0;
  static int Fan_Speed=0;
  static int IsOpenMqtt=0;//默认MQTT关闭
  char msg[50]="";
  WeatherFlag=0;
  display->setFont(DialogInput_bold_12);
  display->drawXbm(0, 8, Icon_width, Icon_height, Home_Icon_bits);  //家具标志图标
  display->drawVerticalLine(42, 0, 52);

    //选择
     if (waitButtonRelease(D3)) {
        Mqttflag++;
        if(Mqttflag==4)
          Mqttflag=0;
    }

  switch(Mqttflag){
    case 0:
    display->drawString(86, 0,"MQTT");
    display->drawString(84, 36,"MQTT:");
    display->drawXbm(88, 0, 16, 16, hz16[80]);//连接
    display->drawXbm(104, 0, 16, 16, hz16[81]);//
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        IsOpenMqtt=1;
    }

     //Down按键被按下
     if (waitButtonRelease(D5)) {
        IsOpenMqtt=0;
    }
    if(IsOpenMqtt)
    {
      if(client.connected())
      {
        display->drawXbm(72, 18, 16, 16, hz16[82]);//已连接
        display->drawXbm(88, 18, 16, 16, hz16[80]);//
        display->drawXbm(104, 18, 16, 16, hz16[81]);//
        display->drawXbm(94, 36, 32, 16,Open_Icon);//开
        client.subscribe(topic_name);//接收外来的数据时的intopic
      }else{
        while (!client.connected())
        {
          display->drawXbm(94, 36, 32, 16,Open_Icon);//开
          Serial.println("MQTT Connecting...");
          client.setServer(mqtt_server, 1883);
          client.setCallback(callback);
          client.connect("LOT_Watch");//连接MQTT
          display->drawXbm(72, 18, 16, 16, hz16[80]);//连接中
          display->drawXbm(88, 18, 16, 16, hz16[81]);//
          display->drawXbm(104, 18, 16, 16, hz16[11]);//
          delay(1000);
          if(millis()-MQTTTimer>1000L*(3*60))
          {
            Serial.println("MQTT Connected failed");
            break;
           }
        }
        if(client.connected())
        {
            display->drawXbm(72, 18, 16, 16, hz16[82]);//已连接
            display->drawXbm(88, 18, 16, 16, hz16[80]);//
            display->drawXbm(104, 18, 16, 16, hz16[81]);//
            display->drawXbm(94, 36, 32, 16,Open_Icon);//开
            Serial.println("MQTT Connected");
            client.subscribe(topic_name);//接收外来的数据时的intopic
        }
      }
    }
    else{
      client.disconnect();
      display->drawXbm(94, 36, 32, 16,Close_Icon);
      display->drawXbm(72, 18, 16, 16, hz16[39]);//未连接
      display->drawXbm(88, 18, 16, 16, hz16[80]);//
      display->drawXbm(104, 18, 16, 16, hz16[81]);//
    }
    break;

    case 1:
    display->drawXbm(96, 0, 16, 16, hz16[44]);//台
    display->drawXbm(112, 0, 16, 16, hz16[45]);//灯
     //Up按键被按下
     if (waitButtonRelease(D6)) {
        LED_flag=1;
    }

     //Down按键被按下
     if (waitButtonRelease(D5)) {
        LED_flag=0;
    }
     if(LED_flag==0){
        display->drawXbm(54, 2, 34, 50, TaiDeng_OFF_Icon_bits);  //家具标志图标
        display->drawXbm(94, 36, 32, 16,Close_Icon);//关
        if(millis()-changeTime>1000 &&client.connected())
        {
          Serial.println("LED_OFF");
          client.publish("Eagle_Watch", "LED_OFF");//发主题Eagle_Watch
          changeTime=millis();
        }

     }
     else{
        display->drawXbm(54, 2, 34, 50, TaiDeng_ON_Icon_bits);  //家具标志图标
        display->drawXbm(94, 36, 32, 16,Open_Icon);
        if(millis()-changeTime>1000 &&client.connected())//1秒钟发送一次
        {
          Serial.println("LED_ON");
          client.publish("Eagle_Watch", "LED_ON");//发主题Eagle_Watch
          changeTime=millis();
        }

     }
    break;

    case 2:
        display->drawXbm(96, 0, 16, 16, hz16[46]);//空
        display->drawXbm(112, 0, 16, 16, hz16[47]);//调
        display->drawXbm(96, 18, 16, 16, hz16[48]);//风
        display->drawXbm(112, 18, 16, 16, hz16[49]);//速
        display->drawXbm(44, 0, 50, 50, Kongtiao_Icon_bits);  //空调标志图标
        display->drawRect(96, 36, 28, 16);
         //Up按键被按下
     if (waitButtonRelease(D6)) {
        Fan_Speed+=10;if(Fan_Speed==110) Fan_Speed=0;
    }

     //Down按键被按下
     if (waitButtonRelease(D5)) {
        Fan_Speed-=10;if(Fan_Speed==-10) Fan_Speed=100;
    }

    display->drawString(120, 36, String(Fan_Speed));
    snprintf(msg,14,"Fan_Speed:%d",Fan_Speed);
   if(millis()-changeTime>1000 &&client.connected())
    {
      Serial.println(msg);
      client.publish("Eagle_Watch",msg);
      changeTime=millis();
    }
    break;

    case 3:
      display->drawXbm(46, 2, 16, 16, hz16[50]);//室
      display->drawXbm(62, 2, 16, 16, hz16[51]);//内
      display->drawXbm(78, 2, 16, 16, hz16[4]);//温
      display->drawXbm(94, 2, 16, 16, hz16[52]);//湿
      display->drawXbm(110, 2, 16, 16, hz16[5]);//度
      display->drawHorizontalLine(42, 20, 84);
      display->drawHorizontalLine(42, 0, 84);
      display->setFont(DialogInput_bold_12);
      display->drawString(128 , 22 ,Humt);
      display->drawString(128, 38,Temp);
    break;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String temp="";
  //Serial.print("Message arrived [");
  //Serial.print(topic);
  //Serial.print("] ");

  //接收的信息
  for (int i = 0; i < length; i++) {
    //转变成字符串，将数组每一位连接成字符串
    temp=temp+(char)payload[i];
    //Serial.print((char)payload[i]);
  }
  //Serial.print(temp);
  //截取出字符串
  Humt=temp.substring(0,11);
  Temp=temp.substring(11,23);

  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}

//底层界面，包括显示时间、电量
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);

  String mtemp = String("100")  +  "%";
  display->drawString(128, 54, mtemp);

  display->drawHorizontalLine(0, 54, 128);
}

/**获取到的Json数据格式
 * {
 *    "results": [
 *        {
 *            "location": {
 *                "id": "WX4FBXXFKE4F",
 *                "name": "北京",
 *                "country": "CN",
 *                "path": "北京,北京,中国",
 *                "timezone": "Asia/Shanghai",
 *                "timezone_offset": "+08:00"
 *            },
 *            "now": {
 *                "text": "多云",
 *                "code": "4",
 *                "temperature": "23"
 *            },
 *            "last_update": "2019-10-13T09:51:00+08:00"
 *        }
 *    ]
 *}
 */
//获取当前天气

void GetCurrentWeather(void)
{
    //向心知天气的服务器发送请求。
    //连接服务器并判断是否连接成功，若成功就发送GET 请求数据下发
    String json_from_server;
    uint8_t ErrorTimes=0;//未成功的次数计算
    WiFiClient client;//创建网络对象
    RETRY_CURRENT:if(client.connect(host, httpPort)==1)
    {
        client.print("GET /v3/weather/now.json?key=cinm0okk7gzgtujn&location=Lanzhou&language=en&unit=c HTTP/1.1\r\nHost:api.seniverse.com\r\n\r\n");
        String status_code = client.readStringUntil('\r');
        Serial.println(status_code);
        if(client.find("\r\n\r\n")==1)
        {
          json_from_server=client.readStringUntil('\n');
          Serial.println(json_from_server);
        }
    }
    else
    {
      ErrorTimes++;
      if(ErrorTimes>5)
        goto END_CURRENT;
      Serial.println("Weather connection failed this time");
      delay(1000);
      goto RETRY_CURRENT;
    }

    END_CURRENT:DynamicJsonDocument jsonBuffer(500);
    // Parse JSON object
    //将天气数据放入jsonBuffer
    DeserializationError error = deserializeJson(jsonBuffer, json_from_server);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    //以下代码分别提取地区，当前天气，代码，及湿度
    Serial.println("Start Decode:");
    strcpy(location_name,jsonBuffer["results"][0]["location"]["name"]);
    strcpy(now_temperature,jsonBuffer["results"][0]["now"]["temperature"]);
    //strcpy(now_text,jsonBuffer["results"][0]["now"]["text"]);
    //strcpy(now_code,jsonBuffer["results"][0]["now"]["code"]);

    //client.stop();     //关闭HTTP客户端，采用HTTP短链接，数据请求完毕后要客户端要主动断开
}

//获取未来三天的天气信息
void GetForecastWeather(void)
{
    uint8_t ErrorTimes=0;
    String json_from_server;
    WiFiClient client;

    RETRY_FORECAST:if(client.connect(host, httpPort)==1)
    {
        client.print("GET /v3/weather/daily.json?key=cinm0okk7gzgtujn&location=lanzhou&language=en&unit=c&start=0&days=4 HTTP/1.1\r\nHost: api.seniverse.com\r\n\r\n");
        String status_code = client.readStringUntil('\r');
        Serial.println(status_code);
        if(client.find("\r\n\r\n")==1)
        {
          json_from_server=client.readStringUntil('\n');
          Serial.println(json_from_server);
        }
    }
    else
    {
      ErrorTimes++;
      if(ErrorTimes>5)
        goto END_FORECAST;
      Serial.println("Weather connection failed this time");
      delay(1000);
      goto RETRY_FORECAST;
    }

    END_FORECAST:DynamicJsonDocument jsonBuffer(2000);
    //将天气数据放入jsonBuffer
    DeserializationError error = deserializeJson(jsonBuffer, json_from_server);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    //以下代码分别提取地区，当前天气，代码，及湿度
    char* Localtemp="000";//临时变量，注意字符个数
    for(int i=0;i<3;i++)
    {
       strcpy(ForcastContext[i].forcast_code,jsonBuffer["results"][0]["daily"][i]["code_day"]);
       strcpy(ForcastContext[i].forcast_date,jsonBuffer["results"][0]["daily"][i]["date"]);
       strcpy(ForcastContext[i].forcast_temperaturerange,jsonBuffer["results"][0]["daily"][i]["high"]); //最高温度
       strcpy(Localtemp,jsonBuffer["results"][0]["daily"][i]["low"]); //最低温度
       strcat(ForcastContext[i].forcast_temperaturerange,"/"); //连接两个字符串
       strcat(ForcastContext[i].forcast_temperaturerange,Localtemp);
    }

      //strcpy(forcast_text1,jsonBuffer["results"][0]["daily"][0]["text_day"]); //今天天气现象
      //strcpy(forcast_text2,jsonBuffer["results"][0]["daily"][1]["text_day"]); //明天天气现象
      //strcpy(forcast_text3,jsonBuffer["results"][0]["daily"][2]["text_day"]); //后天天气现象

      client.stop();     //关闭HTTP客户端，采用HTTP短链接，数据请求完毕后要客户端要主动断开

}

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10); //设置字体
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void updateData(OLEDDisplay *display) {
  uint32_t MQTTTimer=millis();
  drawProgress(display, 30, "Updating time...");
  delay(2500);//等待SNTP服务器响应
  drawProgress(display, 30, "Updating weather...");
  GetCurrentWeather();//获取当天天气
  GetForecastWeather();//获取未来三天的天气
  drawProgress(display, 80, "Mqtt Connect...");
  while (!client.connected()){
    Serial.println("MQTT Connecting...");
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    client.connect("LOT_Watch");//连接MQTT
    delay(1000);
    if(millis()-MQTTTimer>1000L*(3*60))
    {
      Serial.println("MQTT Connected failed");
      break;
     }
  }
  if(client.connected())
  {
     Serial.println("MQTT Connected");
    client.subscribe(topic_name);//接收外来的数据时的intopic
  }
  drawProgress(display, 100, "System Config...");
}

void time_is_set_scheduled() {
  // everything is allowed in this function
  if (time_machine_days == 0) {
    time_machine_running = !time_machine_running;
  }

  // time machine demo
  if (time_machine_running) {
    if (time_machine_days == 0)
      Serial.printf("---- settimeofday() has been called - possibly from SNTP\n"
                    "     (starting time machine demo to show libc's automatic DST handling)\n\n");
    now = time(nullptr);
    const tm* tm = localtime(&now);
    Serial.printf("future=%3ddays: DST=%s - ",
                  time_machine_days,
                  tm->tm_isdst ? "true " : "false");
    Serial.print(ctime(&now));
    gettimeofday(&tv, nullptr);
    constexpr int days = 30;
    time_machine_days += days;
    if (time_machine_days > 360) {
      tv.tv_sec -= (time_machine_days - days) * 60 * 60 * 24;
      time_machine_days = 0;
    } else {
      tv.tv_sec += days * 60 * 60 * 24;
    }
    settimeofday(&tv, nullptr);
  } else {
      gettimeofday(&tv, nullptr);
      now = time(nullptr);
      // human readable
      Serial.print("ctime:     ");
      Serial.print(ctime(&now));
  }

}
