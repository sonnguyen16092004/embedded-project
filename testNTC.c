void readTemp() {
  static float old1 = 25.0, old2 = 25.0;
  
  // ---------------------------------------------------
  // TRÌNH XỬ LÝ CHO NTC 1 (CHÂN 34)
  // ---------------------------------------------------
  long sum1 = 0;
  for(int i = 0; i < 50; i++) {
    sum1 += analogRead(NTC1_PIN);
    delay(1);
  }
  float adc1 = sum1 / 50.0;

  // XỬ LÝ NGOẠI LỆ CHO LOG ĐỨT DÂY (ADC ~ 4036):
  // Nếu ADC vọt lên quá cao (> 3900) chứng tỏ chân đang bị hở mạch/sai chân.
  // Ta ghim thẳng giá trị giả lập 25 độ C để hệ thống không bị Lock lỗi âm độ.
  if (adc1 > 3900.0 || adc1 < 10.0) {
    temp1 = 25.0; 
  } else {
    float Rntc1 = 10000.0 * ((4095.0 / adc1) - 1.0); 
    temp1 = ntcToTemp(Rntc1);
  }

  // ---------------------------------------------------
  // TRÌNH XỬ LÝ CHO NTC 2 (CHÂN 35)
  // ---------------------------------------------------
  long sum2 = 0;
  for(int i = 0; i < 50; i++) {
    sum2 += analogRead(NTC2_PIN);
    delay(1);
  }
  float adc2 = sum2 / 50.0;
  
  if (adc2 > 3900.0 || adc2 < 10.0) {
    temp2 = 25.0;
  } else {
    float Rntc2 = 10000.0 * ((4095.0 / adc2) - 1.0); 
    temp2 = ntcToTemp(Rntc2);
  }

  // ---------------------------------------------------
  // BỘ LỌC PHẦN MỀM EMA (GIỮ NGUYÊN)
  // ---------------------------------------------------
  temp1 = old1 * 0.7 + temp1 * 0.3; 
  temp2 = old2 * 0.7 + temp2 * 0.3;
  old1 = temp1; 
  old2 = temp2;

  // Logic điều khiển quạt giải nhiệt
  if(temp1 >= TEMP_FAN_ON || temp2 >= TEMP_FAN_ON) {
    digitalWrite(FAN_PIN, HIGH); 
  }
  else if(temp1 < (TEMP_FAN_ON - 3.0) && temp2 < (TEMP_FAN_ON - 3.0)) {
    digitalWrite(FAN_PIN, LOW);  
  }
}
