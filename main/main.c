/*
 * ผู้จัดทำ: นายณภัทร รัศมี (รหัสนักศึกษา 68030079)
 * ข้อสอบประจำตัว: ข้อ 2 (รหัสนักศึกษา 68030079 -> หลักสุดท้าย 9 % 4 = 1 -> 1 + 1 = ข้อ 2)
 * โจทย์ข้อ 2: ใช้สวิตช์ 2 ตัวควบคุมการเคลื่อนที่และหยุดของ LED (ESP-IDF)
 *
 * เงื่อนไข (Remarks):
 * - LED 8 ดวง
 * - การเคลื่อนที่หมายถึงวิ่งไปและวิ่งกลับ (ซ้ายไปขวา และ ขวาไปซ้าย)
 * - สวิตช์สามารถกดควบคุมการเคลื่อนที่ได้ทุกขณะ (Non-blocking polling ทุก 10ms พร้อม Debounce)
 * - สวิตช์ 2 ตัว (+/-)
 *
 * การต่อสายบนบอร์ดทดลอง ESP32:
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
 *     PUSH BT1 (สวิตช์ + / เริ่มเคลื่อนที่) -> GPIO 32
 *     PUSH BT2 (สวิตช์ - / หยุดการเคลื่อนที่) -> GPIO 33
 */

#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "LED_EXAM_Q2";

// กำหนดจำนวนและขา GPIO ของ LED ทั้ง 8 ดวง
#define NUM_LEDS 8
static const gpio_num_t led_pins[NUM_LEDS] = {
    GPIO_NUM_23, // LED 1
    GPIO_NUM_22, // LED 2
    GPIO_NUM_21, // LED 3
    GPIO_NUM_19, // LED 4
    GPIO_NUM_18, // LED 5
    GPIO_NUM_5,  // LED 6
    GPIO_NUM_17, // LED 7
    GPIO_NUM_16  // LED 8
};

// กำหนดขา GPIO ของสวิตช์ 2 ตัว (+/-)
#define SW_PLUS_PIN     GPIO_NUM_32  // สวิตช์ + (PUSH BT1) : สั่งให้ LED เคลื่อนที่ (Start / Resume)
#define SW_MINUS_PIN    GPIO_NUM_33  // สวิตช์ - (PUSH BT2) : สั่งให้ LED หยุด (Stop / Pause)

// กำหนดระดับลอจิกของ LED (1 = Active HIGH, 0 = Active LOW)
#define LED_ON_LEVEL    1
#define LED_OFF_LEVEL   0

// ความเร็วในการเคลื่อนที่ของ LED (มิลลิวินาทีต่อก้าว) และเวลา Debounce สวิตช์
#define MOVE_INTERVAL_MS    150
#define DEBOUNCE_DELAY_MS   30

// ฟังก์ชันอัปเดตการแสดงผล LED ทั้ง 8 ดวง
static void update_leds(int active_index)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i == active_index) {
            gpio_set_level(led_pins[i], LED_ON_LEVEL);
        } else {
            gpio_set_level(led_pins[i], LED_OFF_LEVEL);
        }
    }
}

// ฟังก์ชันตั้งค่าเริ่มต้นให้ GPIO ของ LED และสวิตช์
static void init_gpio(void)
{
    // 1. ตั้งค่าขา LED ทั้ง 8 ดวงเป็น Output
    uint64_t led_pin_mask = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        led_pin_mask |= (1ULL << led_pins[i]);
    }

    gpio_config_t io_conf_led = {
        .pin_bit_mask = led_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_led);

    // 2. ตั้งค่าขาสวิตช์ 2 ตัวเป็น Input พร้อมเปิด Internal Pull-up (กดติด LOW ปล่อยเป็น HIGH)
    uint64_t sw_pin_mask = (1ULL << SW_PLUS_PIN) | (1ULL << SW_MINUS_PIN);
    gpio_config_t io_conf_sw = {
        .pin_bit_mask = sw_pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_sw);
}

void app_main(void)
{
    init_gpio();

    bool is_moving = true;          // สถานะเริ่มต้น: กำลังเคลื่อนที่ (true = วิ่ง, false = หยุด)
    int current_led = 0;            // ตำแหน่ง LED ปัจจุบัน (0 ถึง 7)
    int direction = 1;              // ทิศทางการวิ่ง (1 = วิ่งไป 0->7, -1 = วิ่งกลับ 7->0)

    int64_t last_move_time_ms = esp_timer_get_time() / 1000;
    int64_t last_debounce_plus_ms = 0;
    int64_t last_debounce_minus_ms = 0;

    int last_sw_plus_state = 1;
    int last_sw_minus_state = 1;

    // แสดงผล LED ดวงแรก
    update_leds(current_led);
    ESP_LOGI(TAG, "ESP-IDF 8-LED Controller Started (Question 2: Move / Stop)");

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        // อ่านค่าสถานะสวิตช์ทั้ง 2 ตัวได้ทุกขณะ (Non-blocking)
        int reading_plus = gpio_get_level(SW_PLUS_PIN);
        int reading_minus = gpio_get_level(SW_MINUS_PIN);

        // ตรวจจับการกดสวิตช์ + (GPIO 32) -> เริ่มเคลื่อนที่ (Start / Move)
        if (reading_plus != last_sw_plus_state && (now_ms - last_debounce_plus_ms) > DEBOUNCE_DELAY_MS) {
            last_debounce_plus_ms = now_ms;
            last_sw_plus_state = reading_plus;
            if (reading_plus == 0) { // กดสวิตช์ (Active LOW)
                is_moving = true;
                ESP_LOGI(TAG, "Switch (+) Pressed -> LED Moving (START)");
            }
        }

        // ตรวจจับการกดสวิตช์ - (GPIO 33) -> หยุดการเคลื่อนที่ (Stop / Pause)
        if (reading_minus != last_sw_minus_state && (now_ms - last_debounce_minus_ms) > DEBOUNCE_DELAY_MS) {
            last_debounce_minus_ms = now_ms;
            last_sw_minus_state = reading_minus;
            if (reading_minus == 0) { // กดสวิตช์ (Active LOW)
                is_moving = false;
                ESP_LOGI(TAG, "Switch (-) Pressed -> LED Stopped (STOP) at LED %d", current_led + 1);
            }
        }

        // อัปเดตตำแหน่งการเคลื่อนที่ของ LED (วิ่งไปและวิ่งกลับ)
        if (is_moving && (now_ms - last_move_time_ms >= MOVE_INTERVAL_MS)) {
            last_move_time_ms = now_ms;

            current_led += direction;

            // เมื่อวิ่งไปสุดขอบขวา (ดวงที่ 8) ให้เปลี่ยนทิศทางเป็นวิ่งกลับ
            if (current_led >= NUM_LEDS - 1) {
                current_led = NUM_LEDS - 1;
                direction = -1;
            }
            // เมื่อวิ่งกลับมาสุดขอบซ้าย (ดวงที่ 1) ให้เปลี่ยนทิศทางเป็นวิ่งไป
            else if (current_led <= 0) {
                current_led = 0;
                direction = 1;
            }

            update_leds(current_led);
        }

        // หน่วงเวลาสั้นๆ 10ms เพื่อให้ FreeRTOS Watchdog ทำงานปกติ และยังตอบสนองปุ่มกดได้ทันทีทุกขณะ
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

