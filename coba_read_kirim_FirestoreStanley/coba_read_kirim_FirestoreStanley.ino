#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h> // Provide the token generation process info.
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <time.h>
#include <Preferences.h>

// Firestore
#define apiKey "AIzaSyDReoy6uwXV2Z3kM76DEQaldj5y1Aruoag" 
#define projectId "fish-feeder-9341e"
#define userEmail "fishfeederdeviceKMS@gmail.com";
#define emailPassword "FishfeederKMS11" 

//ULTRASONIC HC SR04 SENSOR
#define TRIGPIN 26
#define ECHOPIN 25
#define SERVOPIN 14
#define BUTTON 4
#define LED 2

Preferences preferences;  //  preferences flash memory  //

//  fish feeder device wifi  //
const char* ssid = "FishFeeder";     
const char* password = "12345678"; 
const int serverPort = 80;       
String deviceId = "sGghkYSKOULVLNVLaIY8";   //    DEVICE ID   //
WiFiServer server(serverPort);
const char* CONNECTED_MSG = "New client connected";
const char* RECEIVED_MSG = "Received from client: ";
String devUID = "0"; // Initialize with a default value

//  for servo
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

//   saved feeding when offline
String savedLastFeeding = "";
int savedAfterFeedVol = 0;
bool isFoodTankOffline = false;

//  send UID when set wifi and password for first time
void updateDocumentUID(String UID) {
  FirebaseJson content;
  String documentPath = "Devices/" + deviceId;
  Serial.println("Mencoba update UID... ");
  content.clear();
  content.set("fields/ownerUID/stringValue", UID);
  if (Firebase.Firestore.patchDocument(&fbdo, projectId, "" /* databaseId can be (default) or empty */, documentPath.c_str(), content.raw(), "ownerUID" /* updateMask */)) {
    Serial.printf("BERHASIL\n%s\n\n", fbdo.payload().c_str());
  }
  else {
    Serial.println(fbdo.errorReason());
  }
}

unsigned long cooldownStartTime = 0;
const unsigned long cooldownDuration = 5000; // Durasi

void onLED() {
  unsigned long currentTime = millis();

  if (currentTime - cooldownStartTime >= cooldownDuration) {
    for (int i=0; i<5; i++) {
      digitalWrite(LED, HIGH);  
      delay(100);               
      digitalWrite(LED, LOW);  
      delay(100); 
    }  
    cooldownStartTime = currentTime;  // Reset waktu cooldown
  }
}

unsigned long lastConnectionTime = 0; // variabel untuk menyimpan waktu terakhir client terhubung
const unsigned long connectionDelay = 5000;

void WifiClientServer() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println(CONNECTED_MSG);
    lastConnectionTime = millis();
    digitalWrite(LED, HIGH);

    while (client.connected()) {
      if (client.available()) {
        String request = client.readStringUntil('\r');
        Serial.println(RECEIVED_MSG + request);

        // Process the request...
        int firstDelimiter = request.indexOf('#');
        int secondDelimiter = request.indexOf('#', firstDelimiter + 1);

        if (firstDelimiter != -1 && secondDelimiter != -1) {
          String newWifiSSID = request.substring(0, firstDelimiter);
          String newWifiPass = request.substring(firstDelimiter + 1, secondDelimiter);
          devUID = request.substring(secondDelimiter + 1);

          Serial.println("newWifiSSID: " + newWifiSSID);
          Serial.println("newWifiPass: " + newWifiPass);
          Serial.println("UID: " + devUID);

          //  FLASH MEMORY SAVE
          preferences.begin("my-app", false);
          preferences.clear();
          // Menyimpan nama jaringan WiFi dan kata sandinya
          String newSSID = newWifiSSID;
          String newPassword = newWifiPass;
          preferences.putString("wifi_ssid", newSSID.c_str());
          preferences.putString("wifi_password", newPassword.c_str());
          Serial.println("BERHASIL SAVE FLASH MEMORY");
          preferences.end();

          if (client.println(deviceId) > 0) {
            Serial.println("Sent response to client: " + deviceId);
            delay(5000);

            // Disconnect from SoftAP
            WiFi.softAPdisconnect(true);

            // Connect to the new Wi-Fi network
            WiFi.begin(newWifiSSID.c_str(), newWifiPass.c_str());
            Serial.print(".");

            // Wait for connection
            int connectionAttempts = 0;
            while (WiFi.status() != WL_CONNECTED && connectionAttempts < 5) {
              delay(1000);
              Serial.print(".");
              connectionAttempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
              // Successfully connected to the new Wi-Fi network
              Serial.println("Connected to new WiFi network");
              updateDocumentUID(devUID);
            } else {
              // Failed to connect to the new Wi-Fi network
              // Serial.println("Failed to connect to the new WiFi network");
              //  LED ON  //
              onLED();
            }
          } else {
            Serial.println("Failed to send response to client");
          }     
        } else {
          Serial.println("Invalid request format");
        }
      }
      unsigned long elapsedTime = millis() - lastConnectionTime;
    }
    client.stop();
    Serial.println("Client disconnected");
    digitalWrite(LED, LOW);
  }
}

