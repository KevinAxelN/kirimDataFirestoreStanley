#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h> // Provide the token generation process info.
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <time.h>

#define ssid "Trojan Horse" //  ssid wifi  Mandala1.  Cursor_ID
#define ssidPassword "1sampai8" //  passwordwifi zollinger1   cursor12345*

#define apiKey "AIzaSyDReoy6uwXV2Z3kM76DEQaldj5y1Aruoag"  /* 2. Define the API Key  FIRESTORE STANLEY*/ 
#define projectId "fish-feeder-9341e"  /* 3. Define the project ID */

#define userEmail "fishfeederdeviceKMS@gmail.com"  // const char* email = "kevinaxelpipin@gmail.com";
#define emailPassword "FishfeederKMS11" // const char* emailPassword = "Skywalker";

//ULTRASONIC HC SR04 SENSOR
#define TRIGPIN 26
#define ECHOPIN 25

//  ===   DEVICE ID FOR FIRESTORE === //
String deviceId = "sGghkYSKOULVLNVLaIY8";

long duration;
int distance;

Servo myServo; 
int pos = 0;

FirebaseData fbdo;  // Define Firebase Data object

FirebaseJson payload;
FirebaseJsonData jsonData;

FirebaseAuth auth;
FirebaseConfig config;

bool taskCompleted = false;

unsigned long dataMillis = 0;
int count = 0;

String savedLastFeeding = "";
int savedAfterFeedVol = 0;
int savedBeforeFeedVol = 0;
bool isFoodTankOffline = false;

void updateDocumentUltrasonic(int beforeFeed, int afterFeed, String timeNow) {
  //  Firebase send data update 
  FirebaseJson content;
  String documentPath = "Devices/" + deviceId;
  Serial.println("Mencoba update a document... ");
  content.clear();
  content.set("fields/beforeFeedVol/integerValue", beforeFeed);
  content.set("fields/afterFeedVol/integerValue", afterFeed);
  content.set("fields/lastFeedTime/stringValue", timeNow);
  if (Firebase.Firestore.patchDocument(&fbdo, projectId, "" /* databaseId can be (default) or empty */, documentPath.c_str(), content.raw(), "beforeFeedVol, afterFeedVol, lastFeedTime" /* updateMask */)) {
    isFoodTankOffline = false;
    Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
  }
  else {
    isFoodTankOffline = true;
    Serial.println(fbdo.errorReason());

    savedLastFeeding = timeNow;
    savedAfterFeedVol = afterFeed;
    savedBeforeFeedVol = beforeFeed;
    Serial.println(savedLastFeeding + "(DHHSS) After: " + savedAfterFeedVol + " Before: " + savedBeforeFeedVol);
  }
}

String getLocalTime() {
  time_t now;
  struct tm timeinfo;
  char strftime_buf[64];
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "Failed to obtain time";
  }
  time(&now);

  int day = timeinfo.tm_wday == 0 ? 7 : timeinfo.tm_wday;

  localtime_r(&now, &timeinfo);
  // strftime(strftime_buf, sizeof(strftime_buf), "%A, %d %B %Y at %H:%M", &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%H%M", &timeinfo);
  String timeStr = strftime_buf;
  String result = String(day) + String(timeStr);
  return result;
}

//Ultrasonic cek distance
int ultrasonic() {
  digitalWrite(TRIGPIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGPIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGPIN, LOW);

  // pinMode(ECHOPIN, INPUT);
  duration = pulseIn(ECHOPIN, HIGH);
  int maximumDuration = 726; // durasi maksimum dianggap 0%
  int minimumDuration = 141; // durasi minimum dianggap 100%
  if (duration > maximumDuration) {
    duration = maximumDuration;
  }
  int percentage = map(duration, maximumDuration, minimumDuration, 0, 100);
  if (duration == 0) {
    return 0;
  } else {
    return percentage;
  }
}

