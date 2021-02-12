#include <obniz.h>
#include <M5Stack.h>
#include "utility/M5Timer.h"
#include "esp_deep_sleep.h"
#include <string.h>

#define GATE_EN_PIN 13
#define FUNC_SELECT_PIN 26

const uint8_t is_debug = true;

bool is_debug_screen_mode = false;

int ack_timer_id = -1;
bool is_ack_done = false;

bool button_states[3];

M5Timer m5_timer;

typedef enum {
  BUTTON_TYPE_A = 0,
  BUTTON_TYPE_B,
  BUTTON_TYPE_C,
} ButtonType;

typedef enum {
  STATE_SLEEP = 0,
  STATE_WAKEUP,
  STATE_INPUT,
  STATE_AUTHORIZING,
  STATE_AUTHORIZED,
  STATE_AUTH_FAILED,
  STATE_NETWORK_TIMEOUT,
  STATE_CLOUD_NOT_CONNECTED
} State;

// 認証成功後、初期状態に戻るまでの待ち時間
#define WAIT_TIME_AFTER_AUTHORIZED 10000
#define NETWORK_TIMEOUT 10000
#define GATE_OPEN_TIME 2000
#define GATE_CLOSE_TIME 2000
#define GATE_CLOSE_INTERVAL 60000

uint32_t deep_sleep_timeout_no_action = 600000UL;
uint32_t deep_sleep_wakeup_timeout = 600000UL;

#define PASSWORD_SIZE 4
#define CURSOR_POS_SIZE (PASSWORD_SIZE+1)

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

typedef struct {
  char password[PASSWORD_SIZE+1];
  uint8_t pos;
} InputState;

InputState input_state;
uint8_t is_display_dirty = 1;
uint8_t is_deep_sleeping = 1;
uint8_t is_update_mode = 0;
uint32_t last_action_msec;

State state = STATE_WAKEUP;

void change_state(State state_) {
  state = state_;
  is_display_dirty = 1;
}

void check_update() {
  char cmd[6] = "$upd?";
  obniz.commandSend((uint8_t*)cmd, 5);
}

void request_fetch_pass() {
  char cmd[3] = "$f";
  obniz.commandSend((uint8_t*)cmd, 2);
}

void request_ack() {
  char cmd[4] = "$a?";
  obniz.commandSend((uint8_t*)cmd, 3);
}

void request_ack_timeout() {
  if (is_ack_done) {
    return;
  }
  if (ack_timer_id != -1) {
    m5_timer.deleteTimer(ack_timer_id);
    ack_timer_id = -1;
  }
  change_state(STATE_CLOUD_NOT_CONNECTED);
}

void reset_input_state() {
  for (uint8_t i = 0; i < PASSWORD_SIZE; ++i) {
    input_state.password[i] = '0';
  }
  input_state.password[PASSWORD_SIZE] = 0; // null terminate
  input_state.pos = 0;
}

bool isEndPos() {
  return input_state.pos == (CURSOR_POS_SIZE - 1);
}

bool is_debug_screen() {
  return button_states[0] && button_states[2];
}

bool is_debug_screen_leaved() {
  return !button_states[0] && !button_states[2];
}

void setup_input() {
  reset_input_state();
}

void debug_print(const char* format) {
  if (is_debug) {
    //M5.Lcd.printf(format);
    Serial.println(format);
  }
}

void print_system_msg(const char* format) {
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextColor(~GREEN);
  M5.Lcd.drawCentreString(format, 0, 100, 4);
}

void sleep_until_btn_down() {
  is_deep_sleeping = 1;
  change_state(STATE_SLEEP);
  esp_deep_sleep_enable_ext0_wakeup(GPIO_NUM_38, 0);
  esp_deep_sleep_enable_timer_wakeup(deep_sleep_wakeup_timeout * 1000);
  esp_deep_sleep_start();
}

void on_connected() {
  change_state(STATE_INPUT);
  check_update();
  request_fetch_pass();
}

// TODO: エラー処理(エラー表示？)
void onEvent(os_event_t event, uint8_t* data, uint16_t length) {
  switch (event) {
  case PLUGIN_EVENT_NETWORK_WIFI_CONNECTING:
    debug_print("Wifi Connecting");
    print_system_msg("Connecting server...");
    break;
  case PLUGIN_EVENT_NETWORK_WIFI_SCANNING:
    debug_print("Wifi Scanning");
    break;
  case PLUGIN_EVENT_NETWORK_WIFI_NOTFOUND:
    debug_print("Wifi Not Found");
    break;
  case PLUGIN_EVENT_NETWORK_HARDWARE_CONNECTED:
    debug_print("hardware Connected!!!");
    break;
  case PLUGIN_EVENT_NETWORK_HARDWARE_DISCONNECTED:
    debug_print("hardware Disconnected!!!");
    break;
  case PLUGIN_EVENT_NETWORK_CLOUD_CONNECTED:
    debug_print("cloud Connected!!!");
    is_ack_done = false;
    ack_timer_id = m5_timer.setInterval(500, request_ack);
    m5_timer.setTimeout(12000, request_ack_timeout);
    //m5_timer.setTimeout(10000, on_connected);
    break;
  case PLUGIN_EVENT_NETWORK_CLOUD_DISCONNECTED:
    debug_print("cloud Disconnected");
    break;
  default:
    break;
  }
}

