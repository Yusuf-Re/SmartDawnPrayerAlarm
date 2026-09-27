#include <Wire.h>
#include <RTClib.h>
#include <PrayerTimes.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>


// =====================================================
// OLED
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);


// =====================================================
// RTC
// =====================================================

RTC_DS1307 rtc;


// =====================================================
// LOCATION - MANCHESTER, UK
// MUSLIM WORLD LEAGUE
// =====================================================

const float latitude = 53.4808;
const float longitude = -2.2426;

PrayerTimes pt(latitude, longitude, 0);


// =====================================================
// PINS
// =====================================================

// RGB LED
const int redPin = 9;
const int greenPin = 10;
const int bluePin = 11;

// Passive buzzer
const int buzzerPin = 8;

// Stop button
const int buttonPin = 7;

// Alarm mode potentiometer
const int potPin = A0;


// =====================================================
// TEST MODE
// =====================================================

// false = real Manchester MWL Fajr
// true  = sunrise starts immediately
//         and fake Fajr occurs after 1 minute

const bool TEST_MODE = false;

bool testStarted = false;
unsigned long testStartMillis = 0;


// =====================================================
// LIGHT SETTINGS
// =====================================================

// Warm amber / incandescent colour
const int MAX_RED = 80;
const int MAX_GREEN = 5;
const int MAX_BLUE = 0;


// Sunrise lasts 60 seconds
const int SUNRISE_DURATION = 60;


// One complete breathing pulse = 6 seconds
const unsigned long PULSE_TIME = 6000;


// Lowest point of pulse
//
// 255 = no pulse
// 140 = about 55% brightness
//
// Therefore:
//
// medium-bright -> bright -> medium-bright

const int PULSE_MIN = 140;


// =====================================================
// ALARM VARIABLES
// =====================================================

bool alarmRunning = false;
bool alarmSilenced = false;

unsigned long buzzerPreviousMillis = 0;

int buzzerStep = 0;
int previousAlarmMode = 0;


// Stops alarm triggering repeatedly on same day
long lastAlarmDate = -1;


// Detects a new date
long currentStateDate = -1;


// =====================================================
// FIND LAST SUNDAY OF MONTH
// =====================================================

int lastSunday(int year, int month) {

  int daysInMonth = 31;

  if (
    month == 4 ||
    month == 6 ||
    month == 9 ||
    month == 11
  ) {
    daysInMonth = 30;
  }


  if (month == 2) {

    bool leapYear =
      (year % 4 == 0 && year % 100 != 0) ||
      (year % 400 == 0);

    daysInMonth = leapYear ? 29 : 28;
  }


  DateTime lastDay(
    year,
    month,
    daysInMonth
  );


  return
    daysInMonth -
    lastDay.dayOfTheWeek();
}


// =====================================================
// AUTOMATIC UK GMT / BST
// =====================================================

