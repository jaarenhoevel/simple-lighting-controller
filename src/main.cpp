#include <Arduino.h>
#include <FastLED.h>
#include <ESPDMX.h>

#include <ESP8266WiFi.h>
#include <espnow.h>

#define OUT

#define BEAT_BUTTON_PIN       D1
#define BEAT_LED_PIN          D5

#define EFFECT_STATIC_PIN     D6
#define EFFECT_SYNC_PIN       D7

#define BLACKOUT_PIN          D2
#define BEAT_PAUSE_PIN        D3

#define UV_DURING_BEAT_PAUSE  true

#define ESP_NOW_ENABLED       true

#define STROBE_PIN            D0

#define DIMMER_PIN            A0

#define DMX_OUTPUT_PIN        D4 // unchangeable
#define DMX_UNIVERSE_SIZE     255

#define STROBE_SPEED          250

#define FRAMES_PER_SECOND     30 // max 44 fps at full dmx frame

#define BEAT_TAPS             10
#define BEAT_TAP_DURATION     5000

#define PATTERN_SWITCH_TIME   30000 // 20s

#define LIGHT_COUNT           4

#define LIGHT_CHANNEL_DIMMER  0
#define LIGHT_CHANNEL_STROBE  7

#define LIGHT_CHANNEL_RED     1
#define LIGHT_CHANNEL_GREEN   2
#define LIGHT_CHANNEL_BLUE    3
#define LIGHT_CHANNEL_WHITE   4
#define LIGHT_CHANNEL_AMBER   5
#define LIGHT_CHANNEL_UV      6

#define LIGHT_CHANNEL_SPACING 10
#define LIGHT_FIRST_CHANNEL   1

#define LIGHT_LAST_CHANNEL    (LIGHT_COUNT * LIGHT_CHANNEL_SPACING) + LIGHT_FIRST_CHANNEL + LIGHT_CHANNEL_STROBE
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))


uint32_t last_beat = 0;
uint16_t beat_duration = 500; // 2bps ^= 120bpm
accum88 g_bpm = (uint16_t) ((30000.f / beat_duration) * 256.f);

bool g_beat_due = false;

uint32_t last_taps[BEAT_TAPS];

uint8_t g_dimmer = 255;
uint8_t g_strobe = 0;
uint8_t g_hue = 0;

uint8_t g_static_effect = false;
uint8_t g_effect_rotation = true;
uint8_t g_blackout = false;
uint8_t g_beat_paused = false;

CRGB lights[LIGHT_COUNT];
CRGB base_color = CRGB::Red;

DMXESPSerial dmx;

uint8_t broadcast_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
//uint8_t broadcast_address[] = {0xD8, 0xBF, 0xC0, 0x14, 0x75, 0x72};
uint8_t dmx_buffer[LIGHT_LAST_CHANNEL + 1];

void juggle();
void rainbow();
void sinelon();
void confetti();
void chase();
void blinder();
void wave();

typedef void (*SimplePatternList[])();
SimplePatternList g_patterns_sync = {confetti, sinelon, chase, blinder, wave};
SimplePatternList g_patterns_static = {rainbow, juggle};

uint8_t g_current_sync_pattern = 0;
uint8_t g_current_static_pattern = 0;

uint32_t g_last_pattern_switch = 0;

uint32_t g_effect_var_a, g_effect_var_b, g_effect_var_c = 0;


void next_pattern() {
  // New random color...
  base_color = CHSV(random8(255), 255, 255);
  
  // add one to the current pattern number, and wrap around at the end
  g_current_sync_pattern = (g_current_sync_pattern + 1) % ARRAY_SIZE( g_patterns_sync);
  g_current_static_pattern = (g_current_static_pattern + 1) % ARRAY_SIZE( g_patterns_static);

  g_effect_var_a = 0; // Reset effect vars
  g_effect_var_b = 0; // Reset effect vars
  g_effect_var_c = 0; // Reset effect vars

  Serial.println("NEXT Pattern!");
}

void crgb_to_rgbwau(CRGB color, OUT uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* w, uint8_t* a, uint8_t* u) {
  // Default channels
  *r = color.r;
  *g = color.g;
  *b = color.b;

  // Very basic upmix to 6 channels
  *w = min(color.r, min(color.g, color.b)); // white is set to the same level as the darkest channel
  *a = min(color.r, color.g); // amber is the mixture of red and green
  *u = min(color.r, color.b); // uv is activated when blue and red are active
} 

void setDmx(uint8_t channel, uint8_t value) {
  dmx.write(channel, value);
  dmx_buffer[channel] = value;
}

