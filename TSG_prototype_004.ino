//------------------------------------------------------------
//    姿勢制御フィルタリングプログラム
//                Arduino　IDE　1.6.11
//
//　　　Arduino　　　　　　　　LSM9DS1基板　
//　　　　3.3V　　　------　　　　3.3V
//　　　　GND       ------   　　 GND
//　　　　SCL       ------        SCL
//　　　　SDA       ------        SDA
//
//　センサーで取得した値をシリアルモニターに表示する
//
//　　　　
//----------------------------------------------------------//


#include <SPI.h>                                //SPIライブラリ
#include <Wire.h>                               //I2Cライブラリ
#include <SparkFunLSM9DS1.h>                  //LSM9DS1ライブラリ：https://github.com/sparkfun/LSM9DS1_Breakout
#include <SD.h>
#include <LSM9DS1_Registers.h>
#include <LSM9DS1_Types.h>
#include <SoftwareSerial.h>
#include <MsTimer2.h>            //https://github.com/PaulStoffregen/MsTimer2


//#define ADAddr 0x48//

#define LSM9DS1_M  0x1E                 // SPIアドレス設定 0x1C if SDO_M is LOW
#define LSM9DS1_AG  0x6B                // SPIアドレス設定 if SDO_AG is LOW

//#define PRINT_CALCULATED              //表示用の定義
//#define DEBUG_GYRO                    //ジャイロスコープの表示


#define RX 8                            //GPS用のソフトウェアシリアル
#define TX 9                            //GPS用のソフトウェアシリアル
#define SENTENCES_BUFLEN      128        // GPSのメッセージデータバッファの個数

#define UPDATE_INTERVAL        50       //モーションセンサーの値取得間隔
#define DATAPUSH_INTERVAL     200       //モーションセンサーの値記録間隔

//-------------------------------------------------------------------------
//[Global valiables]

LSM9DS1 imu;


//###############################################
//MicroSD 
//const int chipSelect = 4;//Arduino UNO
const int chipSelect = 10;//Arduino Micro

File dataFile;                          //SD CARD
boolean sdOpened = false;
//###############################################

const int tact_switch = 7;//タクトスイッチ
boolean switchIs;
boolean switchOn;
boolean switchRelease;


volatile boolean enableWrite = false;

///////////////////////カルマンフィルタ/////////////////////////////
String motionData;
volatile unsigned long time;

////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------
//=== Global for GPS ===========================================
SoftwareSerial  g_gps( RX, TX );
char head[] = "$GPRMC";
char info[] = "$GPGGA";
char buf[10];
int SentencesNum = 0;                   // GPSのセンテンス文字列個数
byte SentencesData[SENTENCES_BUFLEN] ;  // GPSのセンテンスデータバッファ
boolean isReaded;                       //GPSの読み込みが完了したかどうか
String gpsData;                         //GPSの読み込みが完了データ

//======================================================


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@
unsigned long delta;

unsigned long delta1;
unsigned long delta2;
void test(String s)
{
  Serial.print(millis() - delta);
  Serial.print("::DEBUG:");
  Serial.println(s);

  delta = millis();
  
}
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@


void setup(void) {

  // Open serial communications and wait for port to open:
  Serial.begin(9600);

  //=== SD Card Initialize ====================================
  Serial.print(F("Initializing SD card..."));
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println(F("Card failed, or not present"));
    // don't do anything more:
    return;
  }
  Serial.println(F("card initialized."));
  //=======================================================

  //===タクトスイッチ===========================================
  pinMode(tact_switch, INPUT);
  switchIs = false;
  //=======================================================

  //=== LSM9DS1 Initialize =====================================
  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress  = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;

  if (!imu.begin())              //センサ接続エラー時の表示
  {
    Serial.println(F("Failed to communicate with LSM9DS1."));
    while (1)
      ;
  }
  //=======================================================


  //=== GPS用のソフトウェアシリアル有効化 =================
  setupSoftwareSerial();
  //=======================================================


  //===MotionSensorのタイマー化 =================
  delay(100);
  motionData = "";
  //MsTimer2::set(100, updateMotionSensors);
  //MsTimer2::start();
  //=======================================================
  
  //DEBUG
  delta = millis();
  delta1 = millis();
  delta2 = millis();
}