void checkTimeAndUltrasonic(String currentTime, String values[], int arraySize, int beforeFeed) {
  for (int i = 0; i < arraySize; i++) {
    // Serial.print("ULTrasonic Arr : ");
    // Serial.print(i);
    // Serial.print(": ");
    // Serial.println(values[i]);
    String currentValue = values[i];
    String extractedTime = currentValue.substring(0, 4);  // Mengambil empat digit pertama sebagai waktu
    // Serial.print("currTime: ");
    // Serial.println(extractedTime);
    if (currentTime.equals(extractedTime)) {

      int afterFeed = ultrasonic();

      String timeNow = getLocalTime();

      Serial.print("Ultrasonic: ");
      Serial.println(afterFeed);
      Serial.print("Time Ultrasonic: ");
      Serial.println(timeNow);
      
      //  Firebase send data update 
      updateDocumentUltrasonic(beforeFeed, afterFeed, timeNow);

      break;
    }
  }
}

//servo muter
void servo(int putaran) {
  Serial.print("Putaran : ");
  Serial.println(putaran);
  for (int i = 0; i < putaran; i++) {
    for (pos = 0; pos <= 180; pos += 1) { 
      myServo.write(pos);             
      delay(5);                       
    }
    for (pos = 180; pos >= 0; pos -= 1) { 
      myServo.write(pos);             
      delay(5);                      
    }
  }
}

void checkTimeAndRotateServo(String currentTime, String values[], int arraySize) {
  for (int i = 0; i < arraySize; i++) {
    // Serial.print("time array: ");
    // Serial.print(i);
    // Serial.print(": ");
    // Serial.println(values[i]);
    String currentValue = values[i];
    String extractedTime = currentValue.substring(0, 4);  // Mengambil empat digit pertama sebagai waktu
    String extractedRotation = currentValue.substring(4); // Mengambil digit terakhir sebagai rotasi
    // Serial.print("currTime: ");
    // Serial.println(extractedTime);
    // Serial.print("rotation: ");
    // Serial.println(extractedRotation);
  
    if (currentTime.equals(extractedTime)) {
        String rotation = extractedRotation;
        // Serial.print("currentTime PUTAR: ");
        // Serial.println(currentTime);
        // Serial.print("rotation PUTAR: ");
        // Serial.println(rotation);
        servo(rotation.toInt()); // Memutar servo berdasarkan nilai yang ditem
        break;
    }
  }
}

//untuk ambil isi array
void parseJsonAndExtractValues(const char* json, String day, String*& values, int& size) {
  // Parse JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Accessing the array and retrieving string values
  JsonArray array = doc["fields"][day]["arrayValue"]["values"];
  size = array.size();
  values = new String[size]; // Membuat array string dengan ukuran yang sesuai
  int index = 0;
  for (JsonVariant v : array) {
    if (v["stringValue"]) {
      String valueString = v["stringValue"].as<String>();
      values[index] = valueString; // Menyimpan nilai dalam array
      index++;
    }
  }
}

// The Firestore payload upload callback function
void fcsUploadCallback(CFS_UploadStatusInfo info) {
    if (info.status == fb_esp_cfs_upload_status_init)
    {
        Serial.printf("\nUploading data (%d)...\n", info.size);
    }
    else if (info.status == fb_esp_cfs_upload_status_upload)
    {
        Serial.printf("Uploaded %d%s\n", (int)info.progress, "%");
    }
    else if (info.status == fb_esp_cfs_upload_status_complete)
    {
        Serial.println("Upload completed ");
    }
    else if (info.status == fb_esp_cfs_upload_status_process_response)
    {
        Serial.print("Processing the response... ");
    }
    else if (info.status == fb_esp_cfs_upload_status_error)
    {
        Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
    }
}

void printLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

String getClock() {
  time_t now;
  struct tm timeinfo;
  char strftime_buf[64];
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "0000";
  }
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%H%M", &timeinfo);
  return String(strftime_buf);
}

String getToday() {
  time_t now;
  struct tm timeinfo;
  char strftime_buf[64];
    if(!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
      String a = "Failed";
      return a;
    }
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%A", &timeinfo);
  String a = strftime_buf;
  return a;
}


// =================== SETUP ====================