int getUKOffset(
  int year,
  int month,
  int day
) {

  int marchSunday =
    lastSunday(year, 3);

  int octoberSunday =
    lastSunday(year, 10);


  // January / February = GMT
  if (month < 3) {
    return 0;
  }


  // November / December = GMT
  if (month > 10) {
    return 0;
  }


  // April - September = BST
  if (
    month > 3 &&
    month < 10
  ) {
    return 60;
  }


  // March
  if (month == 3) {

    if (day >= marchSunday) {
      return 60;
    }

    return 0;
  }


  // October
  if (month == 10) {

    if (day < octoberSunday) {
      return 60;
    }

    return 0;
  }


  return 0;
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  // RGB LED
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);


  // Buzzer
  pinMode(buzzerPin, OUTPUT);


  // Stop button
  pinMode(
    buttonPin,
    INPUT_PULLUP
  );


  // Start LED and buzzer OFF
  setRGB(0, 0, 0);
  noTone(buzzerPin);


  // OLED
  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C
    )
  ) {
    while (1);
  }


  // RTC
  if (!rtc.begin()) {

    display.clearDisplay();

    display.setTextColor(
      SSD1306_WHITE
    );

    display.setTextSize(2);

    display.setCursor(
      5,
      25
    );

    display.println(
      "RTC ERROR"
    );

    display.display();

    while (1);
  }


  // Muslim World League calculation
  pt.setCalculationMethod(
    CalculationMethods::MWL
  );
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  DateTime now =
    rtc.now();


  unsigned long currentMillis =
    millis();


  // ===================================================
  // TODAY'S DATE
  //
  // Example:
  // 23/09/2026 -> 20260923
  // ===================================================

  long todayDate =

    now.year() * 10000L +

    now.month() * 100L +

    now.day();


  // ===================================================
  // NEW DAY RESET
  // ===================================================

  if (
    todayDate !=
    currentStateDate
  ) {

    alarmRunning = false;
    alarmSilenced = false;

    buzzerStep = 0;

    noTone(buzzerPin);

    setRGB(
      0,
      0,
      0
    );


    currentStateDate =
      todayDate;


    testStarted = false;
  }


  // ===================================================
  // AUTOMATIC BST / GMT
  // ===================================================

  int utcOffset =
    getUKOffset(
      now.year(),
      now.month(),
      now.day()
    );


  // ===================================================
  // CALCULATE TODAY'S MWL FAJR
  // ===================================================

  PrayerTimesResult times =

    pt.calculateWithOffset(

      now.day(),

      now.month(),

      now.year(),

      utcOffset
    );


  if (!times.valid) {
    return;
  }


  int fajrHour;
  int fajrMinute;


  pt.minutesToTime(

    times.fajr,

    fajrHour,

    fajrMinute
  );


  // ===================================================
  // CURRENT TIME IN SECONDS
  // ===================================================

  long currentSeconds =

    now.hour() * 3600L +

    now.minute() * 60L +

    now.second();


  long realFajrSeconds =

    fajrHour * 3600L +

    fajrMinute * 60L;


  // ===================================================
  // ALARM MODE POTENTIOMETER
  // ===================================================

  int potValue =
    analogRead(potPin);


  int alarmMode;


  if (potValue <= 340) {

    alarmMode = 1; // CALM
  }

  else if (potValue <= 681) {

    alarmMode = 2; // MEDIUM
  }

  else {

    alarmMode = 3; // STRONG
  }


  // ===================================================
  // SUNRISE VARIABLES
  // ===================================================

  bool sunriseActive = false;

  long sunriseElapsed = 0;

  bool testFajrReached = false;


  // ===================================================
  // TEST MODE
  // ===================================================

  if (TEST_MODE) {


    // Start test timer once
    if (!testStarted) {

      testStarted = true;

      testStartMillis =
        currentMillis;
    }


    unsigned long testElapsed =

      currentMillis -
      testStartMillis;


    // First 60 seconds = sunrise
    if (
      testElapsed <
      60000UL
    ) {

      sunriseActive = true;


      sunriseElapsed =

        testElapsed /
        1000UL;


      if (
        sunriseElapsed >
        SUNRISE_DURATION - 1
      ) {

        sunriseElapsed =
          SUNRISE_DURATION - 1;
      }
    }


    // After 60 seconds = fake Fajr
    else {

      testFajrReached = true;
    }
  }


  // ===================================================
  // REAL FAJR MODE
  // ===================================================

  else {


    // Sunrise begins exactly 1 minute before Fajr

    long sunriseStart =

      realFajrSeconds -
      SUNRISE_DURATION;


    // Handle midnight
    if (
      sunriseStart < 0
    ) {

      sunriseStart +=
        86400;
    }


    // --------------------------------
    // Normal sunrise window
    // --------------------------------

    if (
      sunriseStart <
      realFajrSeconds
    ) {

      if (
        currentSeconds >= sunriseStart &&
        currentSeconds < realFajrSeconds
      ) {

        sunriseActive = true;


        sunriseElapsed =

          currentSeconds -
          sunriseStart;
      }
    }


    // --------------------------------
    // Sunrise crosses midnight
    // --------------------------------

    else {

      if (
        currentSeconds >= sunriseStart ||
        currentSeconds < realFajrSeconds
      ) {

        sunriseActive = true;


        if (
          currentSeconds >=
          sunriseStart
        ) {

          sunriseElapsed =

            currentSeconds -
            sunriseStart;
        }

        else {

          sunriseElapsed =

            (86400 - sunriseStart) +

            currentSeconds;
        }
      }
    }
  }


  // ===================================================
  // START ALARM
  // ===================================================

  if (TEST_MODE) {


    if (
      testFajrReached &&
      lastAlarmDate != todayDate
    ) {

      alarmRunning = true;

      alarmSilenced = false;


      lastAlarmDate =
        todayDate;


      buzzerStep = 0;

      buzzerPreviousMillis =
        currentMillis;
    }
  }


  else {


    // Trigger once when actual Fajr minute arrives

    if (
      now.hour() == fajrHour &&
      now.minute() == fajrMinute &&
      lastAlarmDate != todayDate
    ) {

      alarmRunning = true;

      alarmSilenced = false;


      lastAlarmDate =
        todayDate;


      buzzerStep = 0;

      buzzerPreviousMillis =
        currentMillis;
    }
  }


  // ===================================================
  // STOP BUTTON
  // ===================================================

  if (
    digitalRead(buttonPin) == LOW &&
    alarmRunning
  ) {

    alarmRunning = false;

    alarmSilenced = true;


    noTone(buzzerPin);


    buzzerStep = 0;
  }


  // ===================================================
  // 1-MINUTE WARM AMBER SUNRISE
  //
  // COMPLETELY OFF -> GRADUALLY BRIGHTER
  //
  // NO PULSING DURING THIS SECTION
  // ===================================================

  if (sunriseActive) {


    // 0 -> 255 over the 60-second sunrise

    int sunriseProgress =

      (
        sunriseElapsed *
        255L
      )

      /

      (
        SUNRISE_DURATION -
        1
      );


    sunriseProgress =
      constrain(
        sunriseProgress,
        0,
        255
      );


    // --------------------------------
    // RED
    //
    // Completely OFF at beginning
    // Gradually reaches MAX_RED
    // --------------------------------

    int red =

      map(

        sunriseProgress,

        0,

        255,

        0,

        MAX_RED
      );


    // --------------------------------
    // GREEN
    //
    // Gradually introduced to keep
    // the colour warm amber.
    // --------------------------------

    int green = 0;


    if (
      red >= 10 &&
      red < 30
    ) {

      green = 1;
    }


    else if (
      red >= 30 &&
      red < 50
    ) {

      green = 2;
    }


    else if (
      red >= 50 &&
      red < 65
    ) {

      green = 3;
    }


    else if (
      red >= 65 &&
      red < 75
    ) {

      green = 4;
    }


    else if (
      red >= 75
    ) {

      green = 5;
    }


    // --------------------------------
    // OUTPUT
    // --------------------------------

    setRGB(

      red,

      green,

      MAX_BLUE
    );
  }


  // ===================================================
  // AT / AFTER FAJR
  //
  // DEEP AMBER BREATHING PULSE
  //
  // Medium-bright -> bright -> medium-bright
  // ===================================================

  else if (

    lastAlarmDate ==
    todayDate &&

    (
      alarmRunning ||
      alarmSilenced
    )

  ) {

    updateWarmPulse();
  }


  // ===================================================
  // OTHERWISE LED OFF
  // ===================================================

  else {

    setRGB(
      0,
      0,
      0
    );
  }


  // ===================================================
  // BUZZER
  // ===================================================

  if (
    alarmRunning &&
    !alarmSilenced
  ) {

    updateAlarm(
      alarmMode
    );
  }

  else {

    noTone(
      buzzerPin
    );
  }


  // ===================================================
  // OLED
  // ===================================================

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );


  // -------------------------
  // TITLE
  // -------------------------

  display.setTextSize(1);

  display.setCursor(
    0,
    0
  );

  display.println(
    "FAJR ALARM"
  );


  // -------------------------
  // CURRENT TIME
  // -------------------------

  display.setTextSize(2);

  display.setCursor(
    10,
    14
  );


  if (
    now.hour() < 10
  ) {

    display.print("0");
  }


  display.print(
    now.hour()
  );


  display.print(":");


  if (
    now.minute() < 10
  ) {

    display.print("0");
  }


  display.print(
    now.minute()
  );


  display.print(":");


  if (
    now.second() < 10
  ) {

    display.print("0");
  }


  display.print(
    now.second()
  );


  // -------------------------
  // FAJR TIME
  // -------------------------

  display.setTextSize(1);

  display.setCursor(
    0,
    37
  );


  display.print(
    "Fajr: "
  );


  if (
    fajrHour < 10
  ) {

    display.print("0");
  }


  display.print(
    fajrHour
  );


  display.print(":");


  if (
    fajrMinute < 10
  ) {

    display.print("0");
  }


  display.print(
    fajrMinute
  );


  // -------------------------
  // ALARM MODE
  // -------------------------

  display.setCursor(
    0,
    48
  );


  display.print(
    "Mode: "
  );


  if (
    alarmMode == 1
  ) {

    display.print(
      "CALM"
    );
  }


  else if (
    alarmMode == 2
  ) {

    display.print(
      "MEDIUM"
    );
  }


  else {

    display.print(
      "STRONG"
    );
  }


  // -------------------------
  // STATUS
  // -------------------------

  display.setCursor(
    0,
    57
  );


  if (
    alarmSilenced
  ) {

    display.print(
      "Alarm stopped"
    );
  }


  else if (
    alarmRunning
  ) {

    if (TEST_MODE) {

      display.print(
        "TEST ALARM!"
      );
    }

    else {

      display.print(
        "FAJR - ALARM!"
      );
    }
  }


  else if (
    sunriseActive
  ) {


    int percent =

      (
        sunriseElapsed *
        100L
      )

      /

      (
        SUNRISE_DURATION -
        1
      );


    percent =
      constrain(
        percent,
        0,
        100
      );


    if (TEST_MODE) {

      display.print(
        "Test light "
      );
    }

    else {

      display.print(
        "Sunrise "
      );
    }


    display.print(
      percent
    );


    display.print("%");
  }


  else {

    display.print(
      "Waiting..."
    );
  }


  display.display();
}


