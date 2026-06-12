/*
 * ĐỒ ÁN : HỆ THỐNG PHÁT HIỆN CHÁY & CẢNH BÁO QUA BLYNK
 * NHÓM 11
 */

#define BLYNK_TEMPLATE_ID "TMPL6HJOM9NWc"
#define BLYNK_TEMPLATE_NAME "Fire Alarm System"
#define BLYNK_AUTH_TOKEN "bxX6zpwfOQohgvU1xPvkabb7BoOLU1Ib"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <ESP32Servo.h>

// --- KHAI BÁO CHÂN ---
#define MQ2_PIN       34  
#define FLAME_PIN     35  
#define TRIG_PIN      27  
#define ECHO_PIN      13  
#define DHT_PIN       32  
#define SERVO_PIN     18  
#define RELAY_PIN     26  
#define BUZZER_PIN    33  
#define LED_PIN       14  
#define BTN_MUTE_PIN  25  

// --- CẤU HÌNH WIFI ---
char ssid[] = "***";
char pass[] = "***";

// --- KHỞI TẠO ĐỐI TƯỢNG ---
#define DHTTYPE DHT22     
DHT dht(DHT_PIN, DHTTYPE);
Servo windowServo;
BlynkTimer timer;

// --- BIẾN HỆ THỐNG & FSM ---
enum SystemState { NORMAL, ALARM_FIRE, ALARM_GAS, WARN_COOKING, PANIC };
SystemState currentState = NORMAL;

float temp = 0.0, hum = 0.0, lastTemp = 0.0;
int gasValue = 0;
int flameValue = LOW; 
const int GAS_OFFSET = 1550;
bool personDetected = false; 
bool isMuted = false;
bool isManualPumpOn = false; 
bool isManualWindowOn = false; 

// Biến tính toán tốc độ tăng nhiệt
unsigned long lastTempCheckTime = 0;
const float FIRE_TEMP_RATE = 5.0; 

// --- BIẾN ĐIỀU KHIỂN SERVO ---
bool isWindowOpen = false;
bool isWindowOpening = false;
bool isWindowClosing = false;
bool windowShouldBeOpen = false; 
unsigned long windowMoveStartTime = 0;

// --- HÀM KHỞI TẠO ---
void setup() {
  Serial.begin(115200);
  
  pinMode(FLAME_PIN, INPUT); 
  
  // Khởi tạo chân cho SRF05
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  pinMode(BTN_MUTE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); 
  
  windowServo.attach(SERVO_PIN);
  windowServo.write(90); 
  
  dht.begin();
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  timer.setInterval(2000L, readSensors);    
  timer.setInterval(1000L, evaluateLogic);  
  timer.setInterval(100L, checkMuteButton); 
  timer.setInterval(50L, handleActuators);  
  
  Serial.println("===============================");
  Serial.println("  HỆ THỐNG ĐÃ KHỞI ĐỘNG!  ");
  Serial.println("===============================");
}

void loop() {
  Blynk.run();
  timer.run();
}

// --- Đọc dữ liệu cảm biến ---
void readSensors() {
  temp = dht.readTemperature();
  hum = dht.readHumidity();
  int rawGas = analogRead(MQ2_PIN);
  gasValue = rawGas - GAS_OFFSET;
  if (gasValue < 0) {
    gasValue = 0;
  }
  flameValue = digitalRead(FLAME_PIN); 

  // Đọc cảm biến siêu âm SRF05
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Timeout 20000us (20ms) để ESP32 KHÔNG BỊ TREO
  long duration = pulseIn(ECHO_PIN, HIGH, 20000); 
  int distance = duration * 0.034 / 2; 
  
  // Check khoảng cách có người <=100cm
  if (duration > 0 && distance <= 100) {
    personDetected = true;
  } else {
    personDetected = false;
  }

  Serial.print("Nhiet do: "); Serial.print(temp);
  Serial.print(" | Do am: "); Serial.print(hum);
  Serial.print(" | Gas: "); Serial.print(gasValue);
  Serial.print(" | Lua: "); Serial.print(flameValue);
  Serial.print(" | Khoang cach: "); 
  if (duration == 0) Serial.println("Qua xa (Timeout)");
  else { Serial.print(distance); Serial.println(" cm"); }

  Blynk.virtualWrite(V0, temp);
  Blynk.virtualWrite(V1, hum);
  Blynk.virtualWrite(V2, gasValue);
  
  String stateStr = "BÌNH THƯỜNG";
  if(currentState == ALARM_FIRE) stateStr = "CHÁY KHẨN CẤP!";
  else if(currentState == ALARM_GAS) stateStr = "RÒ RỈ GAS!";
  else if(currentState == WARN_COOKING) stateStr = "CÓ KHÓI BẾP";
  Blynk.virtualWrite(V3, stateStr);
}

