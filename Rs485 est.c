#define RS485_DE_RE  4

void setup() {
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);  // Mặc định chế độ RECEIVE
  RS485.begin(9600, SERIAL_8N1, 16, 17);
}

void rs485TextSend() {
  digitalWrite(RS485_DE_RE, HIGH);  // Chuyển sang chế độ TRANSMIT
  delay(10);
  
  // Gửi dữ liệu...
  RS485.print("PACK=");
  // ...
  
  RS485.flush();
  digitalWrite(RS485_DE_RE, LOW);   // Quay lại chế độ RECEIVE
}