// =====================================================
// SET RGB LED
// =====================================================

void setRGB(
  int red,
  int green,
  int blue
) {

  analogWrite(
    redPin,
    red
  );


  analogWrite(
    greenPin,
    green
  );


  analogWrite(
    bluePin,
    blue
  );
}


// =====================================================
// DEEP WARM AMBER BREATHING PULSE
//
// Runs after Fajr.
//
// Smoothly moves:
//
// medium-bright
//      ↓
// bright
//      ↓
// medium-bright
//
// No flickering.
// =====================================================

void updateWarmPulse() {

  unsigned long pulsePosition =

    millis() %
    PULSE_TIME;


  // 0.0 -> 1.0

  float phase =

    (float)pulsePosition /

    (float)PULSE_TIME;


  // Smooth breathing curve
  //
  // 0 -> 1 -> 0

  float breathing =

    0.5 -

    0.5 *

    cos(
      phase *
      2.0 *
      PI
    );


  // Convert breathing curve
  // from PULSE_MIN -> 255

  float pulseScale =

    PULSE_MIN +

    (
      255 -
      PULSE_MIN
    )

    *

    breathing;


  // ===================================================
  // RED
  // ===================================================

  int red =

    (int)(

      MAX_RED *

      pulseScale /

      255.0
    );


  // ===================================================
  // GREEN
  //
  // Keep same amber colour progression
  // ===================================================

  int green = 0;


  if (
    red >= 10 &&
    red < 30
  ) {

    green = 1;
  }


  else if (
    red >= 30 &&
    red < 50
  ) {

    green = 2;
  }


  else if (
    red >= 50 &&
    red < 65
  ) {

    green = 3;
  }


  else if (
    red >= 65 &&
    red < 75
  ) {

    green = 4;
  }


  else if (
    red >= 75
  ) {

    green = 5;
  }


  // ===================================================
  // OUTPUT
  // ===================================================

  setRGB(

    red,

    green,

    MAX_BLUE
  );
}