void setup() {
  //servo attach pin 12
  myServo.attach(14);
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);

  Serial.begin(115200);
  WiFi.begin(ssid, ssidPassword);
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print('-');
  }
  
  Serial.println("");
  Serial.println("Project connected");
  Serial.println(WiFi.localIP());
  Serial.println("");

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  config.api_key = apiKey;
  auth.user.email = userEmail;
  auth.user.password = emailPassword;
  config.token_status_callback = tokenStatusCallback;

   // Limit the size of response payload to be collected in FirebaseData
    fbdo.setResponseSize(2048);
    // Firebase.begin(&config, &auth);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    // For sending payload callback
    config.cfs.upload_callback = fcsUploadCallback;

  //   untuk timestamp today()  
  configTime(7 * 3600, 0, "pool.ntp.org");
  // configTime(19 * 3600, 0, "pool.ntp.org");
  // configTime(67 * 3600, 0, "pool.ntp.org");
}


// ==================== LOOP  ==========================
String* values = nullptr; // Deklarasikan values sebagai variabel global di luar loop
int savedSize = 0; // Ukuran values yang disimpan sebelumnya

void loop() {
  String jam = getClock();
  while (!Firebase.ready()) {
    Firebase.ready();
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Dapatkan data dari Firebase jika terhubung ke internet
    
    //get a document dan print salah satu masknya setiap 60 detik skali...
    if (Firebase.ready() && (millis() - dataMillis > 60000 || dataMillis == 0))
    {
      String today = "";
      today = getToday();
      dataMillis = millis();
      Serial.print("HARI INII: ");
      Serial.println(today);

      String documentPath = "Schedules/" + deviceId;
      String mask = today;

      Serial.print("Mencoba Get a document... ");
    
      if (Firebase.Firestore.getDocument(&fbdo, projectId, "", documentPath.c_str(), mask.c_str())) {
        
        const char* a = fbdo.payload().c_str();
        
        Serial.println("Get a document... ok");
        Serial.println(a); // Cetak isi variabel a tanpa tambahan karakter
        Serial.print("AUTH ID BANG :   "); // Cetak isi variabel a tanpa tambahan karakter
          if (auth.user.id.length() > 0) {
            String uid = auth.user.id;
            Serial.println(uid);
          } else {
            Serial.println("Pengguna belum terotentikasi");
          }

        
        printLocalTime();
        

        String* newValues;
        int newSize;
        parseJsonAndExtractValues(a, today, newValues, newSize);

        Serial.println(jam);
        // checkTimeAndRotateServo(jam, values, size);

        delete[] values; // Hapus values yang disimpan sebelumnya
        values = newValues; // Simpan values baru
        savedSize = newSize; // Simpan ukuran values baru

        Serial.println("Wifi NYALA");

        if (isFoodTankOffline) {
          //  Firebase send data update 
          Serial.println("Food tank ofline TRUE");
          updateDocumentUltrasonic(savedBeforeFeedVol, savedAfterFeedVol, savedLastFeeding);
        }
        Serial.println("WIFI TELAH NYALA : " + savedLastFeeding);

        for (int i = 0; i < savedSize; i++) {
          Serial.print("Nilai DARI IF");
          Serial.print(i);
          Serial.print(": ");
          Serial.println(values[i]);
        }

        // HRS CEK TIME
        int beforeFeed = ultrasonic();
        checkTimeAndRotateServo(jam, values, savedSize);
        checkTimeAndUltrasonic(jam, values, savedSize, beforeFeed);
      } else {
        Serial.println(fbdo.errorReason());

        Serial.println("Wifi PUTUS");
        Serial.println(jam);
        for (int i = 0; i < savedSize; i++) {
          Serial.print("Nilai DARI ELSE");
          Serial.print(i);
          Serial.print(": ");
          Serial.println(values[i]);
        }
        
        int beforeFeed = ultrasonic();
        checkTimeAndRotateServo(jam, values, savedSize);
        checkTimeAndUltrasonic(jam, values, savedSize, beforeFeed);
      }
    }
  }
}


