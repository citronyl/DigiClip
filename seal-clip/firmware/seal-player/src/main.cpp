#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <util/delay.h>

// 8-bit PWM: PER = 255 -> 10 MHz / 256 = 39,062.5 Hz carrier
// One sample every 5 PWM cycles -> 7,812.5 samples per second
#define PWM_TOP        255
#define PWM_MID        128          // 50% duty = silence
#define CYCLES_PER_SEC 39063UL      // waitOverflows(CYCLES_PER_SEC) = ~1 s
#define CYCLES_SETTLE  3906         // ~0.1 s
#define CYCLES_SAMPLE  5            // 7,812.5 Hz

#define VOLUME        8             // 0..8
#define TOUCH_ON      400
#define TOUCH_OFF     200
#define LOCKOUT_WAKES 8             // 8 x 125 ms = 1 s (RTC, unaffected by the PWM)
#define MAX_CLIPS     32

#define F_CS   PIN4_bm              // PA4
#define F_SCK  PIN3_bm              // PA3
#define F_MOSI PIN6_bm              // PA6
#define F_MISO PIN5_bm              // PA5

uint16_t baseline;
uint8_t  clipCount = 0;
uint32_t clipStart[MAX_CLIPS], clipLen[MAX_CLIPS];
uint16_t rng = 1;

ISR(RTC_PIT_vect) { RTC.PITINTFLAGS = RTC_PI_bm; }

// ---------- timing ----------
void waitOverflows(uint32_t n) {                // one count = one PWM cycle (25.6 µs)
  while (n--) {
    while (!(TCA0.SINGLE.INTFLAGS & TCA_SINGLE_OVF_bm)) {}
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
  }
}

// ---------- flash ----------
void csLow()  { PORTA.OUTCLR = F_CS; }
void csHigh() { PORTA.OUTSET = F_CS; }

uint8_t spi(uint8_t out) {
  uint8_t in = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (out & 0x80) PORTA.OUTSET = F_MOSI; else PORTA.OUTCLR = F_MOSI;
    out <<= 1;
    PORTA.OUTSET = F_SCK;
    in <<= 1;
    if (PORTA.IN & F_MISO) in |= 1;
    PORTA.OUTCLR = F_SCK;
  }
  return in;
}

void sendAddr(uint32_t a) { spi(a >> 16); spi(a >> 8); spi(a); }

void flashWake() {
  PORTA.PIN5CTRL = PORT_ISC_INTDISABLE_gc;      // MISO input buffer on
  csLow(); spi(0xAB); csHigh();
  _delay_us(10);
}

void flashSleep() {
  csLow(); spi(0xB9); csHigh();                 // deep power-down, ~1 µA
  PORTA.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;   // MISO buffer off
}

uint32_t read32() {
  uint32_t v = spi(0);
  v |= (uint32_t)spi(0) << 8;
  v |= (uint32_t)spi(0) << 16;
  v |= (uint32_t)spi(0) << 24;
  return v;
}

void readTable() {
  flashWake();
  csLow(); spi(0x03); sendAddr(0);
  char sig[4];
  for (uint8_t i = 0; i < 4; i++) sig[i] = spi(0);
  uint16_t n = spi(0); n |= (uint16_t)spi(0) << 8;
  if (sig[0] == 'S' && sig[1] == 'E' && sig[2] == 'A' && sig[3] == 'L' && n >= 1 && n <= MAX_CLIPS) {
    clipCount = n;
    for (uint8_t k = 0; k < n; k++) { clipStart[k] = read32(); clipLen[k] = read32(); }
  }
  csHigh();
  flashSleep();
}

// ---------- touch ----------
uint16_t touchOnce() {
  analogRead(PIN_PB1);                          // empty the ADC sample cap
  PORTA.OUTSET = PIN7_bm; PORTA.DIRSET = PIN7_bm;   // charge the pad
  _delay_us(10);
  PORTA.DIRCLR = PIN7_bm;                       // release and measure
  uint16_t v = analogRead(PIN_PA7);
  PORTA.OUTCLR = PIN7_bm; PORTA.DIRSET = PIN7_bm;   // park at 0 V
  return v;
}