void gate_open() {
  digitalWrite(FUNC_SELECT_PIN, LOW);
  digitalWrite(GATE_EN_PIN, HIGH);
  m5_timer.setTimeout(GATE_OPEN_TIME, stop_gate_open);
}

void stop_gate_close() {
  digitalWrite(FUNC_SELECT_PIN, LOW);
  digitalWrite(GATE_EN_PIN, LOW);

  change_state(STATE_INPUT);
  reset_input_state();
}

void gate_close() {
  digitalWrite(FUNC_SELECT_PIN, HIGH);
  digitalWrite(GATE_EN_PIN, HIGH);
  m5_timer.setTimeout(GATE_CLOSE_TIME, stop_gate_close);
}

void stop_gate_open() {
  digitalWrite(GATE_EN_PIN, LOW);
  m5_timer.setTimeout(GATE_CLOSE_INTERVAL, stop_gate_close);
  //m5_timer.setTimeout(GATE_CLOSE_INTERVAL, gate_close);
}

void return_to_default() {
  change_state(STATE_INPUT);
  reset_input_state();
}

void network_timeout() {
  if (state == STATE_AUTHORIZING) {
    change_state(STATE_NETWORK_TIMEOUT);
    m5_timer.setTimeout(2000, return_to_default);
  }
}

bool str_start_with(const char* target, const char* start_str, uint16_t len) {
  for (uint16_t i = 0; i < len && start_str[i] != 0; ++i) {
    if (target[i] == 0 || target[i] != start_str[i]) {
      return false;
    }
  }
  return true;
}

bool is_sleep_timeout_set_response(const char* data, uint16_t len) {
  return str_start_with(data, "$set:sleep_timeout=", len);
}

bool is_wakeup_timeout_set_response(const char* data, uint16_t len) {
  return str_start_with(data, "$set:wakeup_timeout=", len);
}

uint32_t extract_data_from_response(const char* data, uint16_t length) {
  bool found_eq = false;
  uint32_t value = 0;
  for (uint16_t i = 0; i < length && data[i] != 0; ++i) {
    if (found_eq) {
      if ('0' <= data[i] && data[i] <= '9') {
        value = value * 10 + (uint8_t)(data[i] - '0');
      } else {
        break;
      }
    } else if (data[i] == '=') {
      found_eq = true;
    }
  }
  return value;
}

void onCommandReceive(uint8_t* data, uint16_t length) {
  //M5.Lcd.printf("Command Received!");
  //for (uint16_t i = 0; i < length; ++i) {
  //  M5.Lcd.printf("%c", (char)data[i]);
  //}

  if (length > 0) {
    if ((char)data[0] == 'O') {
      change_state(STATE_AUTHORIZED);
      gate_open();
      m5.Speaker.setBeep(880, 1000);
      m5.Speaker.beep();
    } else if ((char)data[0] == 'N'){
      change_state(STATE_AUTH_FAILED);
      m5_timer.setTimeout(2000, return_to_default);
      m5.Speaker.setBeep(440, 500);
      m5.Speaker.beep();
    } else if ((char)data[0] == 'U'){
      is_update_mode = 1;
    } else if ((char)data[0] == 'A'){
      if (ack_timer_id != -1) {
        m5_timer.deleteTimer(ack_timer_id);
        ack_timer_id = -1;
      }
      is_ack_done = true;
      on_connected();
    } else if (is_sleep_timeout_set_response((const char*)data, length)) {
      uint32_t value = extract_data_from_response((const char*)data, length);
      if (value > 60000UL) {
        deep_sleep_timeout_no_action = value;
      }
    } else if (is_wakeup_timeout_set_response((const char*)data, length)) {
      uint32_t value = extract_data_from_response((const char*)data, length);
      if (value > 60000UL) {
        deep_sleep_wakeup_timeout = value;
      }
    }
  }
}

void setup_m5() {
  M5.begin();

  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(1);
}

