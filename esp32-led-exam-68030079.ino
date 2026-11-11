/*
 * ผู้จัดทำ: นายณภัทร รัศมี (รหัสนักศึกษา 68030079)
 * ข้อสอบประจำตัว: ข้อ 2 (รหัสนักศึกษา 68030079 -> หลักสุดท้าย 9 % 4 = 1 -> 1 + 1 = ข้อ 2)
 * โจทย์ข้อ 2: ใช้สวิตช์ 2 ตัวควบคุมการเคลื่อนที่และหยุดของ LED
 *
 * เงื่อนไข (Remarks):
 * - LED 8 ดวง
 * - การเคลื่อนที่หมายถึงวิ่งไปและวิ่งกลับ (ซ้ายไปขวา และ ขวาไปซ้าย)
 * - สวิตช์สามารถกดควบคุมการเคลื่อนที่ได้ทุกขณะ (Non-blocking ด้วย millis() + Debounce)
 * - สวิตช์ 2 ตัว (+/-)
 *
 * การต่อสายบนบอร์ดทดลอง ESP32:
 * - เสียบตัวบอร์ด ESP32 ลงบนซ็อกเก็ต ESP32 บนบอร์ดเขียวให้ตรงขา (ฝั่ง USB หันขึ้นด้านบนตามรูป)
 * - LED 1 ถึง 8 (ขา Header "LEDs" ล่างซ้ายของบอร์ด):
 *     LED1 -> GPIO 23
 *     LED2 -> GPIO 22
 *     LED3 -> GPIO 21
 *     LED4 -> GPIO 19
 *     LED5 -> GPIO 18
 *     LED6 -> GPIO 5
 *     LED7 -> GPIO 17
 *     LED8 -> GPIO 16
 * - สวิตช์ 2 ตัว (PUSH BT1 และ PUSH BT2 ด้านบนของบอร์ด):
 *     PUSH BT1 (สวิตช์ + / เริ่มเคลื่อนที่ หรือ เดินหน้า) -> GPIO 32
 *     PUSH BT2 (สวิตช์ - / หยุดการเคลื่อนที่)            -> GPIO 33
 */

#include <Arduino.h>

// กำหนดขา LED ทั้ง 8 ดวง (เรียงจากดวงที่ 1 ถึง 8)
const int NUM_LEDS = 8;
const int ledPins[NUM_LEDS] = {23, 22, 21, 19, 18, 5, 17, 16};

// กำหนดขาสวิตช์ 2 ตัว (+ และ -)
const int SW_PLUS_PIN  = 32; // PUSH BT1 (+) : สั่งให้ LED เคลื่อนที่ (Start / Resume)
const int SW_MINUS_PIN = 33; // PUSH BT2 (-) : สั่งให้ LED หยุด (Stop / Pause)

// หากบอร์ดต่อ LED แบบ Active HIGH (จ่าย HIGH แล้วไฟติด) ให้เป็น true
// หากไฟติดสลับกัน (Active LOW) ให้เปลี่ยนเป็น false
const bool LED_ACTIVE_HIGH = true;

// ตัวแปรสถานะการทำงาน
bool isMoving = true;          // สถานะกำลังเคลื่อนที่ (true = วิ่ง, false = หยุด)
int currentLed = 0;            // ตำแหน่ง LED ปัจจุบัน (0 ถึง 7)
int direction = 1;             // ทิศทางการวิ่ง (1 = วิ่งไป 0->7, -1 = วิ่งกลับ 7->0)
unsigned long lastMoveTime = 0;
const unsigned long moveInterval = 150; // ความเร็วในการวิ่ง (มิลลิวินาทีต่อก้าว)

// ตัวแปรสำหรับ Debounce สวิตช์ (เพื่อให้กดได้ทุกขณะอย่างแม่นยำ)
bool lastSwPlusState = HIGH;
bool lastSwMinusState = HIGH;
unsigned long lastDebouncePlus = 0;
unsigned long lastDebounceMinus = 0;
const unsigned long debounceDelay = 30;

// ฟังก์ชันแสดงผล LED ดวงปัจจุบัน
void updateLeds() {
  for (int i = 0; i < NUM_LEDS; i++) {
    bool turnOn = (i == currentLed);
    if (LED_ACTIVE_HIGH) {
      digitalWrite(ledPins[i], turnOn ? HIGH : LOW);
    } else {
      digitalWrite(ledPins[i], turnOn ? LOW : HIGH);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // ตั้งค่าขา LED ทั้ง 8 ดวงเป็น OUTPUT
  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(ledPins[i], OUTPUT);
  }

  // ตั้งค่าขาสวิตช์ 2 ตัวเป็น INPUT_PULLUP (กดแล้วเป็น LOW)
  // หมายเหตุ: รองรับทั้งสวิตช์ที่ต่อลง GND (Active LOW)
  pinMode(SW_PLUS_PIN, INPUT_PULLUP);
  pinMode(SW_MINUS_PIN, INPUT_PULLUP);

  // แสดงสถานะ LED ดวงแรกตอนเริ่มต้น
  updateLeds();
  Serial.println("ESP32 8-LED Controller Started (Question 2: Move / Stop)");
}

void loop() {
  unsigned long now = millis();

  // 1. อ่านค่าสวิตช์ตัวที่ 1 (+) และตัวที่ 2 (-) ได้ทุกขณะ (Non-blocking)
  bool readingPlus = digitalRead(SW_PLUS_PIN);
  bool readingMinus = digitalRead(SW_MINUS_PIN);

  // ตรวจจับการกดสวิตช์ + (PUSH BT1) -> เริ่มเคลื่อนที่ (Run/Start)
  if (readingPlus != lastSwPlusState && (now - lastDebouncePlus) > debounceDelay) {
    lastDebouncePlus = now;
    lastSwPlusState = readingPlus;
    if (readingPlus == LOW) { // เมื่อกดปุ่ม (Active LOW)
      isMoving = true;
      Serial.println("SW+ Pressed: LED Moving (START)");
    }
  }

  // ตรวจจับการกดสวิตช์ - (PUSH BT2) -> หยุดการเคลื่อนที่ (Stop/Pause)
  if (readingMinus != lastSwMinusState && (now - lastDebounceMinus) > debounceDelay) {
    lastDebounceMinus = now;
    lastSwMinusState = readingMinus;
    if (readingMinus == LOW) { // เมื่อกดปุ่ม (Active LOW)
      isMoving = false;
      Serial.println("SW- Pressed: LED Stopped (STOP)");
    }
  }

  // 2. อัปเดตการเคลื่อนที่ของ LED (วิ่งไปและวิ่งกลับ) เมื่ออยู่ในสถานะเคลื่อนที่
  if (isMoving && (now - lastMoveTime >= moveInterval)) {
    lastMoveTime = now;

    // เลื่อนตำแหน่ง LED
    currentLed += direction;

    // ถ้าวิ่งไปสุดขอบขวา (ดวงสุดท้าย) ให้กลับทิศวิ่งกลับ
    if (currentLed >= NUM_LEDS - 1) {
      currentLed = NUM_LEDS - 1;
      direction = -1;
    }
    // ถ้าวิ่งกลับมาสุดขอบซ้าย (ดวงแรก) ให้กลับทิศวิ่งไป
    else if (currentLed <= 0) {
      currentLed = 0;
      direction = 1;
    }

    updateLeds();
  }
}

