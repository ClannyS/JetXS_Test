/*
 28-12-2020 - Added interrupt feature
 31-12-2020 - improved interrupt with timers

  Libraries:
   Ethernet Library
   Spi library
   FastLED library 3.4.0






*/

//Includes needed for Ethernet and WS2812B LEDS
#include <Ethernet.h>
#include <SPI.h>
#include <FastLED.h>

//defines for the amount of LEDS connected to the datapin, and initialize CRGB
#define NUM_LEDS 16
#define DATA_PIN 8
CRGB leds[NUM_LEDS];

int i = 0;

//Set the MAC address & IP
const byte mac[] PROGMEM = {0xDE, 0xAD, 0xC0, 0xA8, 0xC0, 0xB};
IPAddress ip(192, 168, 192, 11);
IPAddress remote(192, 168, 192, 105);
unsigned int localPort = 9004;
unsigned int remPort = 3002;

EthernetUDP Udp;

const byte numChars = 32;
char tempChars[numChars];
char packetBuffer[numChars];

//ID for this node
const int PROGMEM thisNode = 10003;
const int PROGMEM thisMember = 1;
const char PROGMEM ackNowledge = 6;
const char PROGMEM BELL = 7;
const char PROGMEM stx = 43;
const char PROGMEM etx = 45;

boolean newData = false;
volatile int counter = 0;

char *datagram[10];

//======== interrupt ========
boolean debug = false;
int sw = 7;
int val = 1;
int lpin = 6;
volatile boolean flag = false;
int state = 1;

// === interrupt routines ===
ISR(TIMER2_COMPA_vect) {        //start timer
  bitClear(TIMSK2,OCIE2A);      //Disable interrupt timer 2
  val = digitalRead(sw);
  if (((state == 1)) && ((val == 0))); {
    state = val;
      if (state == 0) {
        digitalWrite(lpin, HIGH);
      }
  }
   bitSet(PCIFR,PCIF2);
   bitSet(PCICR,PCIE2);
}



ISR(PCINT2_vect) {          //Debounce
  bitClear(PCICR,PCIE2);       //Disable PinChangeInterruptControlRegister 2
  bitSet(TCCR2A,WGM21);        //Set TimerCounterControlRegister A to CTC mode
  bitSet(TCCR2B,CS20);         //Clock 128
  bitSet(TCCR2B,CS22);         //Clock 128
  OCR2A = 250;                 //Set OutputCompareRegister to 250, 2ms
  TCNT2 = 0;                   //Clear the counter
  bitSet(TIFR2,OCF2A);         //Set Timer to compare with OCR2A
  bitSet(TIMSK2,OCIE2A);
}


void setup() {
// interupt
pinMode(sw, INPUT_PULLUP);
pinMode(lpin, OUTPUT);
PCMSK2 = 0X80;              //Set the inetrrupt mask to 1000;0000 = 0x80
bitSet(PCIFR,PCIF2);        //Clear pending interrupts of PinChangeInterruptFlagRegister 2)
bitSet(PCICR,PCIE2);        //Enable PinChangeInterruptControlRegister 2

 
FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
Serial.begin(9600);
Serial.print("My IP adress is ");
Serial.println(ip);
Serial.print("port: ");
Serial.println(localPort);
Serial.print("This node: ");
Serial.println(thisNode);
Serial.print("This member: ");
Serial.println(thisMember);
Serial.println("This sketch expects 3 pieces of data - text, an integer and a floating point value");
Serial.println("Enter data in this style <STX>[Node ID (5)],[Member ID (2)],[Command (4)],[Bytes to follow],[cmd DATA]<ETX>  ");
Serial.println();

Ethernet.init(10);
Ethernet.begin(mac, ip);
Udp.begin(localPort);
}