//butuh nambah hari
void updateDocumentUltrasonic(int afterFeed, String timeNow) {
  //  Firebase send data update 
  FirebaseJson content;
  String documentPath = "Devices/" + deviceId;
  Serial.println("Mencoba update a document... ");
  content.clear();
  content.set("fields/afterFeedVol/integerValue", afterFeed);
  content.set("fields/lastFeedTime/stringValue", timeNow);
  if (Firebase.Firestore.patchDocument(&fbdo, projectId, "" /* databaseId can be (default) or empty */, documentPath.c_str(), content.raw(), "afterFeedVol, lastFeedTime" /* updateMask */)) {
    isFoodTankOffline = false;
    Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
  }
  else {
    isFoodTankOffline = true;
    Serial.println(fbdo.errorReason());

    savedLastFeeding = timeNow;
    savedAfterFeedVol = afterFeed;
    Serial.println(savedLastFeeding + "(DHHSS) After: " + savedAfterFeedVol);
  }
}

//HAPUS kalo bisa pake getTime
// add moth dan tanggal
String getLocalTime() {
  time_t now;
  struct tm timeinfo;
  char strftime_buf[64];

  delay(1000);

  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "Failed to obtain time";
  }
  time(&now);

  int day = timeinfo.tm_wday == 0 ? 7 : timeinfo.tm_wday;

  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%H%M", &timeinfo);  //  "%A, %d %B %Y at %H:%M"
  String timeStr = strftime_buf;
  String result = String(day) + String(timeStr);
  return result;
}

String getTime() {
  time_t now;
  struct tm timeinfo;
  char strftime_buf[64];

  delay(1000);

  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "Failed to obtain time";
  }
  time(&now);

  int day = timeinfo.tm_wday == 0 ? 7 : timeinfo.tm_wday;

  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%m%d%H%M", &timeinfo);
  
  String timeStr = strftime_buf;
  return timeStr;
}

bool ultrasonicRetry = false;
String timeRetry = "";

int ultrasonic() {
  int totalDuration = 0;
  int numberOfReadings = 10;
  int readings[numberOfReadings];

  for (int i = 0; i < numberOfReadings; i++) {
    delay(250);
    digitalWrite(TRIGPIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGPIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGPIN, LOW);
    duration = pulseIn(ECHOPIN, HIGH);
    digitalWrite(ECHOPIN, LOW);
    delayMicroseconds(2);  // 
    Serial.println(duration);
    readings[i] = duration;
    totalDuration += readings[i];
  }

  // Hitung rata-rata
  int averageDuration = totalDuration / numberOfReadings;
  Serial.print("Durasi (Sebelum Penyaringan): ");
  Serial.print(averageDuration);
  Serial.println(" us");

  // Singkirkan nilai yang ekstrem
  for (int i = 0; i < numberOfReadings; i++) {
    if (abs(readings[i] - averageDuration) > 500) {  // Sesuaikan ambang batas penyaringan sesuai kebutuhan
      Serial.print("dibuang: ");
      Serial.println(readings[i]);
      totalDuration -= readings[i];
      numberOfReadings--; // Kurangi jumlah pembacaan yang digunakan untuk menghitung rata-rata
    }
  }


  // Hitung rata-rata setelah penyaringan
  averageDuration = totalDuration / numberOfReadings;
  Serial.print("Durasi (Setelah Penyaringan): ");
  Serial.print(averageDuration);
  Serial.println(" us");

  // Hitung jarak hanya pada nilai duration terakhir
  distance = averageDuration * 0.34 / 2;
  Serial.print("Jarak: ");
  Serial.print(distance);
  Serial.println(" mm");
  
  if (distance > 240 || duration > 1370) {
    Serial.println("ULTRA RETRY TRUE");
    ultrasonicRetry = true;
  } else {
    Serial.println("ULTRA RETRY FALSE");
    ultrasonicRetry = false;
    // timeRetry = "";
  }


  int tankPercentage;

  // Hitung persentase berdasarkan jarak relatif terhadap range 30mm - 190mm
  if (distance < 30) {
    tankPercentage = 100;  // Jarak kurang dari 20mm, tangki penuh
  } else if (distance > 230) {
    tankPercentage = 0;    // Jarak lebih dari 200mm, tangki kosong
  } else {
    // Hitung persentase berdasarkan jarak relatif terhadap range 20mm - 200mm
    tankPercentage = map(distance, 30, 230, 100, 0);
  }

  Serial.print("Persentase Tangki: ");
  Serial.print(tankPercentage);
  Serial.println("%");
  // ===========================================================
  Serial.println(" ");
  return tankPercentage;
}

