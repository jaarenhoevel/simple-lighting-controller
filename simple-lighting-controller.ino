#define BEAT_BUTTON_PIN D1
#define BEAT_LED_PIN D5

#define BEAT_TAPS 10
#define BEAT_TAP_DURATION 5000

uint32_t last_beat = 0;
uint16_t beat_duration = 500; // 1bps ^= 60bpm

uint32_t last_taps[BEAT_TAPS];

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
}

void loop() {
  // handle beat
  if (millis() > last_beat + beat_duration) { // beat due
    last_beat = millis();
  }

  analogWrite(BEAT_LED_PIN, ((beat_duration - (millis() - last_beat)) / (beat_duration * 1.f)) * 255); // fade beat led in sync
}
