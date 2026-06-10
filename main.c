#include <Arduino.h>

#include <Wire.h>

#include <Adafruit_ADS1X15.h>

#include <U8g2lib.h>

#include <math.h>



Adafruit_ADS1115 ads;

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);



// =====================================================

// ĐỊNH NGHĨA CHÂN NGOẠI VI & CẢM BIẾN

// =====================================================

#define OPTO_CHARGE      18  

#define OPTO_DISCHARGE   5    

#define FAN_PIN          32  



// 3 nút bấm vật lý

#define BTN_CHARGE       33   // Nút kích hoạt Sạc

#define BTN_DISCHARGE    39   // Nút kích hoạt Xả

#define BTN_LOCK         36   // Nút Khóa hệ thống khẩn cấp



#define NTC1_PIN        34

#define NTC2_PIN        35



const int balPin[3] = {26, 27, 25};



// =====================================================

// CẤU HÌNH NGƯỠNG BẢO VỆ AN TOÀN

// =====================================================

const float CELL_MAX  = 4.20;

const float CELL_MIN  = 2.60;

const float PACK_MAX  = 12.6;

const float CURR_MAX  = 2.60;

const float TEMP_MAX  = 45.0;

const float TEMP_FAN_ON = 35.0;



const float BAL_START = 3.80;

const float BAL_DIFF  = 0.03;



// =====================================================

// BIẾN TOÀN CỤC KHỞI TẠO TRẠNG THÁI

// =====================================================

float cell[3] = {0, 0, 0};

float packV = 0;

float currentA = 0;

float temp1 = 0, temp2 = 0;

int soc = 0;



float CAPACITY_mAh = 6000.0;

float remain_mAh = 6000.0;

unsigned long tAh = 0;



// YÊU CẦU: Mặc định ban đầu hệ thống ở chế độ Khóa (bmsMode = 3)

int bmsMode = 3;



// YÊU CẦU: Hệ thống lúc bắt đầu luôn tắt 2 FET này không cho sạc và xả

int chargeState = LOW;

int dischargeState = LOW;



bool protectionActive = false;

String protectionMsg = "";



// Biến điều khiển hiển thị ngắt ưu tiên khi thao tác nút bấm

bool forceShowStatus = true; // Bật true để lúc khởi động hiển thị ngay chữ SYSTEM LOCKED

unsigned long tBtnPressed = 0;

String btnStatusMsg = "SYS: INIT LOCKED";



byte page = 0;

unsigned long tRead = 0;

unsigned long tOLED = 0;

unsigned long tAutoPage = 0;



// Biến trạng thái chống rung phím nhấn

bool lastBtnChgState = HIGH;

bool lastBtnDsgState = HIGH;

bool lastBtnLockState = HIGH;



// =====================================================

// TIẾN TRÌNH QUÉT ĐỌC DỮ LIỆU PHẦN CỨNG

// =====================================================

float readADS(int ch) {

  long sum = 0;

  for(int i = 0; i < 10; i++) { sum += ads.readADC_SingleEnded(ch); delay(2); }

  return (sum / 10.0) * 0.1875 / 1000.0;

}



void readCells() {

  // Đọc điện áp rơi trên trở Shunt trước ở chân A3

  float v = readADS(3);



  // Hệ số nhân phân áp 6.3, 1.4, 2.0, 1.2, 3.0 từ logic gốc của bạn

  float a0 = (readADS(0) - (1.4 * v)) * 2.0;

  float a1 = (readADS(1) - (1.4 * v)) * 2.0;

  float a2 = (readADS(2) - (1.4 * v)) * 3.0;



  // Logic trừ tầng để ra áp từng cell độc lập

  cell[0] = a0;

  cell[1] = (a1 - a0);

  cell[2] = (a2 - a1);



  // Khống chế lỗi giá trị âm

  for(int i = 0; i < 3; i++) if(cell[i] < 0) cell[i] = 0;



  // Tổng điện áp Pack dâng theo cell cuối cùng như logic trước

  packV = a2;

}

void readCurrent() {

  float vShunt = readADS(3);

  currentA = fabs(vShunt / 0.1);

  if(currentA < 0.07) currentA = 0;

}



int readADCavg(int pin) {

  long sum = 0;

  for(int i = 0; i < 20; i++) { sum += analogRead(pin); delay(2); }

  return sum / 20;

}



