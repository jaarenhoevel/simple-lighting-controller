#include <Arduino.h>
#include <FastLED.h>
#include <ESPDMX.h>

#define OUT

#define BEAT_BUTTON_PIN       D1
#define BEAT_LED_PIN          D5

#define DMX_OUTPUT_PIN        D4
#define DMX_UNIVERSE_SIZE     255

#define FRAMES_PER_SECOND     40

#define BEAT_TAPS             10
#define BEAT_TAP_DURATION     5000

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

uint32_t last_beat = 0;
uint16_t beat_duration = 500; // 1bps ^= 60bpm

uint32_t last_taps[BEAT_TAPS];

uint8_t gDimmer = 255;
uint8_t gStrobe = 0;
uint8_t gHue = 0;

CRGB lights[LIGHT_COUNT];
CRGB base_color = CRGB::Red;

DMXESPSerial dmx;

typedef void (*SimplePatternList[])();
SimplePatternList gPatternsSync = {confetti};
SimplePatternList gPatternsStatic = {rainbow};

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

void write_dmx_frame(CRGB* lights) {
  for (uint8_t i = 0; i < LIGHT_COUNT; i ++) {
    uint8_t start_channel = (i * LIGHT_CHANNEL_SPACING) + LIGHT_FIRST_CHANNEL;

    uint8_t r, g, b, w, a, u;
    crgb_to_rgbwau(lights[i], &r, &g, &b, &w, &a, &u);

    dmx.write(start_channel + LIGHT_CHANNEL_DIMMER, gDimmer);
    dmx.write(start_channel + LIGHT_CHANNEL_STROBE, gStrobe);

    dmx.write(start_channel + LIGHT_CHANNEL_RED, r);
    dmx.write(start_channel + LIGHT_CHANNEL_GREEN, g);
    dmx.write(start_channel + LIGHT_CHANNEL_BLUE, b);
    dmx.write(start_channel + LIGHT_CHANNEL_WHITE, w);
    dmx.write(start_channel + LIGHT_CHANNEL_AMBER, a);
    dmx.write(start_channel + LIGHT_CHANNEL_UV, u);
  }

  dmx.update();
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
  }
}


void setup() {
  pinMode(BEAT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BEAT_LED_PIN, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(BEAT_BUTTON_PIN), handle_beat_button, FALLING);

  Serial.begin(115200);

  dmx.init(DMX_UNIVERSE_SIZE);
}

void loop() {
  FastLED.delay(1000 / FRAMES_PER_SECOND);

  EVERY_N_MILLISECONDS(1) {
    // handle beat
    if (millis() > last_beat + beat_duration) { // beat due
      last_beat = millis();
    }    
  }

  gHue += 2;

  analogWrite(BEAT_LED_PIN, ((beat_duration - (millis() - last_beat)) / (beat_duration * 1.f)) * 255); // fade beat led in sync

  confetti();

  write_dmx_frame(lights);
}

void rainbow() {
  fill_rainbow(lights, LIGHT_COUNT, gHue, 15);
}

void confetti(bool beat) {
  // random colored speckles that blink in and fade smoothly

  if (beat) {
    int pos = random16(LIGHT_COUNT);
    lights[pos] += CHSV( gHue + random8(64), 255, 255);
    return;
  }

  fadeToBlackBy(lights, LIGHT_COUNT, 10);
}