void sendDmx() {
  dmx.update();
  if (ESP_NOW_ENABLED) esp_now_send(broadcast_address, dmx_buffer, sizeof(dmx_buffer));
}

void write_dmx_frame(CRGB* lights) {
  for (uint8_t i = 0; i < LIGHT_COUNT; i ++) {
    uint8_t start_channel = (i * LIGHT_CHANNEL_SPACING) + LIGHT_FIRST_CHANNEL;

    uint8_t r, g, b, w, a, u;
    crgb_to_rgbwau(lights[i], &r, &g, &b, &w, &a, &u);

    setDmx(start_channel + LIGHT_CHANNEL_DIMMER, (g_blackout) ? 0 : ((g_strobe) ? 255 : g_dimmer));
    setDmx(start_channel + LIGHT_CHANNEL_STROBE, g_strobe);

    setDmx(start_channel + LIGHT_CHANNEL_RED, (g_strobe) ? 255 : r);
    setDmx(start_channel + LIGHT_CHANNEL_GREEN, (g_strobe) ? 255 : g);
    setDmx(start_channel + LIGHT_CHANNEL_BLUE, (g_strobe) ? 255 : b);
    setDmx(start_channel + LIGHT_CHANNEL_WHITE, (g_strobe) ? 255 : w);
    setDmx(start_channel + LIGHT_CHANNEL_AMBER, (g_strobe) ? 255 : a);
    setDmx(start_channel + LIGHT_CHANNEL_UV, (g_strobe) ? 255 : u);

    if (UV_DURING_BEAT_PAUSE && g_beat_paused) {
      setDmx(start_channel + LIGHT_CHANNEL_DIMMER, (g_strobe) ? 255 : g_dimmer);
      setDmx(start_channel + LIGHT_CHANNEL_STROBE, g_strobe);

      setDmx(start_channel + LIGHT_CHANNEL_RED, 0);
      setDmx(start_channel + LIGHT_CHANNEL_GREEN, 0);
      setDmx(start_channel + LIGHT_CHANNEL_BLUE, 0);
      setDmx(start_channel + LIGHT_CHANNEL_WHITE, 0);
      setDmx(start_channel + LIGHT_CHANNEL_AMBER, 0);
      setDmx(start_channel + LIGHT_CHANNEL_UV, 255);
    }
  }

  sendDmx();
}

void check_button_status() {
  g_strobe = !digitalRead(STROBE_PIN) * STROBE_SPEED;
  g_blackout = !digitalRead(BLACKOUT_PIN);
  g_beat_paused = !digitalRead(BEAT_PAUSE_PIN);

  if (!g_effect_rotation && (!digitalRead(EFFECT_STATIC_PIN) || !digitalRead(EFFECT_SYNC_PIN))) {
    next_pattern();

    g_static_effect = !digitalRead(EFFECT_STATIC_PIN);
  }

  g_effect_rotation = !(digitalRead(EFFECT_STATIC_PIN) && digitalRead(EFFECT_SYNC_PIN));

  if (analogRead(DIMMER_PIN) < 10) {
    g_dimmer = 0;    
  } else {
    g_dimmer = map(analogRead(DIMMER_PIN), 10, 1024, 0, 255);    
  }
}

ICACHE_RAM_ATTR void handle_beat_button() {
  // debounce
  if (last_taps[0] + 250 > millis()) return;
  
  Serial.println("Beat Button pressed!");
  last_beat = millis() - beat_duration; // beat now!

  // shift last taps array to the right
  for (uint8_t i = BEAT_TAPS - 1; i > 0; i --) {
    last_taps[i] = last_taps[i - 1];
  }

  // add current tap to array
  last_taps[0] = millis();

  // check if new beat duration should be calculated
  uint64_t temp_duration = 0;
  uint8_t taps_to_consider = 0;
  
  for (uint8_t i = 1; i < BEAT_TAPS; i ++)  {
    if (last_taps[i] > last_taps[0] - BEAT_TAP_DURATION) {
      temp_duration += last_taps[i - 1] - last_taps[i];
      taps_to_consider ++;

      Serial.println(last_taps[i - 1] - last_taps[i]);
    } else {
      break;
    }
  }

  if (taps_to_consider > 0) {
    // calculating new beat duration
    beat_duration = temp_duration / taps_to_consider;
    Serial.print("New beat duration: ");
    Serial.print(beat_duration);
    Serial.print(" Considered Taps: ");
    Serial.println(taps_to_consider);

    Serial.printf("BPM: %f\n", 60000.f / beat_duration);

    g_bpm = (uint16_t) ((30000.f / beat_duration) * 256.f); // Yeaaaaah
  }
}


