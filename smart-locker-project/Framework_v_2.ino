#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_SSD1306.h>
#include <dht.h>
#include <ThingSpeak.h>
#include <WiFiEsp.h>
#include <Keypad.h>
#include <Adafruit_Fingerprint.h>



// Pin
// Temperateure & Humdity
#define DHT11_PIN 40
// Ultrasonic
#define trigPin 41
#define echoPin 2 //cannot be changed
// RFID
#define SS_PIN 53
#define RST_PIN 49
// LED
#define RED_LED_PIN 48
#define GREEN_LED_PIN 47
#define BLUE_LED_PIN 46
// Motor control pins
#define motorIN1Pin 45
#define motorIN2Pin 44
#define motorIN3Pin 43
#define motorIN4Pin 42
// Lightsensor
#define LightsensorPin A0
// Buzzer
#define buzzer 38 //buzzer to arduino pwn pin
// Fingerprint
SoftwareSerial mySerial(10, 11); // TX/RX on fingerprint sensor
// Keypad
byte pin_rows[4] = {13, 12, 9, 8};
byte pin_column[4] = {7, 6, 5, 4};




// Password
// Wifi password
char ssid[] = "EE3070_P1615_1";   //EE3070_P1615_1
char pass[] = "EE3070P1615";     //EE3070P1615
// RFID password
byte storedUID[2][4] = {
  {0xF1, 0x3C, 0x82, 0x19},
  {0xBC, 0x74, 0x1A, 0x30}
};
// Keypad password
String password = "";
const String correctPassword = "12345678";
String masked_password = "";
// ThinkSpeak
unsigned long myChannelNumber = 2315925;
const char * myWriteAPIKey = "B66AQC1B5H7758EU";
const char * myReadAPIKey = "CGTJDYPH6BL5TVIP";
int statusCode = 0;



//Setting
// Ultrasonic Setting
float uppestDistanceBound = 5; // larger than 5cm = the door opened
float lowestDistanceBound = 2.5;// smaller than 2.5cm = sensor error

// OLED Setting
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Wifi Setting
#define ESP_BAUDRATE 115200
WiFiEspClient client;

// DHT Setting
dht DHT;

// Buzzer Setting
bool alarmWarning = false; //buzzer warning when the door is opened abnormaly

// Keypad Setting
volatile boolean PasswordState;
char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, 4, 4);
char keyRead;

// Motor Setting
int mD1;
int mD2;
int mD3;
int mD4;
volatile int motorState;
volatile boolean enableLock = false;
bool door_opened = false;

// Light sensor Setting
int LightValue;
int LightWarningBound = 500; // Light > 500, warning

// Fingerprint Setting
volatile int finger_status = -1;
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// RFID Setting
MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;



//Variables
int door_value = 0;
// Ultrasonic distance sensor Variables
volatile long duration;
volatile float distance;
volatile int d_flag;
// Timer Variables
volatile unsigned long startTime;
volatile unsigned long PasswordTimerStart;
volatile unsigned long FingerTimerStart;
volatile unsigned long CloudTimerStart;
volatile unsigned long ModuleTimerStart;
volatile unsigned long TimeCur;
volatile unsigned long ReadCloudStart;
volatile unsigned long ReadCloudCur;


int autolock_count = 0;
int upload_count = 0;




void setup() {
  Serial.begin(115200);

  Serial1.begin(ESP_BAUDRATE);

  // Initialize ESP8266 module
  WiFi.init(&Serial1);

  // Connect to WiFi network
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);
    delay(5000);
  }
  Serial.println("Connected to WiFi");

  ThingSpeak.begin(client);  // Initialize ThingSpeak

  pinMode(buzzer, OUTPUT);
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT_PULLUP); // Sets the echoPin as an Input
  //attachInterrupt(digitalPinToInterrupt(2), echo_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(echoPin), interruptLogic, CHANGE);
  pinMode(motorIN1Pin, OUTPUT);
  pinMode(motorIN2Pin, OUTPUT);
  pinMode(motorIN3Pin, OUTPUT);
  pinMode(motorIN4Pin, OUTPUT);
  motorState = 0;

  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
  }

  finger.getTemplateCount();
  Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
  Serial.println("Waiting for valid finger...");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) {} // Infinite loop to halt execution
  }
  display.display();
  delay(1000);
  display.clearDisplay();

  SPI.begin();
  rfid.PCD_Init();

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  Serial.println(F("Waiting for card or password..."));
  displayMessage("Waiting for card or password...");

  FingerTimerStart = millis();
  CloudTimerStart = millis();
  ModuleTimerStart = millis();
  TimeCur = millis();
  ReadCloudStart = millis();
}