void setup_obniz() {
  //obniz.setKey("k2IDdKpmvYosM_anRCft9capGPc_ZwR7"); // development
  obniz.setKey("CgTamDnL6Ro2a5QnUNE6kIbFHJxtLypt"); // production
  obniz.onEvent(onEvent);
  obniz.commandReceive(onCommandReceive);

  // ネットワーク経由でIOにアクセスされないように保護
  obniz.pinReserve(BUTTON_A_PIN);
  obniz.pinReserve(BUTTON_B_PIN);
  obniz.pinReserve(BUTTON_C_PIN);
  obniz.pinReserve(TFT_LED_PIN);
  obniz.pinReserve(TFT_DC_PIN);
  obniz.pinReserve(TFT_CS_PIN);
  obniz.pinReserve(TFT_MOSI_PIN);
  obniz.pinReserve(TFT_CLK_PIN);
  obniz.pinReserve(TFT_RST_PIN);
  obniz.pinReserve(TFT_MISO_PIN);
  obniz.pinReserve(TFCARD_CS_PIN);
  obniz.pinReserve(SPEAKER_PIN);
  obniz.pinReserve(LORA_CS_PIN);
  obniz.pinReserve(LORA_RST_PIN);
  obniz.pinReserve(LORA_IRQ_PIN);

  obniz.start();
}

void input_ok() {
  obniz.commandSend((uint8_t*)input_state.password, PASSWORD_SIZE);
  change_state(STATE_AUTHORIZING);
  m5_timer.setTimeout(NETWORK_TIMEOUT, network_timeout);
}

void input_change_num() {
  char next_chr = input_state.password[input_state.pos] + 1;
  if (next_chr > '9') {
    next_chr = '0';
  }
  input_state.password[input_state.pos] = next_chr;
  is_display_dirty = 1;
}

void input_button(uint8_t btn_idx, uint8_t on) {
  button_states[btn_idx] = on;
  if (state != STATE_INPUT) { return; }
  if (!on) { return; }

  last_action_msec = millis();

  switch (btn_idx) {
    case BUTTON_TYPE_A:
      input_state.pos = (input_state.pos + CURSOR_POS_SIZE - 1) % CURSOR_POS_SIZE;
      if (isEndPos()) {
        input_ok();
      }
      is_display_dirty = 1;
      break;
    case BUTTON_TYPE_B:
      // // without M5 Faces
      //if (isEndPos()) {
      //  input_ok();
      //} else {
      //  input_change_num();
      //}
      //is_display_dirty = 1;
      break;
    case BUTTON_TYPE_C:
      input_state.pos = (input_state.pos + 1) % CURSOR_POS_SIZE;
      if (isEndPos()) {
        input_ok();
      }
      is_display_dirty = 1;
      break;
    default:
      break;
  }
}

void process_button() {
  if (M5.BtnA.wasPressed()) {
    input_button(0, 1);
  }
  if (M5.BtnB.wasPressed()) {
    input_button(1, 1);
  }
  if (M5.BtnC.wasPressed()) {
    input_button(2, 1);
  }
  if (M5.BtnA.wasReleased()) {
    input_button(0, 0);
  }
  if (M5.BtnB.wasReleased()) {
    input_button(1, 0);
  }
  if (M5.BtnC.wasReleased()) {
    input_button(2, 0);
  }
  if (!is_debug_screen_mode && is_debug_screen()) {
    is_debug_screen_mode = true;
    is_display_dirty = 1;
  } else if (is_debug_screen_mode) {
    if (is_debug_screen_leaved()) {
      is_debug_screen_mode = false;
      is_display_dirty = 1;
    }
  }

}

void process_display() {
  if (state == STATE_SLEEP || state == STATE_WAKEUP) {
    return;
  } else if (is_debug_screen_mode) {
    if (is_display_dirty) {
      M5.Lcd.fillScreen(WHITE);
      M5.Lcd.setTextColor(~GREEN);
      char msg[64];
      sprintf(msg, "Sleep by no action: %d min", deep_sleep_timeout_no_action / 60 / 1000);
      M5.Lcd.drawCentreString(msg, 0, 0, 4);
      sprintf(msg, "Wakeup interval: %d min", deep_sleep_wakeup_timeout / 60 / 1000);
      M5.Lcd.drawCentreString(msg, 0, 50, 4);

      is_display_dirty = 0;
    }
  } else if (is_display_dirty) {
    M5.Lcd.fillScreen(WHITE);

    M5.Lcd.setTextColor(~ORANGE);
    M5.Lcd.drawCentreString("Please input password:", 0, 0, 4);

    const uint16_t x_span = (SCREEN_WIDTH / PASSWORD_SIZE);
    const uint16_t center_y = SCREEN_HEIGHT / 2;
    const uint16_t char_y = center_y - 60;
    const uint16_t cursor_y = center_y - 20;
    for (uint16_t i=0; i < PASSWORD_SIZE; ++i) {
      const uint16_t x = i * x_span;
      M5.Lcd.drawChar(x, char_y, input_state.password[i], BLACK, WHITE, 4);
    }
    switch (state) {
      case STATE_SLEEP:
        break;
      case STATE_INPUT:
        {
          M5.Lcd.fillRect(input_state.pos * x_span, cursor_y, x_span - 12, 4, BLACK);
        }
        break;
      case STATE_AUTHORIZING:
        {
          M5.Lcd.setTextColor(~GREEN);
          M5.Lcd.drawCentreString("Authrozing...", 0, 200, 4);
        }
        break;
      case STATE_AUTHORIZED:
        {
          M5.Lcd.setTextColor(~BLUE);
          M5.Lcd.drawCentreString("Authrozed!", 0, 200, 4);
        }
        break;
      case STATE_AUTH_FAILED:
        {
          M5.Lcd.setTextColor(~RED);
          M5.Lcd.drawCentreString("Authrozation Failed", 0, 200, 4);
        }
        break;
      case STATE_NETWORK_TIMEOUT:
        {
          M5.Lcd.setTextColor(~RED);
          M5.Lcd.drawCentreString("Network Failed", 0, 200, 4);
        }
        break;
      case STATE_CLOUD_NOT_CONNECTED:
        {
          M5.Lcd.setTextColor(~RED);
          M5.Lcd.drawCentreString("Cloud Connection Failed", 0, 200, 4);
        }
        break;
      default:
        break;
    }

    M5.Lcd.setTextColor(BLACK);

    is_display_dirty = 0;
  }
}