void setup() {
  pinMode(BEAT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(EFFECT_STATIC_PIN, INPUT_PULLUP);
  pinMode(EFFECT_SYNC_PIN, INPUT_PULLUP);
  pinMode(BLACKOUT_PIN, INPUT_PULLUP);
  pinMode(BEAT_PAUSE_PIN, INPUT_PULLUP);
  pinMode(STROBE_PIN, INPUT_PULLUP);

  pinMode(BEAT_LED_PIN, OUTPUT);

  pinMode(DIMMER_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(BEAT_BUTTON_PIN), handle_beat_button, FALLING);

  Serial.begin(115200);

  Serial.println();
  Serial.print("MAC-Address: ");
  Serial.println(WiFi.macAddress());

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    while (1);
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(broadcast_address, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  dmx.init(DMX_UNIVERSE_SIZE);
}

void loop() {
  FastLED.delay(1000 / FRAMES_PER_SECOND);

  check_button_status();

  if (g_effect_rotation && millis() > g_last_pattern_switch + PATTERN_SWITCH_TIME) {
    g_last_pattern_switch = millis();
    next_pattern();
  }

  EVERY_N_MILLISECONDS(1) {
    // handle beat
    if (millis() > last_beat + beat_duration) { // beat due
      last_beat += beat_duration;
      
      g_beat_due = !g_beat_paused;

      // run effect on beat
      if (!g_static_effect) {
        g_patterns_sync[g_current_sync_pattern]();
      }

      g_beat_due = false;
    }    
  }

  EVERY_N_SECONDS(1) {
    Serial.print("SYNC Pattern ID: ");
    Serial.print(g_current_sync_pattern);
    Serial.print(" STATIC Pattern: ");
    Serial.print(g_static_effect);
    Serial.print(" STROBE: ");
    Serial.print(g_strobe);
    Serial.print(" BEAT Paused: ");
    Serial.print(g_beat_paused);
    Serial.print(" BLACKOUT: ");
    Serial.print(g_blackout);
    Serial.print(" DIMMER: ");
    Serial.print(g_dimmer);
    Serial.print(" EFFECT Rotation on: ");
    Serial.println(g_effect_rotation);
  }

  g_hue += 2;

  if (g_static_effect) {
    g_patterns_static[g_current_static_pattern]();
  } else {
    g_patterns_sync[g_current_sync_pattern]();
  }
  

  write_dmx_frame(lights);
  analogWrite(BEAT_LED_PIN, ((beat_duration - (millis() - last_beat)) / (beat_duration * 1.f)) * 255); // fade beat led in sync
}

// EFFECT SECTION //

void rainbow() {
  fill_rainbow(lights, LIGHT_COUNT, g_hue, 15);
}

void confetti() {
  // random colored speckles that blink in and fade smoothly

  if (g_beat_due) {
    int pos = random16(LIGHT_COUNT);
    lights[pos] += CHSV( g_hue + random8(64), 255, 255);
    return;
  }

  fadeToBlackBy(lights, LIGHT_COUNT, 10);
}

void sinelon() {
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(lights, LIGHT_COUNT, 20);
  int pos = beatsin16( g_bpm, 0, LIGHT_COUNT-1, last_taps[0] );
  lights[pos] += CHSV( g_hue, 255, 192);
}

void juggle() {
  // two colored dots, weaving in and out of sync with each other
  fadeToBlackBy( lights, LIGHT_COUNT, 20);
  byte dothue = 0;
  for( int i = 0; i < 2; i++) {
    lights[beatsin16( i+7, 0, LIGHT_COUNT-1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

void chase() {
  if (g_beat_due) {
    lights[g_effect_var_a] += CHSV( g_hue, 255, 255);
    g_effect_var_a ++;
    if (g_effect_var_a >= LIGHT_COUNT) g_effect_var_a = 0;
    return;
  }
  
  fadeToBlackBy(lights, LIGHT_COUNT, 10);  
}

void blinder() {
  if (g_beat_due) {
    for (uint8_t i = 0; i < LIGHT_COUNT; i++) {
      if (i % 2 == g_effect_var_a) lights[i] = ColorTemperature::Candle;
    }
    g_effect_var_a = (g_effect_var_a == 0) ? 1 : 0;
    return;
  }
  
  fadeUsingColor(lights, LIGHT_COUNT, CRGB(220, 200, 200));
}

void wave() {
  for (uint8_t i = 0; i < LIGHT_COUNT; i ++ ) {
    CRGB color = base_color;
    lights[i] = color.fadeToBlackBy(beatsin8(g_bpm, 0, 255, last_taps[0], (255 / (LIGHT_COUNT + 1)) * i));
  }
}