void loop() {
  if (not door_opened) {
    CheckRFID();
    CheckKeypad();
    CheckFingerprint();
    CheckCloud();
  }
  ultrasonicSensor();
  display_and_upload_All();
  if (enableLock&&(distance > lowestDistanceBound) && (distance < uppestDistanceBound)){
    autolock_count++;
    if (autolock_count==10){   //avoid false trigger
      doorClose();
      autolock_count = 0;
    }
  }
  else if (door_opened&&(distance > uppestDistanceBound)){
    autolock_count = 0;
    enableLock=true;
  }
  alarmsystem();
  delay(5);
}

void CheckFingerprint() {
  if(door_opened)
    return;
  TimeCur = millis();
  if ((TimeCur - FingerTimerStart) > 500) {
    FingerTimerStart = millis();
    finger_status = getFingerprintIDez();
    if (finger_status != -1 and finger_status != -2) {
      displayMessage("Correct ID!");
      password = "";  // Reset the password string
      doorOpen();
    } else {
      if (finger_status == -2) {
        setColor(255, 0, 0);  // Red
        tone(buzzer, 4000); // 1000 == Send 1KHz sound signal
        delay(500);  // Keep the LED on for 1 second
        setColor(0, 0, 0);  // Turn off LED
        noTone(buzzer);
      }
    }
  }
}

// returns -1 if failed, otherwise returns ID #
int getFingerprintIDez() {
  uint8_t p = finger.getImage();

  if (p != FINGERPRINT_OK)  return -1;

  p = finger.image2Tz();

  if (p != FINGERPRINT_OK)  return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK)  return -2;

  // found a match!
  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  Serial.print(" with confidence of "); Serial.println(finger.confidence);
  return finger.fingerID;
}

void CheckKeypad() {
  if(door_opened)
    return;
  TimeCur = millis();
  keyRead = keypad.getKey();
  if (keyRead) {
    tone(buzzer, 2000); // 1000 == Send 1KHz sound signal
//    display.print("*");
//    display.display();
    if (keyRead == '*') {
      PasswordState = false;
      Serial.println(F("\nClear!"));
      displayMessage("\nClear!");
      password = "";
      masked_password = "";
      delay(100);        // for 0.1 sec
      noTone(buzzer);
      return;
    }
    PasswordState = true;
    PasswordTimerStart = millis();
    password += keyRead;
    masked_password += '*';
    Serial.print(keyRead);
      delay(100);        // for 0.1 sec
      noTone(buzzer);
    if (password.length() == 8) {
      if (password == correctPassword) {
        Serial.println(F("\nCorrect Password!"));
        displayMessage("Correct Password!");
        doorOpen();
      }
      else {
        Serial.println(F("\nWrong Password!"));
        displayMessage("Wrong Password!");
        password = "";  // Reset the password string
        masked_password = "";
         tone(buzzer, 2000); // 1000 == Send 1KHz sound signal
  
        setColor(255, 0, 0);  // Red
        delay(1000);  // Keep the LED on for 1 second
        setColor(0, 0, 0);  // Turn off LED
        noTone(buzzer);
      }
    }
  } else if (((TimeCur - PasswordTimerStart) >= 10000) && PasswordState) {
    PasswordState = false;
    Serial.println(F("\nTime out!"));
    Serial.println(F("\nPlease input password again!"));
    displayMessage("Time out!\nPlease input password again!");
    password = "";
    masked_password = "";
  }
}