float ntcToTemp(float Rntc) {

  if (Rntc <= 0) return 25.0;

  return (1.0 / (log(Rntc / 10000.0) / 3950.0 + (1.0 / 298.15))) - 273.15;

}



void readTemp() {

  static float old1 = 25.0, old2 = 25.0;

  int adc1 = readADCavg(NTC1_PIN);

  float v1 = constrain(adc1 * 3.3 / 4095.0, 0.03, 3.27);

  temp1 = ntcToTemp((v1 * 10000.0) / (3.3 - v1));



  int adc2 = readADCavg(NTC2_PIN);

  float v2 = constrain(adc2 * 3.3 / 4095.0, 0.03, 3.27);

  temp2 = ntcToTemp((v2 * 10000.0) / (3.3 - v2));



  temp1 = old1 * 0.7 + temp1 * 0.3; temp2 = old2 * 0.7 + temp2 * 0.3;

  old1 = temp1; old2 = temp2;



  if(temp1 >= TEMP_FAN_ON || temp2 >= TEMP_FAN_ON) digitalWrite(FAN_PIN, HIGH);

  else if(temp1 < (TEMP_FAN_ON - 3.0) && temp2 < (TEMP_FAN_ON - 3.0)) digitalWrite(FAN_PIN, LOW);  

}



void capacityTask() {

  float absI = fabs(currentA);

  if(absI > 0.05) {

    unsigned long now = millis();

    if(tAh == 0) { tAh = now; return; }

    float dtHour = (now - tAh) / 3600000.0; tAh = now;

    float delta_mAh = absI * 1000.0 * dtHour;

    if(dischargeState == HIGH && chargeState == LOW) remain_mAh -= delta_mAh;

    else if(chargeState == HIGH && dischargeState == LOW) remain_mAh += delta_mAh;

    remain_mAh = constrain(remain_mAh, 0.0, CAPACITY_mAh);

    soc = (remain_mAh * 100.0) / CAPACITY_mAh;

  } else {

    tAh = 0;

    soc = constrain(((packV / 3.0 - CELL_MIN) / (3.90 - CELL_MIN)) * 100.0, 0, 100);

    remain_mAh = (CAPACITY_mAh * soc) / 100.0;

  }

}



void balanceTask() {

  float minV = cell[0];

  for(int i = 1; i < 3; i++) if(cell[i] < minV) minV = cell[i];

  for(int i = 0; i < 3; i++) {

    if(cell[i] > BAL_START && (cell[i] - minV) > BAL_DIFF) digitalWrite(balPin[i], HIGH);

    else digitalWrite(balPin[i], LOW);

  }

}



// =====================================================

// CẢI TIẾN TIẾN TRÌNH QUÉT NÚT BẤM (FIX LỖI KHÔNG NHẬN CHÂN 36, 33)

// =====================================================

void handleButtons() {

  // Đọc giá trị điện áp thực tế từ các chân (Có trở treo ngoài)

  bool curChg   = digitalRead(BTN_CHARGE);

  bool curDsg   = digitalRead(BTN_DISCHARGE);

  bool curLock  = digitalRead(BTN_LOCK);



  // 1. Kiểm tra Nút Sạc (Chân 33)

  if (lastBtnChgState == HIGH && curChg == LOW) {

    delay(25); // Chống rung phím phần cứng

    if (digitalRead(BTN_CHARGE) == LOW) {

      bmsMode = 1;

      forceShowStatus = true;

      tBtnPressed = millis();

      btnStatusMsg = "CMD: ENABLE CHARGE";

      tRead = 0; tOLED = 0; // Ép hệ thống xử lý logic và OLED lập tức

    }

  }



  // 2. Kiểm tra Nút Xả (Chân 39 - sw Xả)

  if (lastBtnDsgState == HIGH && curDsg == LOW) {

    delay(25);

    if (digitalRead(BTN_DISCHARGE) == LOW) {

      bmsMode = 2;

      forceShowStatus = true;

      tBtnPressed = millis();

      btnStatusMsg = "CMD: ENABLE DISCHG";

      tRead = 0; tOLED = 0;

    }

  }



  // 3. Kiểm tra Nút Khóa (Chân 36 - sw Khóa) -> OFF cả 2 FET ngay lập tức

  if (lastBtnLockState == HIGH && curLock == LOW) {

    delay(25);

    if (digitalRead(BTN_LOCK) == LOW) {

      bmsMode = 3;

      forceShowStatus = true;

      tBtnPressed = millis();

      btnStatusMsg = "CMD: MANUAL LOCK";

      tRead = 0; tOLED = 0;

    }

  }



  // Lưu trạng thái để quét chu kỳ sau

  lastBtnChgState = curChg;

  lastBtnDsgState = curDsg;

  lastBtnLockState = curLock;

}