void process_m5() {
  M5.update();
  m5_timer.run();
  process_button();
  process_display();
}

void process_obniz() {
}

// M5Stack Face {{{
#define KEYBOARD_I2C_ADDR     0X08
#define KEYBOARD_INT          5 // ピンが被ってる!

void setup_face() {
  Wire.begin();
  pinMode(KEYBOARD_INT, INPUT_PULLUP);
  pinMode(GATE_EN_PIN, OUTPUT);
  pinMode(FUNC_SELECT_PIN, OUTPUT);
  digitalWrite(GATE_EN_PIN, LOW);
  digitalWrite(FUNC_SELECT_PIN, LOW);
}

void input_key_from_face(char key) {
  if (state != STATE_INPUT) { return; }
  last_action_msec = millis();

  switch (key) {
    case '=': // Enter
      if (input_state.pos == (CURSOR_POS_SIZE - 1)) {
        input_ok();
      }
      break;
    case '+': // next
      input_state.pos = (input_state.pos + 1) % CURSOR_POS_SIZE;
      if (isEndPos()) { input_ok(); }
      is_display_dirty = 1;
      break;
    case '-': // prev
      input_state.pos = (input_state.pos + CURSOR_POS_SIZE - 1) % CURSOR_POS_SIZE;
      if (isEndPos()) { input_ok(); }
      is_display_dirty = 1;
      break;
    default:
      if (input_state.pos == (CURSOR_POS_SIZE - 1)) {
        return;
      }
      if ('0' <= key && key <= '9') {
        input_state.password[input_state.pos] = key;
        if (input_state.pos < (CURSOR_POS_SIZE-1)) {
          input_state.pos += 1;
        }
        if (isEndPos()) { input_ok(); }
        is_display_dirty = 1;
      }
      break;
  }
}

void process_face() {
  if(digitalRead(KEYBOARD_INT) == LOW) {
    Wire.requestFrom(KEYBOARD_I2C_ADDR, 1);  // request 1 byte from keyboard
    while (Wire.available()) { 
      uint8_t key_val = Wire.read();                  // receive a byte as character
      if(key_val != 0) {
        if(key_val >= 0x20 && key_val < 0x7F) { // ASCII String
          Serial.print((char)key_val);
          //M5.Lcd.print((char)key_val);
          input_key_from_face((char)key_val);
        } else {
          Serial.printf("0x%02X ",key_val);
          //M5.Lcd.printf("0x%02X ",key_val);
        }
      }
    }
  }
}
// M5Stack Face }}}

void check_timeout() {
  if ((millis() - last_action_msec) > deep_sleep_timeout_no_action) {
    last_action_msec = millis();
    if (state != STATE_INPUT) {
      return;
    }
    if (is_update_mode) {
      reset_input_state();
      // re-check
      is_update_mode = 0;
      check_update();
      is_display_dirty = 1;
    } else {
      sleep_until_btn_down();
    }
  }
}

void on_wakeup() {
  Serial.println("WAKEUP!");
  is_update_mode = 0;
  change_state(STATE_WAKEUP);
  print_system_msg("Please wait...");
}

void setup() {
  Serial.begin(115200);

  setup_input();
  setup_m5();
  setup_obniz();
  setup_face();

  change_state(STATE_WAKEUP);

  last_action_msec = millis();
}
void loop() {
  if (is_deep_sleeping) {
    is_deep_sleeping = 0;
    on_wakeup();
    is_display_dirty = 1;
  }

  process_m5();
  process_obniz();
  process_face();
  check_timeout();
}
