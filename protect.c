// ============================================================================
// KHỐI BẢO VỆ AN TOÀN VÀ ĐIỀU PHỐI OPTO (ĐÃ CHUẨN HÓA CHỐNG NHIỄU)
// ============================================================================
void checkProtection() {
  // --------------------------------------------------------------------------
  // 1. KIỂM TRA TRẠNG THÁI LỖI TOÀN CỤC (QUÉT CELL VÀ DÒNG)
  // --------------------------------------------------------------------------
  bool overVoltage = false;
  bool underVoltage = false;
  
  for(int i = 0; i < 3; i++) {
    if(cell[i] >= CELL_MAX) overVoltage = true;
    if(cell[i] <= CELL_MIN) underVoltage = true;
  }

  // Biến đếm thời gian chống sốc nhiễu (Debounce) cho Quá dòng và Lỗi Cell kịch khung
  static unsigned long tOverCurrent = 0;
  static unsigned long tCellFault = 0;

  // --------------------------------------------------------------------------
  // MỨC ƯU TIÊN 1: CÁC LỖI NGUY HIỂM KHẨN CẤP (LOCKDOWN HỆ THỐNG)
  // --------------------------------------------------------------------------
  
  // A. BẢO VỆ QUÁ NHIỆT (OVER TEMPERATURE) - Ưu tiên tối cao
  if(temp1 >= TEMP_MAX || temp2 >= TEMP_MAX) {
    protectionActive = true; 
    protectionMsg = "OVER TEMPERATURE";
    chargeState = LOW; 
    dischargeState = LOW;
  }
  
  // B. BẢO VỆ QUÁ DÒNG (OVER CURRENT) - Lọc nhiễu đỉnh xung trong 200ms
  else if(fabs(currentA) >= CURR_MAX) {
    if(tOverCurrent == 0) tOverCurrent = millis();
    if(millis() - tOverCurrent >= 200) { // Nếu quá dòng liên tục > 200ms mới khóa
      protectionActive = true; 
      protectionMsg = "OVER CURRENT";
      chargeState = LOW; 
      dischargeState = LOW;
    }
  }
  
  // C. BẢO VỆ LỖI CELL CRITICAL (Vừa báo quá áp vừa thấp áp đồng thời - lỗi mạch đọc)
  else if(overVoltage && underVoltage) {
    if(tCellFault == 0) tCellFault = millis();
    if(millis() - tCellFault >= 500) { // Chống nhiễu ADC trong 500ms
      protectionActive = true; 
      protectionMsg = "CELL FAULT CRIT";
      chargeState = LOW; 
      dischargeState = LOW;
    }
  }
  
  // --------------------------------------------------------------------------
  // MỨC ƯU TIÊN 2: ĐIỀU KHIỂN CHẾ ĐỘ THƯỜNG THEO NÚT BẤM VÀ CẢNH BÁO ĐƠN LẺ
  // --------------------------------------------------------------------------
  else {
    // Reset các bộ đếm thời gian lỗi nếu hệ thống đã về dải an toàn
    tOverCurrent = 0;
    tCellFault = 0;

    // Cơ chế Tự động Reset Lock khi nhiệt độ hạ nhiệt an toàn (Dưới TEMP_MAX - 3°C)
    if(protectionActive && (temp1 < (TEMP_MAX - 3.0) && temp2 < (TEMP_MAX - 3.0))) {
       protectionActive = false;
       bmsMode = 3; // Trả về trạng thái Khóa thông thường, chờ người dùng bấm nút kích hoạt lại
    }

    // Nếu không bị dính lockdown do các lỗi nguy hiểm, chạy logic điều phối FET
    if(!protectionActive) {
      
      // Chế độ 3: MANUAL LOCK (Khóa bằng tay hoặc Trạng thái khởi tạo ban đầu)
      if(bmsMode == 3) {
        chargeState = LOW;
        dischargeState = LOW;
      }
      
      // Chế độ 1: CHẾ ĐỘ SẠC (ENABLE CHARGE)
      else if(bmsMode == 1) {
        dischargeState = LOW; // Khóa chặt đường xả khi đang sạc
        
        // Bảo vệ quá áp đơn cell hoặc quá áp toàn pack
        if(overVoltage || packV >= PACK_MAX) {
          chargeState = LOW; // Ngắt sạc ngay lập tức
        } 
        // Hysteresis sạc: Tự bật lại sạc nếu áp tụt xuống ngưỡng an toàn (Tránh mấp mé)
        else if(!overVoltage && packV < (PACK_MAX - 0.30)) {
          chargeState = HIGH; 
        }
      }
      
      // Chế độ 2: CHẾ ĐỘ XẢ (ENABLE DISCHARGE)
      else if(bmsMode == 2) {
        chargeState = LOW; // Khóa chặt đường sạc khi đang xả
        
        // Bảo vệ thấp áp đơn cell
        if(underVoltage) {
          dischargeState = LOW; // Ngắt xả ngay để bảo vệ pin không chết đói
        } else {
          dischargeState = HIGH;
        }
      }
    }
  }

  // --------------------------------------------------------------------------
  // 3. XUẤT XUNG LỆNH VẬT LÝ RA CỔNG OPTO CÁCH LY
  // --------------------------------------------------------------------------
  digitalWrite(OPTO_CHARGE, chargeState);
  digitalWrite(OPTO_DISCHARGE, dischargeState);
}