uint16_t touchRead(uint8_t n) {
  ADC0.CTRLA |= ADC_ENABLE_bm;
  _delay_us(50);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += touchOnce();
  ADC0.CTRLA &= ~ADC_ENABLE_bm;
  return sum / n;
}

// ---------- random pick ----------
uint16_t rnd() {
  rng ^= rng << 7; rng ^= rng >> 9; rng ^= rng << 8;
  return rng;
}

uint8_t pickClip() {
  static uint8_t last = 255;
  if (clipCount == 1) return 0;
  uint8_t k;
  do { k = rnd() % clipCount; } while (k == last);
  last = k;
  return k;
}

// ---------- playback ----------
void playClip(uint8_t k) {
  cli();
  flashWake();
  csLow(); spi(0x03); sendAddr(clipStart[k]);
  digitalWrite(PIN_PA1, HIGH);                  // amp on
  waitOverflows(CYCLES_SETTLE);
  for (uint32_t i = 0; i < clipLen[k]; i++) {
    int16_t v = (int16_t)spi(0) - 128;          // 0..255 -> -128..127
    v = (v * VOLUME) / 8;
    v = PWM_MID + v;                            // full 8 bits, no halving
    if (v < 0)       v = 0;
    if (v > PWM_TOP) v = PWM_TOP;
    TCA0.SINGLE.CMP0BUF = (uint8_t)v;
    waitOverflows(CYCLES_SAMPLE);
  }
  csHigh();
  TCA0.SINGLE.CMP0BUF = PWM_MID;
  waitOverflows(CYCLES_SETTLE);
  digitalWrite(PIN_PA1, LOW);                   // amp off
  flashSleep();
  sei();
}

void sleepOnce() { sei(); sleep_cpu(); }

void setup() {
  cli();
  pinMode(PIN_PA1, OUTPUT); digitalWrite(PIN_PA1, LOW);
  pinMode(PIN_PB1, OUTPUT); digitalWrite(PIN_PB1, LOW);
  PORTB.DIRSET = PIN3_bm;
  PORTA.OUTSET = F_CS;
  PORTA.OUTCLR = F_SCK | F_MOSI;
  PORTA.DIRSET = F_CS | F_SCK | F_MOSI;
  PORTA.DIRCLR = F_MISO;
  PORTA.OUTCLR = PIN7_bm; PORTA.DIRSET = PIN7_bm;

  PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTB.PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTB.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;

  analogReadResolution(12);

  takeOverTCA0();
  PORTMUX.TCAROUTEA |= 0x01;                    // WO0 -> PB3
  TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc | TCA_SINGLE_CMP0EN_bm;
  TCA0.SINGLE.PER  = PWM_TOP;
  TCA0.SINGLE.CMP0 = PWM_MID;
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;

  readTable();

  uint16_t boots = eeprom_read_word((uint16_t*)0) + 1;
  eeprom_update_word((uint16_t*)0, boots);

  waitOverflows(CYCLES_PER_SEC);                // 1 s hands off
  baseline = touchRead(16);
  rng = (boots * 2654435761UL >> 16) ^ baseline;
  if (rng == 0) rng = 1;

  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
  while (RTC.PITSTATUS > 0) {}
  RTC.PITINTCTRL = RTC_PI_bm;
  RTC.PITCTRLA = RTC_PERIOD_CYC4096_gc | RTC_PITEN_bm;   // wake every 125 ms

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
}

void loop() {
  static bool armed = true;

  sleepOnce();
  uint16_t r = touchRead(4);
  rng ^= r;
  if (rng == 0) rng = 1;

  if (!armed) {
    if (r < baseline + TOUCH_OFF) armed = true;
    return;
  }

  if (r > baseline + TOUCH_ON && clipCount > 0) {
    _delay_ms(5); if (touchRead(4) <= baseline + TOUCH_ON) return;
    _delay_ms(5); if (touchRead(4) <= baseline + TOUCH_ON) return;
    playClip(pickClip());
    for (uint8_t i = 0; i < LOCKOUT_WAKES; i++) sleepOnce();
    armed = false;
  } else if (r < baseline + TOUCH_OFF) {
    baseline = (baseline * 15UL + r) / 16;
  }
}