// =====================================================
// NON-BLOCKING ALARM
// =====================================================

void updateAlarm(
  int alarmMode
) {

  unsigned long currentMillis =
    millis();


  // ===================================================
  // ALARM MODE CHANGED
  // ===================================================

  if (
    alarmMode !=
    previousAlarmMode
  ) {

    buzzerStep = 0;

    buzzerPreviousMillis =
      currentMillis;


    noTone(
      buzzerPin
    );


    previousAlarmMode =
      alarmMode;
  }


  // ===================================================
  // CALM
  // ===================================================

  if (
    alarmMode == 1
  ) {


    // Tone 1

    if (
      buzzerStep == 0
    ) {

      tone(
        buzzerPin,
        330
      );


      if (
        currentMillis -
        buzzerPreviousMillis
        >= 250
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 1;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    // Pause

    else if (
      buzzerStep == 1
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 700
      ) {

        tone(
          buzzerPin,
          392
        );


        buzzerStep = 2;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    // Tone 2

    else if (
      buzzerStep == 2
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 250
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 3;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    // Long pause

    else if (
      buzzerStep == 3
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 1200
      ) {

        buzzerStep = 0;

        buzzerPreviousMillis =
          currentMillis;
      }
    }
  }


  // ===================================================
  // MEDIUM
  // ===================================================

  else if (
    alarmMode == 2
  ) {


    if (
      buzzerStep == 0
    ) {

      tone(
        buzzerPin,
        330
      );


      if (
        currentMillis -
        buzzerPreviousMillis
        >= 300
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 1;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 1
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 300
      ) {

        tone(
          buzzerPin,
          392
        );


        buzzerStep = 2;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 2
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 300
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 3;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 3
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 300
      ) {

        tone(
          buzzerPin,
          494
        );


        buzzerStep = 4;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 4
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 400
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 5;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 5
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 800
      ) {

        buzzerStep = 0;

        buzzerPreviousMillis =
          currentMillis;
      }
    }
  }


  // ===================================================
  // STRONG
  // ===================================================

  else {


    if (
      buzzerStep == 0
    ) {

      tone(
        buzzerPin,
        700
      );


      if (
        currentMillis -
        buzzerPreviousMillis
        >= 180
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 1;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 1
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 120
      ) {

        tone(
          buzzerPin,
          900
        );


        buzzerStep = 2;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 2
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 180
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 3;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 3
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 120
      ) {

        tone(
          buzzerPin,
          1100
        );


        buzzerStep = 4;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 4
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 250
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 5;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 5
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 250
      ) {

        tone(
          buzzerPin,
          900
        );


        buzzerStep = 6;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 6
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 180
      ) {

        noTone(
          buzzerPin
        );


        buzzerStep = 7;

        buzzerPreviousMillis =
          currentMillis;
      }
    }


    else if (
      buzzerStep == 7
    ) {

      if (
        currentMillis -
        buzzerPreviousMillis
        >= 700
      ) {

        buzzerStep = 0;

        buzzerPreviousMillis =
          currentMillis;
      }
    }
  }
}