// =====================================================

// KIỂM TRA BẢO VỆ AN TOÀN VÀ ĐIỀU PHỐI CHÂN OPTO

// =====================================================

void checkProtection() {

  bool overVoltage = false;

  bool underVoltage = false;

  for(int i = 0; i < 3; i++) {

    if(cell[i] >= CELL_MAX) overVoltage = true;

    if(cell[i] <= CELL_MIN) underVoltage = true;

  }



  // --- MỨC ƯU TIÊN VẬT LÝ CAO NHẤT: LỖI HỆ THỐNG ---

  if(temp1 >= TEMP_MAX || temp2 >= TEMP_MAX) {

    protectionActive = true; protectionMsg = "OVER TEMPERATURE";

    chargeState = LOW; dischargeState = LOW;

  }

  else if(fabs(currentA) >= CURR_MAX) {

    protectionActive = true; protectionMsg = "OVER CURRENT";

    chargeState = LOW; dischargeState = LOW;

  }

  else if(overVoltage && underVoltage) {

    protectionActive = true; protectionMsg = "CELL FAULT CRIT";

    chargeState = LOW; dischargeState = LOW;

  }

 

  // --- MỨC ƯU TIÊN 2: CHẾ ĐỘ CHỌN TỪ NÚT BẤM VÀ CẢNH BÁO ĐƠN ---

  else {

    protectionActive = false;



    if(bmsMode == 3) {

      // Chế độ Khóa hoặc Khởi động ban đầu: Ngắt sạch Fet

      chargeState = LOW;

      dischargeState = LOW;

    }

    else if(bmsMode == 1) {

      // Nhấn nút SẠC: Mở sạc, khóa xả. Nếu lỡ đầy quá áp thì ngắt luôn cả sạc.

      if(overVoltage || packV >= PACK_MAX) chargeState = LOW;

      else chargeState = HIGH;

      dischargeState = LOW;

    }

    else if(bmsMode == 2) {

      // Nhấn nút XẢ: Mở xả, khóa sạc. Nếu pin cạn tụt áp thì ngắt xả.

      chargeState = LOW;

      if(underVoltage) dischargeState = LOW;

      else dischargeState = HIGH;

    }

  }



  // Xuất xung lệnh vật lý ra cổng Opto cách ly

  digitalWrite(OPTO_CHARGE, chargeState);

  digitalWrite(OPTO_DISCHARGE, dischargeState);

}



// =====================================================

// ĐIỀU KHIỂN XUẤT HÌNH OLED 1.3" (SH1106)

// =====================================================