void checkTimeAndUltrasonic(String currentTime, String values[], int arraySize) {
  for (int i = 0; i < arraySize; i++) {
    String currentValue = values[i];
    String extractedTime = currentValue.substring(0, 4);  // Mengambil empat digit pertama sebagai waktu

    if (currentTime.equals(extractedTime)) {
      delay(100);
      int afterFeed = ultrasonic();
      String timeNow = getTime();

      Serial.print("Ultrasonic: ");
      Serial.println(afterFeed);
      Serial.print("Time Ultrasonic: ");
      Serial.println(timeNow);

      if (!ultrasonicRetry) {
        Serial.println("cektime RETRY FALSE");
        //  Firebase send data update jika tidak retry (berarti berhasil read) 
        updateDocumentUltrasonic(afterFeed, timeNow);
      } else {
        Serial.println("cektime RETRY TRUE");
        timeRetry = timeNow;
      }

      break;
    }
  }
}

//  memutar servo
void servo(int putaran) {
  Serial.print("Putaran : ");
  Serial.println(putaran);
  for (int i = 0; i < putaran; i++) {
    myServo.write(10);             
    delay(300);                       
    myServo.write(0); 
    delay(300);                
  }
}

void checkTimeAndRotateServo(String currentTime, String values[], int arraySize) {
  for (int i = 0; i < arraySize; i++) {
    String currentValue = values[i];
    String extractedTime = currentValue.substring(0, 4);  // Mengambil empat digit pertama sebagai waktu
    String extractedRotation = currentValue.substring(4); // Mengambil digit terakhir sebagai rotasi
  
    if (currentTime.equals(extractedTime)) {
        String rotation = extractedRotation;
        servo(rotation.toInt()); // Memutar servo berdasarkan nilai yang ditem
        break;
    }
  }
}

//untuk ambil isi array
void parseJsonAndExtractValues(const char* json, String day, String*& values, int& size) {
  // Parse JSON
  DynamicJsonDocument doc(2048);
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

// #include <ArduinoJson.h>
// #include <vector>

// void parseJsonAndExtractValues(const char* json, String day, std::vector<String>& values) {
//   // Parse JSON
//   DynamicJsonDocument doc(1024);
//   DeserializationError error = deserializeJson(doc, json);
//   if (error) {
//     Serial.print("deserializeJson() failed: ");
//     Serial.println(error.c_str());
//     return;
//   }

//   // Accessing the array and retrieving string values
//   if (doc.containsKey("fields") && doc["fields"].containsKey(day) && doc["fields"][day].containsKey("arrayValue") && doc["fields"][day]["arrayValue"].containsKey("values")) {
//     JsonArray array = doc["fields"][day]["arrayValue"]["values"];
//     for (JsonVariant v : array) {
//       if (v.containsKey("stringValue")) {
//         String valueString = v["stringValue"].as<String>();
//         values.push_back(valueString); // Menambahkan nilai ke vektor
//       }
//     }
//   }
// }

// String jsonExample = "..."; // Gantilah dengan data JSON yang sesuai
// std::vector<String> extractedValues;
// parseJsonAndExtractValues(jsonExample.c_str(), "Wednesday", extractedValues);

// // Cetak nilai yang diekstrak
// for (const String& value : extractedValues) {
//   Serial.println(value);
// }

//  ===================================================================================================

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

  delay(1000);

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

  delay(1000);

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

void checkSavedWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    preferences.begin("my-app", false);
    // Membaca nilai yang tersimpan dengan kunci "wifi_ssid" dan "wifi_password" kalau ga ada jadi nilai default

    if (preferences.isKey("wifi_ssid") && preferences.isKey("wifi_password")) {
      String savedSSID = preferences.getString("wifi_ssid", "");
      String savedPassword = preferences.getString("wifi_password", "");
      preferences.end();
      if (savedSSID.length() > 0 && savedPassword.length() > 0) {
        Serial.println("SSID dan password sudah tersimpan:");
        Serial.println("SSID: " + savedSSID);
        Serial.println("Password: " + savedPassword);

        WiFi.begin(savedSSID, savedPassword);
        for (int i = 0; i < 5; i++) {
          if (WiFi.status() != WL_CONNECTED) {
            delay(1000);
            Serial.print(".");
          }
        }
      }
  
    } else {
      // Serial.println("Belum ada SSID dan password yang tersimpan atau data kosong.");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      // Serial.println("Failed to connect wifi");
      //  LED ON  //
      onLED();
    }
  }
}