void CheckRFID() {
  if(door_opened)
    return;
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (isUIDMatch(rfid.uid.uidByte, storedUID, sizeof(storedUID) / sizeof(storedUID[0]))) {
      Serial.println("Correct Card!");
      displayMessage("Correct Card!");
      doorOpen();
    }
    else {
      Serial.println("Wrong Card!");
      displayMessage("Wrong Card!");
      setColor(255, 0, 0);  // Red
      delay(1000);  // Keep the LED on for 1 second
      setColor(0, 0, 0);  // Turn off LED
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}

void CheckCloud(){
  if((door_opened)||PasswordState)
    return;
  ReadCloudCur = millis();
  if (ReadCloudCur - ReadCloudStart >= 15000){
    ReadCloudStart = millis();
    statusCode = ThingSpeak.readMultipleFields(myChannelNumber);
    int open_request = ThingSpeak.readIntField(myChannelNumber, 6, myReadAPIKey);
    String createdAt = ThingSpeak.getCreatedAt();
    if (ThingSpeak.getLastReadStatus() == 200) { // Check if the read was successful
      Serial.print("Latest Value in Field 6: ");
      Serial.println(open_request);
    } else {
      Serial.println("Failed to read from ThingSpeak");
    }
    if (open_request == 1){
      doorOpen();
      ThingSpeak.setField(6, 0);
      int result = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
      Serial.print("open_request Upload Result: ");
      Serial.println(result);  // Print result for debugging
    }
      
  }
}

bool isUIDMatch(byte *a, byte storedUID[][4], int numUIDs) {
  for (int i = 0; i < numUIDs; i++) {
    bool match = true;
    for (byte j = 0; j < 4; j++) {
      if (a[j] != storedUID[i][j]) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

void setColor(int red, int green, int blue) {
  analogWrite(RED_LED_PIN, red);
  analogWrite(GREEN_LED_PIN, green);
  analogWrite(BLUE_LED_PIN, blue);
}

void displayMessage(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

void display_and_upload_All() {

  TimeCur = millis();
  if ((TimeCur - ModuleTimerStart) > 2000) {
    int chk = DHT.read11(DHT11_PIN);
    LightValue = analogRead(LightsensorPin);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("Humidity:    ");
    display.println(DHT.humidity, 1);
    display.print("Temperature: ");
    display.println(DHT.temperature, 1);
    display.print("Light:       ");
    display.println(LightValue);
    display.print("Distance:    ");
    display.println(distance);
    display.print("Door locked:    ");
    display.println(door_opened);
    display.print("Password:  ");
    display.print(masked_password);
    display.println();
    display.display();
    ModuleTimerStart = millis();
  }
  TimeCur = millis();
  if ((TimeCur - CloudTimerStart >= 60000) && (alarmWarning == false) &&(!PasswordState)) {
    int humidity_value = int(DHT.humidity);
    int temperature_value = int(DHT.temperature);
    if(upload_count==0){
      ThingSpeak.setField(1, humidity_value);
    }
    if(upload_count==1){
      ThingSpeak.setField(2, temperature_value);
    }
    if(upload_count==2){
      ThingSpeak.setField(3, LightValue);
    }
    if(upload_count==3){
      ThingSpeak.setField(4, distance);
      upload_count=-1;
    }    
//    ThingSpeak.setField(1, humidity_value);
//    ThingSpeak.setField(2, temperature_value);
//    ThingSpeak.setField(3, LightValue);
//    ThingSpeak.setField(4, distance);
    if (door_opened)
      ThingSpeak.setField(5, 1);
    else
      ThingSpeak.setField(5, 0);
    int result = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    Serial.print("Upload Result: ");
    Serial.println(result);  // Print result for debugging
    CloudTimerStart = millis();
    upload_count++;
  }
}

// Angle: 0-360, Direction: 0(anticlock), 1(clockwise), Speed: 0-100
void MotorRun(int angle, int dirct, int speed) {
  motorState = 1; //state the motor is running
  int step = 0;
  int runTimes = 5.688 * angle;
  if (dirct == 0) {
    while (runTimes--) {
      switch (step) {
        case 0: mD1 = 1; mD2 = 1; mD3 = 0; mD4 = 0; break;
        case 1: mD1 = 0; mD2 = 1; mD3 = 1; mD4 = 0; break;
        case 2: mD1 = 0; mD2 = 0; mD3 = 1; mD4 = 1; break;
        case 3: mD1 = 1; mD2 = 0; mD3 = 0; mD4 = 1; break;
      };
      MotorIN(mD1, mD2, mD3, mD4);
      if (step >= 3) step = 0;
      else step++;
      if (speed > 100) speed = 100;
      delay(104 - speed);
      MotorIN(0, 0, 0, 0);
    }
    motorState = 2; //state motor stop in open state
    mD1 = 0; mD2 = 0; mD3 = 0; mD4 = 0;
  } else {
    while (runTimes--) {
      switch (step) {
        case 0: mD1 = 0; mD2 = 0; mD3 = 1; mD4 = 1; break;
        case 1: mD1 = 0; mD2 = 1; mD3 = 1; mD4 = 0; break;
        case 2: mD1 = 1; mD2 = 1; mD3 = 0; mD4 = 0; break;
        case 3: mD1 = 1; mD2 = 0; mD3 = 0; mD4 = 1; break;
      };
      MotorIN(mD1, mD2, mD3, mD4);
      if (step >= 3) step = 0;
      else step++;
      if (speed > 100) speed = 100;
      delay(104 - speed);
      MotorIN(0, 0, 0, 0);
    }
    motorState = 0; //state motor stop in close state
    mD1 = 0; mD2 = 0; mD3 = 0; mD4 = 0;
  }
}

// Send signals to motor
void MotorIN(int D1, int D2, int D3, int D4) {
  digitalWrite(motorIN1Pin, D1);
  digitalWrite(motorIN2Pin, D2);
  digitalWrite(motorIN3Pin, D3);
  digitalWrite(motorIN4Pin, D4);
}


void simpleUltrasonic() {
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  // Calculating the distance
  distance = duration * 0.034 / 2;
}

void doorClose() {
  setColor(0, 0, 0);  // Turn off LED
  MotorRun(90, 1, 100); // Automatic door locking
  door_opened = false;
  enableLock = false;
  Serial.println("Locked");
  tone(buzzer, 1000); // 1000 == Send 1KHz sound signal
  delay(500);        // for 0.5 sec
  noTone(buzzer);
}
void doorOpen() {
  password = "";  // Reset the password string
  PasswordState = false;
  setColor(0, 255, 0);  // Green

  MotorRun(90, 0, 100); // Turn anticlockwise 90 degrees
  door_opened = true;
  enableLock = false;
  Serial.println("Opened");
  tone(buzzer, 3000); // 1000 == Send 1KHz sound signal
  delay(300);        // for 0.3 sec
  noTone(buzzer);
  delay(2000);
}

void alarmsystem() {
  if ((not door_opened) && ((distance >= uppestDistanceBound) || (LightValue > LightWarningBound))) {
    alarmWarning = true;
    tone(buzzer, 3000); // 1000 == Send 1KHz sound signal
    setColor(255, 0, 0);  // Red
    delay(500);        // for 0.5 sec
    noTone(buzzer);
    setColor(0, 0, 0);  // Turn off LED
  }
  else alarmWarning = false;
}

void ultrasonicSensor() {
  sendPulse();
  d_flag = 1;
}

void calculateDistance() {
  duration = TimeCur - startTime;
  // Calculating the distance
  distance = duration * 0.034 / 2;
  if (distance > 500)
    distance = -1;
  if (distance > 300)
    distance = 300;
  d_flag = 0;
}

void sendPulse() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
}

void interruptLogic() {
  if (true && (motorState != 1)) {
    if (d_flag == 1) {
      startTime = micros();
      d_flag = 2;
    }
    else if (d_flag == 2) {
      TimeCur = micros();
      calculateDistance();
    }

  }
}