/**
 * 　==================================================================================================================================================
 * loop
 * ずっと繰り返される関数（何秒周期？）
 * 【概要】
 * 　10msでセンサーデータをサンプリング。
 * 　記録用に、100ms単位でデータ化。
 * 　蓄積したデータをまとめて、1000ms単位でSDカードにデータを出力する。
 * 　==================================================================================================================================================
 */
void loop(void) {

  //START switch ============================================
  switch(digitalRead(tact_switch)){

   case 0://ボタンを押した
          switchOn = true;
          break;

   case 1://ボタン押していない
           if(switchOn){
             //すでにOnなら、falseにする
             if(switchIs)
               switchIs = false;
             //すでにOffなら、trueにする
             else
               switchIs = true;

             switchOn = false;
           }
           break;

    default:
           break;
  }

  //スイッチの判定
  if(!switchIs){ //falseなら、ループする
    digitalWrite(13, 0);
    if (sdOpened) {
      sdcardClose();
    }
    else{
      ; 
    }
    return;
  }
  else{
    digitalWrite(13, 1);
    if (sdOpened) {
      ;
    }
    else{
      sdcardOpen();
    }
  }
  //END switch ============================================


  //START MotionSensor ============================================

  if( millis() - delta1 > UPDATE_INTERVAL )
  {
    //Serial.print("DELTA1::");
    //Serial.println(millis() - delta1);
    updateMotionSensors();
    delta1 = millis();
  }

  if( millis() - delta2 > DATAPUSH_INTERVAL )
  {
    //Serial.print("DELTA2::");
    //Serial.println(millis() - delta2);
    pushMotionData();
    delta2 = millis(); 
  }
  
  //END MotionSensor ============================================


  //GPS MAIN ==========================================================
  char dt = 0 ;

    // センテンスデータが有るなら処理を行う
    if (g_gps.available()) {

        // 1バイト読み出す
        dt = g_gps.read() ;
        //Serial.write(dt);//Debug ALL

        // センテンスの開始
        if (dt == '$') SentencesNum = 0 ;
        
        if (SentencesNum >= 0) {
          
          // センテンスをバッファに溜める
          SentencesData[SentencesNum] = dt ;
          SentencesNum++ ;
             
          // センテンスの最後(LF=0x0Aで判断)
          if (dt == 0x0a || SentencesNum >= SENTENCES_BUFLEN) {

            SentencesData[SentencesNum] = '\0';

            //Serial.println((char *)SentencesData);
            //GPS情報の取得
            //getGpsInfo();

            //MotionSensorの値更新と保存
            //pushMotionData();

            // センテンスのステータスが"有効"になるまで待つ
            if ( gpsIsReady() )
            {
               // 有効になったら書込み開始

               gpsData = String((char *)SentencesData );
               
               // read three sensors and append to the string:
               //記録用のセンサー値を取得
               //updateMotionSensors();

               //SDカードへの出力
               writeDataToSdcard();

               return;
            }
          }
        }
      }
  //GPS MAIN ==========================================================
  
}


//==================================================================================================================================================


/**
 * sdcardOpen
 */
void sdcardOpen()
{


  // ファイル名決定
  String s;
  int fileNum = 0;
  char fileName[16];
  
  while(1){
    s = "LOG";
    if (fileNum < 10) {
      s += "00";
    } else if(fileNum < 100) {
      s += "0";
    }
    s += fileNum;
    s += ".TXT";
    s.toCharArray(fileName, 16);
    if(!SD.exists(fileName)) break;
    fileNum++;
  }


  dataFile = SD.open(fileName, FILE_WRITE);

  if(dataFile){
    Serial.println(fileName);
    sdOpened = true;
  }
  else
    Serial.println("fileError");

}

/**
 * sdcardClose
 */
void sdcardClose()
{
    dataFile.close();
    Serial.println(F("SD Closed."));
    sdOpened = false;

}


/**
 * writeDataToSdcard
 */
 
void writeDataToSdcard()
{

  //File dataFile = SD.open("datalog.txt", FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    
    dataFile.print(gpsData);
    dataFile.print(motionData);

    //dataFile.close();
    
    // print to the serial port too:
    Serial.print(gpsData);
    Serial.println(motionData);
    Serial.println(F("================================"));

    //クリア
    motionData = "";;
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println(F("error opening datalog.txt"));
  }

}


/**
 * updateMotionSensors
 * 
 */

void updateMotionSensors()
{
  imu.readGyro();
  imu.readAccel();
  imu.readMag();    
}


