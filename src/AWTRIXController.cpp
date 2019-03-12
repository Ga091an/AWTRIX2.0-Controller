#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Fonts/TomThumb.h>
#include <LightDependentResistor.h>
#include <Wire.h>
#include <SparkFun_APDS9960.h>

String version = "0.4"; 

#include "awtrix-conf.h"

//////////////////////////////////////////////////////////////
//////////////////////// Don't touch /////////////////////////
#define NUMMATRIX (32 * 8)
CRGB leds[NUMMATRIX];
String incomingCommand;

#ifdef MATRIX_MODEV2
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(leds, 32, 8, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);
#endif
#ifndef MATRIX_MODEV2
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(leds, 32, 8, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
#endif

LightDependentResistor photocell(LDR_PIN, LDR_RESISTOR, LDR_PHOTOCELL);
SparkFun_APDS9960 apds = SparkFun_APDS9960();


unsigned long startTime = 0;
unsigned long endTime = 0;
unsigned long duration;
volatile bool isr_flag = 0;
bool updating = false;

static byte c1;  // Last character buffer

byte utf8ascii(byte ascii) {
  if ( ascii < 128 ) // Standard ASCII-set 0..0x7F handling
  { c1 = 0;
    return ( ascii );
  }
  // get previous input
byte last = c1;   // get last char
  c1 = ascii;       // remember actual character
  switch (last)     // conversion depending on first UTF8-character
  { case 0xC2: return  (ascii) - 34;  break;
    case 0xC3: return  (ascii | 0xC0) - 34;  break;
    case 0x82: if (ascii == 0xAC) return (0xEA);   
  }
  return  (0);
}

// convert String object from UTF8 String to Extended ASCII
String utf8ascii(String s) {
  String r = "";
  char c;
  for (int i = 0; i < s.length(); i++)
  {
    c = utf8ascii(s.charAt(i));
    if (c != 0) r += c;
  }
  return r;
}

// In Place conversion UTF8-string to Extended ASCII (ASCII is shorter!)
void utf8ascii(char* s) {
  int k = 0;
  char c;
  for (int i = 0; i < strlen(s); i++)
  {
    c = utf8ascii(s[i]);
    if (c != 0)
      s[k++] = c;
  }
  s[k] = 0;
}


String GetChipID()
{
	return String(ESP.getChipId());
}

int GetRSSIasQuality(int rssi)
{
	int quality = 0;

	if (rssi <= -100)
	{
		quality = 0;
	}
	else if (rssi >= -50)
	{
		quality = 100;
	}
	else
	{
		quality = 2 * (rssi + 100);
	}
	return quality;
}


void processing(String cmd)
{
	DynamicJsonBuffer jsonBuffer;
	JsonObject &json = jsonBuffer.parseObject(cmd);

		String type = json["type"];

	if (type.equals("show"))
	{
		matrix->show();
	}
	else if (type.equals("clear"))
	{
		matrix->clear();
	}
	else if (type.equals("drawText"))
	{
		if (json["font"].as<String>().equals("big"))
		{
			matrix->setFont();
			matrix->setCursor(json["x"].as<int16_t>(), json["y"].as<int16_t>() - 1);
		}
		else
		{
			matrix->setFont(&TomThumb);
			matrix->setCursor(json["x"].as<int16_t>(), json["y"].as<int16_t>() + 5);
		}
		matrix->setTextColor(matrix->Color(json["color"][0].as<int16_t>(), json["color"][1].as<int16_t>(), json["color"][2].as<int16_t>()));
		String text = json["text"];
		
		matrix->print(utf8ascii(text));
	}
	else if (type.equals("drawBMP"))
	{
		int16_t h = json["height"].as<int16_t>();
		int16_t w = json["width"].as<int16_t>();
		int16_t x = json["x"].as<int16_t>();
		int16_t y = json["y"].as<int16_t>();

		for (int16_t j = 0; j < h; j++, y++)
		{
			for (int16_t i = 0; i < w; i++)
			{
				matrix->drawPixel(x + i, y, json["bmp"][j * w + i].as<int16_t>());
			}
		}
	}
	else if (type.equals("drawLine"))
	{
		matrix->drawLine(json["x0"].as<int16_t>(), json["y0"].as<int16_t>(), json["x1"].as<int16_t>(), json["y1"].as<int16_t>(), matrix->Color(json["color"][0].as<int16_t>(), json["color"][1].as<int16_t>(), json["color"][2].as<int16_t>()));
	}
	else if (type.equals("drawCircle"))
	{
		matrix->drawCircle(json["x0"].as<int16_t>(), json["y0"].as<int16_t>(), json["r"].as<int16_t>(), matrix->Color(json["color"][0].as<int16_t>(), json["color"][1].as<int16_t>(), json["color"][2].as<int16_t>()));
	}
	else if (type.equals("drawRect"))
	{
		matrix->drawRect(json["x"].as<int16_t>(), json["y"].as<int16_t>(), json["w"].as<int16_t>(), json["h"].as<int16_t>(), matrix->Color(json["color"][0].as<int16_t>(), json["color"][1].as<int16_t>(), json["color"][2].as<int16_t>()));
	}
		else if (type.equals("fill"))
	{
		matrix->fillScreen(matrix->Color(json["color"][0].as<int16_t>(), json["color"][1].as<int16_t>(), json["color"][2].as<int16_t>()));
	}
	else if (type.equals("drawPixel"))
	{
		matrix->drawPixel(json["x"].as<int16_t>(), json["y"].as<int16_t>(), matrix->Color(json["color"][0].as<int16_t>(), json["color"][1].as<int16_t>(), json["color"][2].as<int16_t>()));
	}
	else if (type.equals("setBrightness"))
	{
		matrix->setBrightness(json["brightness"].as<int16_t>());
	}
	else if (type.equals("speedtest"))
	{
		matrix->setFont(&TomThumb);
		matrix->setCursor(0, 7);

		endTime = millis();
		duration = endTime - startTime;
		if (duration > 85 || duration < 75)
		{
			matrix->setTextColor(matrix->Color(255, 0, 0));
		}
		else
		{
			matrix->setTextColor(matrix->Color(0, 255, 0));
		}
		matrix->print(duration);
		startTime = millis();
	}
	else if (type.equals("getMatrixInfo"))
	{
		StaticJsonBuffer<200> jsonBuffer;
		JsonObject& root = jsonBuffer.createObject();
		root["version"] = version;
		root["wifirssi"] = String(WiFi.RSSI());
		root["wifiquality"] =GetRSSIasQuality(WiFi.RSSI());
		root["wifissid"] =WiFi.SSID();
		root["getIP"] =WiFi.localIP().toString();
		String JS;
		root.printTo(JS);
		Serial.println("matrixInfo%" + String(JS));
	}
	else if (type.equals("getLUX"))
	{
		StaticJsonBuffer<200> jsonBuffer;
		Serial.println("matrixLux%" + String(photocell.getCurrentLux()));
	}
}



void interruptRoutine() {
  isr_flag = 1;
}

void handleGesture() {
    if (apds.isGestureAvailable()) {
    switch ( apds.readGesture() ) {
      case DIR_UP:
       Serial.write("control%UP");
        break;
      case DIR_DOWN:
        Serial.write("control%DOWN");
        break;
      case DIR_LEFT:
    Serial.write("control%LEFT");
        break;
      case DIR_RIGHT:
       Serial.write("control%RIGHT");
        break;
      case DIR_NEAR:
         Serial.write("control%NEAR");
        break;
      case DIR_FAR:
       Serial.write("control%FAR");
        break;
      default:
        Serial.write("control%NONE");
    }
  }
}

uint32_t Wheel(byte WheelPos, int pos) {
  if(WheelPos < 85) {
   return matrix->Color((WheelPos * 3)-pos, (255 - WheelPos * 3)-pos, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return matrix->Color((255 - WheelPos * 3)-pos, 0, (WheelPos * 3)-pos);
  } else {
   WheelPos -= 170;
   return matrix->Color(0, (WheelPos * 3)-pos, (255 - WheelPos * 3)-pos);
  }
}

void flashProgress(unsigned int progress, unsigned int total) {
    matrix->setBrightness(100);   
    long num = 32 * 8 * progress / total;
    for (unsigned char y = 0; y < 8; y++) {
        for (unsigned char x = 0; x < 32; x++) {
            if (num-- > 0) matrix->drawPixel(x, 8 - y - 1, Wheel((num*16) & 255,0));
        }
    }
    matrix->setCursor(0, 6);

		matrix->setTextColor(matrix->Color(255, 255, 255));
    matrix->print("FLASHING");

    matrix->show();
}


void setup()
{
	Serial.begin(921600);
	FastLED.addLeds<NEOPIXEL, MATRIX_PIN>(leds, NUMMATRIX).setCorrection(TypicalLEDStrip);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	matrix->begin();
	matrix->setTextWrap(false);
	matrix->setBrightness(80);
	matrix->setFont(&TomThumb);
	matrix->setCursor(7, 6);
	matrix->print("WiFi...");
	matrix->show();
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
	}

	photocell.setPhotocellPositionOnGround(false);

	matrix->clear();
	matrix->setCursor(6, 6);
	matrix->print("Ready!");
	matrix->show();



#if GESTURE
	Wire.begin(APDS9960_SDA,APDS9960_SCL);
  pinMode(APDS9960_INT, INPUT);
	attachInterrupt(APDS9960_INT, interruptRoutine, FALLING);
  apds.init();
  apds.enableGestureSensor(true);
#endif

 ArduinoOTA.onStart([&]() {
	  updating = true;
				matrix->clear();
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
         flashProgress(progress, total);
    });

    ArduinoOTA.begin();

}

void loop()
{
 ArduinoOTA.handle();

 if (!updating) {

if (Serial.available() > 0) {

incomingCommand = Serial.read(); // read the incoming byte:
processing(incomingCommand);
Serial.print("Command received:");
Serial.println(incomingCommand);
}
		if(isr_flag == 1 && GESTURE) {
    detachInterrupt(APDS9960_INT);
    handleGesture();
    isr_flag = 0;
    attachInterrupt(APDS9960_INT, interruptRoutine, FALLING);
  }
	
 }
}