void isButtonClicked() {
  int buttonState = digitalRead(BUTTON);
  if (!buttonState) {
    //LED on ketika clear memory
    digitalWrite(LED, HIGH);

    WiFi.disconnect();
    Serial.println("clearing preferences flash memory");
    preferences.begin("my-app", false);
    preferences.clear();
    Serial.println("BERHASIL CLEAR FLASH MEMORY");
    preferences.end();

    //  ESP SBG WIFI BUAT START 
    WiFi.softAP(ssid, password);
    Serial.println();
    Serial.print("ESP32 IP address: ");
    Serial.println(WiFi.softAPIP());
    server.begin();
    Serial.println("Server started");

    //LED off ketika selesai clear
    digitalWrite(LED, LOW);
  } 
}

// =================== SETUP ====================

void setup() {
  //LED
  pinMode(LED, OUTPUT);

  // PUSH button
  pinMode(BUTTON, INPUT_PULLUP);

  //servo attach pin 14
  myServo.attach(SERVOPIN); 
  myServo.write(pos); 
  
  // ULTRASONOIC
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);

  Serial.begin(115200);
  
  // WiFi.begin(ssid, ssidPassword);
  // while (WiFi.status() != WL_CONNECTED){
  //   delay(500);
  //   Serial.print('-');
  // }
  
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
}

// ==================== LOOP  ==========================
String* values = nullptr; // Deklarasikan values sebagai variabel global di luar loop
int savedSize = 0; // Ukuran values yang disimpan sebelumnya

// Untuk ultrasonic retry
unsigned long previousMillis = 0;  // Variable untuk menyimpan waktu terakhir saat masuk ke dalam if statement
const int interval = 10000; 

void loop() {
  //Jika wifi ga konek
  checkSavedWifi();

  //  jika buttonstate di click
  isButtonClicked();

  //  WIFI CLIENT SERVER START
  WifiClientServer();

  if (WiFi.status() == WL_CONNECTED) {
    //cek jika ultrasonic sbelumnya gagal read
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      while (!Firebase.ready()) {
        Firebase.ready();
        Serial.print(".");
      }
      if (Firebase.ready()) {
        if (ultrasonicRetry) {
          String today = "";
          today = getToday();
          String documentPath = "Schedules/" + deviceId;
          String mask = today;

          if (Firebase.Firestore.getDocument(&fbdo, projectId, "", documentPath.c_str(), mask.c_str())) {
            const char* c = fbdo.payload().c_str();
            Serial.println("RETRY Get a document... ok");

            String* retryValues;
            int retrySize;
            parseJsonAndExtractValues(c, today, retryValues, retrySize);

            digitalWrite(LED, HIGH);  
            delay(500);               
            digitalWrite(LED, LOW);  
            delay(100); 

              myServo.write(3);             
              delay(300);                       
              myServo.write(0); 
              delay(300);   

            Serial.println("COBA ULTRASONIC RETRY");
            int nilaiUltraRetry = ultrasonic();
            String waktuTimeRetry = timeRetry;
            if (!ultrasonicRetry) {
              Serial.println("RETRY BERHASIL");
              Serial.println("ultrasonic RETRY");
              // updateDocumentUltrasonic(nilaiUltraRetry, waktuTimeRetry);
              Serial.println(nilaiUltraRetry);
              Serial.println("waktu RETRY");
              Serial.println(waktuTimeRetry);
            }
         
          }
        }
      }

    }

    String jam = getClock();
    while (!Firebase.ready()) {
      Firebase.ready();
      Serial.print(".");
    }
    
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
          updateDocumentUltrasonic(savedAfterFeedVol, savedLastFeeding);
        }
        Serial.println("WIFI TELAH NYALA : " + savedLastFeeding);

        for (int i = 0; i < savedSize; i++) {
          Serial.print("Nilai DARI IF");
          Serial.print(i);
          Serial.print(": ");
          Serial.println(values[i]);
        }

        // HRS CEK TIME
        // int beforeFeed = ultrasonic();
        checkTimeAndRotateServo(jam, values, savedSize);
        checkTimeAndUltrasonic(jam, values, savedSize);
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
        
        // int beforeFeed = ultrasonic();
        checkTimeAndRotateServo(jam, values, savedSize);
        checkTimeAndUltrasonic(jam, values, savedSize);
      }
    }
  } else {
    //   JIKA WIFI DISCONNECT
    onLED();
  }
}