/**
 * pushMotionSensor
 * 
 *  * @enableWrite    GPS受信 "A"
 */
void pushMotionData()
{
  if(enableWrite){

      //時間の更新
      double dt = (double)(millis() - time); // Calculate delta time  

      if(dt < 100)
        return;

      //updateMotionSensors();
    
      
      time = millis();
    
      
      motionData += "$MOTION"; 
      motionData += ",";
    
      motionData += dt; 
      motionData += ",";

      //[g]
      motionData += imu.calcAccel(imu.ax);
      motionData += ",";
      motionData += imu.calcAccel(imu.ay);
      motionData += ",";
      motionData += imu.calcAccel(imu.az);
      motionData += ",";

      //[deg/s]
      motionData += imu.calcGyro(imu.gx);
      motionData += ",";
      motionData += imu.calcGyro(imu.gy);
      motionData += ",";
      motionData += imu.calcGyro(imu.gz);
      motionData += ",";

      //[gauss]
      motionData += imu.calcMag(imu.mx);
      motionData += ",";
      motionData += imu.calcMag(imu.my);
      motionData += ",";
      motionData += imu.calcMag(imu.mz);
    
    
      motionData += "\n";
  }

}

//===============================================
//
//      GPS系の処理
//
//===============================================

/**
 * setupSoftwareSerial
 * GPS用のソフトウェアシリアルの有効化
 */
void setupSoftwareSerial(){
  g_gps.begin(9600);
}


/**
 * getGpsInfo
 * $GPGGA　ヘッダから、衛星受信数や時刻情報を取得
 */
void getGpsInfo()
{
    int i, c;
    
    //$1ヘッダが一致
    if( strncmp((char *)SentencesData, info, 6) == 0 )
    {

      //コンマカウント初期化
      c = 1; 

      // センテンスの長さだけ繰り返す
      for (i=0 ; i<SentencesNum; i++) {
        if (SentencesData[i] == ','){
          
            c++ ; // 区切り文字を数える
    
            if ( c == 2 ) {
                 //Serial.println(F("----------------------------"));
                // Serial.println((char *)SentencesData);
                 Serial.print(F("Time:"));
                 Serial.println(readDataUntilComma(i+1));
                 continue;
            }
            else if ( c == 8 ) {
                // Serial.println((char *)SentencesData);
                 Serial.print(F("Number of Satelites:"));
                 Serial.println(readDataUntilComma(i+1));
                 continue;
            }
        }
      }
      
    }
}



/**
 * gpsIsReady
 * GPS情報が有効かどうかを判断
 * 項目3が"A"かどうかで判断
 */
boolean gpsIsReady()
{
    int i, c;
    
    //$1ヘッダが一致かつ,$3ステータスが有効＝A
    if( strncmp((char *)SentencesData, head, 6) == 0 )
    {

      //コンマカウント初期化
      c = 1; 

      // センテンスの長さだけ繰り返す
      for (i=0 ; i<SentencesNum; i++) {
        if (SentencesData[i] == ','){
              
              c++ ; // 区切り文字を数える
    
            if ( c == 3 ) {
                 //次のコンマまでのデータを呼び出し
                 if( strncmp("A", readDataUntilComma(i+1), 1) == 0 ){
                   //Serial.print("O:");
                   enableWrite = true;
                   return true;
                 }
                 else{
                   //Serial.print("X:");
                   //Serial.print( (char *)SentencesData );
                   enableWrite = false;
                   return false;
                 }
            }
        }
      }
      
    }

    return false;
}

/**
  * readDataUntilComma
  */
char* readDataUntilComma(int s)
{
  int i, j;

  j = 0;
  //初期化
  memset(buf,0x00,sizeof(buf)) ;

  //終了条件
  //次のコンマが出現or特定文字*（チェックサム)が出現
  for (i = s; i < SentencesNum; i++)
  {
    if(( SentencesData[i] == ',') || (SentencesData[i] == '*')){
      buf[j] = '\0';
      return buf;
    }
    else{
      //バッファーのオーバフローをチェック
      if( j < sizeof(buf) ) {
        buf[j] = SentencesData[i];
        j++;
      }
      else{//エラー処理
        int x;
        for(x = 0; x < sizeof(buf); x++)
          buf[x] = 'X';
          return buf;
      }
      
    }
  }
  
}