void loop() {
TurnOffButtonLed();
recvUDPWithStartEndMarkers();

if (newData == true) {
    //      A Datagram was received
    strcpy(tempChars, packetBuffer);
    Serial.println("UDP message detected");
    //      this temporary copy is necessary to protect the original data
    //      because strtok() used in parseData() replaces the commas with \0
    parseData();
    showParsedData();
    commandMenu();
    newData = false;
  }
}

void commandMenu() {

  if (atoi(datagram[0]) != thisNode) {
    Serial.println("This message is not for me!!");
    Serial.println(datagram[0]);
  }
  
  else if (atoi(datagram[1]) != thisMember) {
    Serial.print("I will forward this to member: ");
    Serial.println(datagram[1]);
  }
  
  else if (strcmp(datagram[2], "WTDG") == 0) {
    Serial.println("WD ACK");
  }
  
  else if (strcmp(datagram[2], "LEDO") == 0) {
    Serial.println("LED ON");
    func();
  }
  
  else if (strcmp(datagram[2], "SCAM") == 0) {
  }
  
  else if (strcmp(datagram[2], "SOUT") == 0) {
    Serial.println("SET OUTPUT command received!!");
    digitalWrite(atoi(datagram[4]), HIGH);
  }
  
  else if (strcmp(datagram[2], "COUT") == 0) {
    Serial.println("CLEAR OUTPUT command received!!");
    digitalWrite(atoi(datagram[4]), LOW);
  }
  
  else if (strcmp(datagram[2], "IOUT") == 0) {
    Serial.println("Initialize OUTPUT command received!!");
    pinMode(atoi(datagram[4]), OUTPUT);
    digitalWrite(atoi(datagram[4]), LOW);
  }
  
  else {
    Serial.print("Unknown Command..:");
    Serial.println(datagram[2]);
  }
}

void recvUDPWithStartEndMarkers(){ //  Read Datagram to packetBuffer
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char rc;
  int pSize = Udp.parsePacket();


  while ((pSize > 0) && (newData == false)) {
    rc = Udp.read();
    //Serial.print ("RxD by UDP: ");
    Serial.println (rc);
    pSize--;
    if (recvInProgress == true) {
      if (rc != etx) {
        packetBuffer[ndx] = rc;
        ndx++;
        if (ndx >= numChars) {
          ndx = numChars - 1;
          //                 !! when max length exceeded reset and clear buffer? (abort receive?)
        }
      }
      else {
        remote = Udp.remoteIP();
        remPort = Udp.remotePort();

        packetBuffer[ndx] = '\0'; // terminate the string
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }

    else if (rc == stx) {
      recvInProgress = true;
    }
  }
}


void parseData() { //  split the data into its parts to Datagram[n]

// Datagram: NodeID,MemberID,Command,BytesToFollow,Data\0
  byte index = 0;
  char *ptr = NULL;

  ptr = strtok(tempChars, ",");  // takes a list of delimiters
  while (ptr != NULL)
  {
    datagram[index] = ptr;
    index++;
    ptr = strtok(NULL, ",");  // takes a list of delimiters

  }
}

void showParsedData() {
 //  Serial.print each item (debug)
  Serial.print("Node ID: ");
  Serial.println(datagram[0]);
  Serial.print("Member ID: ");
  Serial.println(datagram[1]);
  Serial.print("Command: ");
  Serial.println(datagram[2]);
  Serial.print("Bytes to follow: ");
  Serial.println(datagram[3]);
  Serial.print("Command Data: ");
  Serial.println(datagram[4]);
  }



void TurnOffButtonLed() {
  if (state == 1) {
  state = 0;
  delay(1000);
  digitalWrite(lpin, LOW);
  }
}

void func() {
   int a = 16;
   for(int q = 0; q <= 16; q++) {
    int r = random(256);
    int g = random(256);
    int b = random(256);
    leds[q].setRGB(r,g,b);
    FastLED.setBrightness(a);
  FastLED.show();
  delay(80);
   a = a + 16;
    if(q == 15) {
      FastLED.clear();
      a = 16;
      }
    }
}