void lcdTask() {

  // NGẮT ƯU TIÊN KHI BẤM NÚT HOẶC KHI VỪA MỞ NGUỒN (HIỂN THỊ 2 GIÂY)

  if(forceShowStatus) {

    if(millis() - tBtnPressed < 2000) {

      u8g2.clearBuffer();

      u8g2.setFont(u8g2_font_6x12_tf);

      u8g2.drawStr(0, 15, "== BMS CONTROL ==");

      u8g2.drawLine(0, 22, 128, 22);

     

      u8g2.drawStr(0, 42, btnStatusMsg.c_str());

     

      String ioState = "CHG:"; ioState += (chargeState?"ON":"OFF");

      ioState += " | DSG:"; ioState += (dischargeState?"ON":"OFF");

      u8g2.drawStr(0, 58, ioState.c_str());

     

      u8g2.sendBuffer();

      return;

    } else {

      forceShowStatus = false;

    }

  }



  if(millis() - tOLED < 1000) return;

  tOLED = millis();



  u8g2.clearBuffer();



  if(protectionActive) {

    u8g2.setFont(u8g2_font_6x12_tf);

    u8g2.drawStr(0, 15, "!!! LOCKDOWN !!!");

    u8g2.drawStr(0, 35, protectionMsg.c_str());

    u8g2.drawStr(0, 55, "SYSTEM DISABLED");

    u8g2.sendBuffer();

    return;

  }



  u8g2.setFont(u8g2_font_6x12_tf);



  if(page == 0) {

    u8g2.drawStr(0, 15, "BMS 3S2P SYSTEM");

    u8g2.drawLine(0, 20, 128, 20);

    char str_v[20], str_i[20], str_soc[20];

    dtostrf(packV, 5, 2, str_v); dtostrf(currentA, 5, 2, str_i); sprintf(str_soc, "SOC: %d%%", soc);

    u8g2.drawStr(0, 38, "Pack V:"); u8g2.drawStr(50, 38, str_v);

    u8g2.drawStr(0, 55, "Curr I:"); u8g2.drawStr(50, 55, str_i); u8g2.drawStr(95, 55, str_soc);

  }

  else if(page == 1) {

    u8g2.drawStr(0, 15, "CELL VOLTAGES");

    u8g2.drawLine(0, 20, 128, 20);

    char c1[15], c2[15], c3[15];

    dtostrf(cell[0], 4, 2, c1); dtostrf(cell[1], 4, 2, c2); dtostrf(cell[2], 4, 2, c3);

    u8g2.drawStr(0, 38, "C1: "); u8g2.drawStr(25, 38, c1);

    u8g2.drawStr(68, 38, "C2: "); u8g2.drawStr(93, 38, c2);

    u8g2.drawStr(0, 55, "C3: "); u8g2.drawStr(25, 55, c3);

  }

  else if(page == 2) {

    u8g2.drawStr(0, 15, "TEMPERATURES & OPTO");

    u8g2.drawLine(0, 20, 128, 20);

    char t_str[30];

    dtostrf(temp1, 4, 1, t_str); u8g2.drawStr(0, 38, "T1:"); u8g2.drawStr(22, 38, t_str);

    dtostrf(temp2, 4, 1, t_str); u8g2.drawStr(65, 38, "T2:"); u8g2.drawStr(87, 38, t_str);



    String stateStr = "CHG:"; stateStr += (chargeState ? "ON" : "OFF");

    stateStr += " | DSG:"; stateStr += (dischargeState ? "ON" : "OFF");

    u8g2.drawStr(0, 55, stateStr.c_str());



    if(digitalRead(FAN_PIN)) u8g2.drawStr(100, 15, "[FAN]");

  }



  u8g2.sendBuffer();

}



void autoPageTask() {

  if(!forceShowStatus && (millis() - tAutoPage > 3000)) {

    tAutoPage = millis();

    page++;

    if(page > 2) page = 0;

  }

}



// =====================================================

// SETUP KHỞI CHẠY ĐẦU VÀO

// =====================================================

void setup() {

  Serial.begin(115200);

  Wire.begin(21, 22);

  u8g2.begin();



  if (!ads.begin(0x48)) {

    u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x12_tf);

    u8g2.drawStr(0, 30, "ERR: NO ADS1115"); u8g2.sendBuffer();

    while (1);

  }

  ads.setGain(GAIN_TWOTHIRDS);



  pinMode(OPTO_CHARGE, OUTPUT);

  pinMode(OPTO_DISCHARGE, OUTPUT);

  pinMode(FAN_PIN, OUTPUT);



  // Mẹo phần cứng quan trọng: Chân 36 và 39 trên ESP32 KHÔNG CÓ điện trở PULLUP nội bộ.

  // Khai báo INPUT để nhận diện sườn áp sạch hơn nếu bạn đã có trở treo ngoài (10k nối lên 3.3V)

  pinMode(BTN_CHARGE, INPUT_PULLUP);

  pinMode(BTN_DISCHARGE, INPUT);

  pinMode(BTN_LOCK, INPUT);



  // YÊU CẦU: Hệ thống bắt đầu luôn tắt 2 fet này

  digitalWrite(OPTO_CHARGE, LOW);

  digitalWrite(OPTO_DISCHARGE, LOW);

  digitalWrite(FAN_PIN, LOW);



  for(int i = 0; i < 3; i++) { pinMode(balPin[i], OUTPUT); digitalWrite(balPin[i], LOW); }

  analogReadResolution(12);



  tBtnPressed = millis(); // Kích hoạt bộ đếm thời gian cho màn hình khóa khởi động ban đầu



  u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(1, 35, "BMS START: LOCKED");

  u8g2.sendBuffer();

  delay(1000);

}



// =====================================================

// VÒNG LẶP ĐIỀU KHIỂN

// =====================================================

void loop() {

  handleButtons();



  if(millis() - tRead > 500) {

    tRead = millis();

    readCells();

    readCurrent();

    readTemp();

    checkProtection();

    capacityTask();

    balanceTask();

  }



  autoPageTask();

  lcdTask();

}