// --- Logic Máy trạng thái ---
void evaluateLogic() {
  float tempRate = 0;
  
  // Tính tốc độ tăng nhiệt
  if (millis() - lastTempCheckTime >= 10000) {
    if (lastTemp > 0.0) {
      tempRate = temp - lastTemp;
    }
    lastTemp = temp;
    lastTempCheckTime = millis();
  }

  // LOGIC 1: CHÁY THẬT 
  if (flameValue == HIGH || tempRate >= FIRE_TEMP_RATE || (temp > 55.0 && gasValue > 1450)) {
    if(currentState != ALARM_FIRE) {
      currentState = ALARM_FIRE;
      Serial.println("\n[!] >> TRANG THAI: CHAY KHAN CAP! BOM KICH HOAT! << [!]");
      Blynk.logEvent("fire_alert", "PHÁT HIỆN CHÁY! Bơm đang bật!");
    }
    windowShouldBeOpen = true; // mở cửa sổ
  }
  
  // NẾU PHÁT HIỆN KHÍ GAS/KHÓI BẤT THƯỜNG 
  else if (gasValue > 1450) {
    
    // NHÁNH A: CÓ NGƯỜI (personDetected == true)
    if (personDetected == true) {
      if(currentState != WARN_COOKING) {
        currentState = WARN_COOKING;
        Serial.println("\n[*] >> TRANG THAI: ĐANG NẤU ĂN << [*]");
        Blynk.logEvent("smoke_alert", "Phát hiện khói bếp.");
      }
      // Nấu ăn thì không cần báo động ầm ĩ, chỉ in cảnh báo
    } 
    
    // NHÁNH B: KHÔNG CÓ NGƯỜI (personDetected == false)
    else {
      if(currentState != ALARM_GAS) {
        currentState = ALARM_GAS;
        Serial.println("\n[!] >> TRANG THAI: RÒ RỈ GAS! << [!]");
        Blynk.logEvent("gas_alert", "RÒ RỈ GAS! Đã tự động mở thông gió.");
      }
      windowShouldBeOpen = true; // Mở cửa sổ thông gió để xả Gas
    }
    
  }
  
  // TRỞ VỀ TRẠNG THÁI BÌNH THƯỜNG 
  else { 
    if(currentState != NORMAL) {
      currentState = NORMAL;
      Serial.println("\n[v] >> TRANG THAI: BÌNH THƯỜNG << [v]");
      isMuted = false; 
    }
    if (isManualWindowOn == false) {
    windowShouldBeOpen = false; // Đóng cửa sổ
    }
  }
}

// --- Xử lý phần cứng đầu ra ---
void handleActuators() {
  
  if (windowShouldBeOpen) {
    if (!isWindowOpen && !isWindowOpening) {
      Serial.println("-> Bắt đầu mở cửa sổ...");
      isWindowOpening = true;
      isWindowClosing = false;
      windowMoveStartTime = millis(); 
      windowServo.write(0);           // Quay mở
    }
    else if (isWindowOpening && (millis() - windowMoveStartTime >= 2000)) {
      windowServo.write(90);          // Dừng
      isWindowOpening = false;
      isWindowOpen = true;
      Serial.println("-> Cửa sổ đã mở xong!");
    }
  } 
  else { 
    if ((isWindowOpen || isWindowOpening) && !isWindowClosing) {
      Serial.println("-> Bắt đầu đóng cửa sổ...");
      isWindowClosing = true;
      isWindowOpening = false;
      windowMoveStartTime = millis(); 
      windowServo.write(180);         // Quay đóng
    }
    else if (isWindowClosing && (millis() - windowMoveStartTime >= 2000)) {
      windowServo.write(90);          // Dừng
      isWindowClosing = false;
      isWindowOpen = false;
      Serial.println("-> Cửa sổ đã đóng xong!");
    }
  }

  if (currentState == ALARM_FIRE) {
    digitalWrite(RELAY_PIN, LOW);  
    if (!isMuted) {
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } 
  else if (currentState == ALARM_GAS) {
    digitalWrite(RELAY_PIN, HIGH); 
    if (!isMuted) {
      digitalWrite(BUZZER_PIN, (millis() % 1000 < 500) ? HIGH : LOW); 
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
  else {
    digitalWrite(BUZZER_PIN, LOW); 
    digitalWrite(LED_PIN, LOW);    
    if (!isManualPumpOn) {
      digitalWrite(RELAY_PIN, HIGH); 
    }
  }
}

// --- Nút nhấn tắt còi ---
void checkMuteButton() {
  if (digitalRead(BTN_MUTE_PIN) == LOW) {
    if (!isMuted && currentState != NORMAL) { 
      Serial.println("-> Đã tắt còi!");
    }
    isMuted = true;
  }
}

// --- Nút nhấn bơm thủ công ---
BLYNK_WRITE(V4) {
  int manualPump = param.asInt();

  if (currentState == ALARM_GAS) {
    isManualPumpOn = false;        
    digitalWrite(RELAY_PIN, HIGH); 
    if (manualPump == 1) {
      Blynk.virtualWrite(V4, 0); 
      Serial.println("HỆ THỐNG TỪ CHỐI: Đã khóa Relay để chống tia lửa điện do có Rò rỉ Gas!");
      Blynk.logEvent("system_locked", "Từ chối bật bơm: Khóa an toàn chống nổ đang kích hoạt!");
    }
    return; 
  }

  if (manualPump == 1) {
    isManualPumpOn = true;         
    digitalWrite(RELAY_PIN, LOW);  
    Serial.println("Đã BẬT máy bơm thủ công qua Blynk.");
  } else {
    isManualPumpOn = false;        
    digitalWrite(RELAY_PIN, HIGH); 
    Serial.println("Đã TẮT máy bơm thủ công qua Blynk.");
  }
}


// ---Nút nhấn cửa sổ thủ công---
BLYNK_WRITE(V5) {
  int manualWindow = param.asInt();

// Khi đang có cảnh báo
  if ((currentState == ALARM_FIRE || currentState == ALARM_GAS || currentState == PANIC) && manualWindow == 0) {
    Serial.println("Đang báo động khẩn cấp! Khóa chức năng đóng cửa.");
    Blynk.virtualWrite(V5, 1); 
    return;
  }

  // Khi an toàn
  if (manualWindow == 1) {
    isManualWindowOn = true;  
    windowShouldBeOpen = true;  
    Serial.println("Đã mở cửa sổ thủ công qua Blynk.");
  } else {
    isManualWindowOn = false; 
    windowShouldBeOpen = false; 
    Serial.println("Đã đóng cửa sổ thủ công qua Blynk.");
  }
}