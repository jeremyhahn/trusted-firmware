// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

#include <stdio.h>
#include <Arduino.h>
#include <Ethernet.h>
#include "DHT.h"
#include "DallasTemperature.h"
#include "EEPROM.h"

#define DEBUG 0
#define EEPROM_DEBUG 1
#define BUFSIZE 255
#define CHANNEL_SIZE 7

#define FLOW0_PIN A0
#define MOISTURE0_PIN A1
#define MOISTURE1_PIN A2
#define MOISTURE2_PIN A3
#define MOISTURE3_PIN A4
#define MOISTURE4_PIN A5
#define MOISTURE5_PIN A6
#define DHT_PIN0 9

extern int  __bss_end;
extern int  *__brkval;

const int NULL_CHANNEL = 255;
const int channels[CHANNEL_SIZE] = {
	8, 2, 3, 4, 5, 6, 7
};
unsigned long channel_table[CHANNEL_SIZE][3] = {
	// channel, start, interval
   {NULL_CHANNEL, 0, 0},
   {NULL_CHANNEL, 0, 0},
   {NULL_CHANNEL, 0, 0},
   {NULL_CHANNEL, 0, 0},
   {NULL_CHANNEL, 0, 0},
   {NULL_CHANNEL, 0, 0},
   {NULL_CHANNEL, 0, 0} // 7
};

const char json_bracket_open[] = "{";
const char json_bracket_close[] = "}";
const char json_array_bracket_close[] = "]";
const char json_comma[] = ",";

const char string_initializing[] PROGMEM = "Initializing irrigation...";
const char string_dhcp_failed[] PROGMEM = "DHCP Failed";
const char string_http_200[] PROGMEM = "HTTP/1.1 200 OK";
const char string_http_404[] PROGMEM = "HTTP/1.1 404 Not Found";
const char string_http_500[] PROGMEM = "HTTP/1.1 500 Internal Server Error";
const char string_http_content_type_json[] PROGMEM = "Content-Type: application/json";
const char string_http_xpowered_by[] PROGMEM = "X-Powered-By: CropDroid";
const char string_rest_address[] PROGMEM = "REST service listening on: ";
const char string_switch_on[] PROGMEM = "Switching on";
const char string_switch_off[] PROGMEM = "Switching off";
const char string_json_key_mem[] PROGMEM = "\"mem\":";
const char string_json_key_tempF[] PROGMEM = ",\"tempF\":";
const char string_json_key_tempC[] PROGMEM = ",\"tempC\":";
const char string_json_key_humidity[] PROGMEM = ",\"humidity\":";
const char string_json_key_heatIndex[] PROGMEM = ",\"heatIndex\":";
const char string_json_key_vpd[] PROGMEM = ",\"vpd\":";
const char string_json_key_flow0[] PROGMEM = ",\"flow0\":";
const char string_json_key_moisture0[] PROGMEM = ",\"moisture0\":";
const char string_json_key_moisture1[] PROGMEM = ",\"moisture1\":";
const char string_json_key_moisture2[] PROGMEM = ",\"moisture2\":";
const char string_json_key_moisture3[] PROGMEM = ",\"moisture3\":";
const char string_json_key_moisture4[] PROGMEM = ",\"moisture4\":";
const char string_json_key_moisture5[] PROGMEM = ",\"moisture5\":";
const char string_json_key_metrics[] PROGMEM = "\"metrics\":{";
const char string_json_key_channels[] PROGMEM = ",\"channels\":[";
const char string_json_key_channel[] PROGMEM = "\"channel\":";
const char string_json_key_pin[] PROGMEM = ",\"pin\":";
const char string_json_key_position[] PROGMEM =  ",\"position\":";
const char string_json_key_value[] PROGMEM =  ",\"value\":";
const char string_json_key_address[] PROGMEM =  "\"address\":";
const char string_json_bracket_open[] PROGMEM = "{";
const char string_json_bracket_close[] PROGMEM = "}";
const char string_json_error_invalid_channel[] PROGMEM = "\"error\":\"Invalid channel\"";
const char string_json_reboot_true PROGMEM = "\"reboot\":true";
const char string_json_reset_true PROGMEM = "\"reset\":true";
const char string_hardware_version[] PROGMEM = "\"hardware\":\"irrigation-v0.1b\",";
const char string_firmware_version[] PROGMEM = "\"firmware\":\"1.0.0b\"";
const char string_json_key_uptime[] PROGMEM = ",\"uptime\":";
const char * const string_table[] PROGMEM = {
  string_initializing,
  string_dhcp_failed,
  string_http_200,
  string_http_404,
  string_http_500,
  string_http_content_type_json,
  string_http_xpowered_by,
  string_rest_address,
  string_switch_on,
  string_switch_off,
  string_json_key_mem,
  string_json_key_tempF,
  string_json_key_tempC,
  string_json_key_humidity,
  string_json_key_heatIndex,
  string_json_key_vpd,
  string_json_key_flow0,
  string_json_key_moisture0,
  string_json_key_moisture1,
  string_json_key_moisture2,
  string_json_key_moisture3,
  string_json_key_moisture4,
  string_json_key_moisture5,
  string_json_key_metrics,
  string_json_key_channels,
  string_json_key_channel,
  string_json_key_pin,
  string_json_key_position,
  string_json_key_value,
  string_json_key_address,
  string_json_bracket_open,
  string_json_bracket_close,
  string_json_error_invalid_channel,
  string_json_reboot_true,
  string_json_reset_true,
  string_hardware_version,
  string_firmware_version,
  string_json_key_uptime
};
int idx_initializing = 0,
    idx_dhcp_failed = 1,
	idx_http_200 = 2,
	idx_http_404 = 3,
	idx_http_500 = 4,
	idx_http_content_type_json = 5,
	idx_http_xpowered_by = 6,
	idx_rest_address = 7,
	idx_switch_on = 8,
	idx_switch_off = 9,
	idx_json_key_mem = 10,
	idx_json_key_tempF = 11,
	idx_json_key_tempC = 12,
	idx_json_key_humidity = 13,
	idx_json_key_heatIndex = 14,
	idx_json_key_vpd = 15,
	idx_json_key_flow0 = 16,
	idx_json_key_moisture0 = 17,
	idx_json_key_moisture1 = 18,
	idx_json_key_moisture2 = 19,
	idx_json_key_moisture3 = 20,
	idx_json_key_moisture4 = 21,
	idx_json_key_moisture5 = 22,
	idx_json_key_metrics = 23,
	idx_json_key_channels = 24,
	idx_json_key_channel = 25,
	idx_json_key_pin = 26,
	idx_json_key_position = 27,
	idx_json_key_value = 28,
	idx_json_key_address = 29,
	idx_json_key_bracket_open = 30,
	idx_json_key_bracket_close = 31,
	idx_json_error_invalid_channel = 32,
	idx_json_reboot_true = 33,
	idx_json_reset_true = 34,
	idx_hardware_version = 35,
	idx_firmware_version = 36,
	idx_json_key_uptime = 37;
char string_buffer[50];
char float_buffer[10];

DHT dht0(DHT_PIN0, DHT22);

float tempF, tempC, humidity, heatIndex, VPD = 0.0;
int flow0, moisture0, moisture1, moisture2,
	moisture3, moisture4, moisture5 = 0;

void readTempHumidity();
void calculateVPD(float temp, float rh);
void readMoistureLevels();
void readFlow();
void handleWebRequest();
void send404();
void switchOn(int pin);
void switchOff(int pin);
void resetDefaults();
int availableMemory();
void(* resetFunc) (void) = 0;

byte defaultMac[] = { 0x04, 0x02, 0x00, 0x00, 0x00, 0x04 };

byte mac[] = {
	EEPROM.read(0),
	EEPROM.read(1),
	EEPROM.read(2),
	EEPROM.read(3),
	EEPROM.read(4),
	EEPROM.read(5)
};
IPAddress ip(
  EEPROM.read(6),
  EEPROM.read(7),
  EEPROM.read(8),
  EEPROM.read(9));

EthernetServer httpServer(80);
EthernetClient httpClient;

int main(void) {
  init();
  setup();;

  for (;;)
    loop();

  return 0;
}

void setup(void) {

  //EEPROM.write(0, 255);

  #if DEBUG || EEPROM_DEBUG
    Serial.begin(115200);

    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_initializing])));
    Serial.println(string_buffer);
  #endif

  analogReference(DEFAULT);

  for(int i=0; i<CHANNEL_SIZE; i++) {
	pinMode(channels[i], OUTPUT);
	digitalWrite(channels[i], LOW);
  }
  pinMode(FLOW0_PIN, INPUT_PULLUP);
  pinMode(MOISTURE0_PIN, INPUT_PULLUP);
  pinMode(MOISTURE1_PIN, INPUT_PULLUP);
  pinMode(MOISTURE2_PIN, INPUT_PULLUP);
  pinMode(MOISTURE3_PIN, INPUT_PULLUP);
  pinMode(MOISTURE4_PIN, INPUT_PULLUP);
  pinMode(MOISTURE5_PIN, INPUT_PULLUP);

  byte macByte1 = EEPROM.read(0);

  #if DEBUG || EEPROM_DEBUG
    Serial.print("EEPROM(0):");
    Serial.println(macByte1);
  #endif

  if(macByte1 == 255) {
	resetDefaults();
    if(Ethernet.begin(defaultMac) == 0) {
	  #if DEBUG
	    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_dhcp_failed])));
		Serial.println(string_buffer);
	  #endif
	  return;
	}
  }
  else {
    Ethernet.begin(mac, ip);
  }

  #if DEBUG || EEPROM_DEBUG
    Serial.print("MAC:");
    for(int i=0; i<6; i++) {
      Serial.print(mac[i], HEX);
    }
    Serial.println();

    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_rest_address])));
    Serial.print(string_buffer);
    Serial.println(Ethernet.localIP());
  #endif

  httpServer.begin();
  dht0.begin();
}

void loop() {

  digitalWrite(LED_BUILTIN, HIGH);

  readTempHumidity();
  calculateVPD(tempC, humidity);
  readMoistureLevels();
  readFlow();

  handleWebRequest();

  digitalWrite(LED_BUILTIN, LOW);

  delay(2000); // DHT22 requires min 2 seconds between polls
}

void calculateVPD(float temp, float rh) {
  //float es = 0.6108 * exp(17.27 * temp / (temp + 237.3));
  //float ea = rh / 100 * es;
  //VPD = ea - es;

  float es = 0.6107 * exp(7.5 * temp / (temp + 237.3));
  float ea = rh / 100 * es;
  VPD = ea - es;

  //float svp = 610.78 * exp(temp / (temp + 238.3) * 17.2694);
  //VPD = svp * (1 - rh / 100);

#if DEBUG
  Serial.print("VPD:");
  Serial.println(VPD);
#endif
}

float zeroIfNan(float number) {
  if(isnan(number)) {
	number = 0.0;
  }
  return number;
}

void readTempHumidity() {

  tempC = zeroIfNan(dht0.readTemperature(false));
  tempF = zeroIfNan(dht0.convertCtoF(tempC));
  humidity = zeroIfNan(dht0.readHumidity());
  heatIndex = zeroIfNan(dht0.computeHeatIndex(tempF, humidity));

#if DEBUG
  Serial.print("TempF:");
  Serial.println(tempF);
  Serial.print("TempC:");
  Serial.println(tempC);
  Serial.print("Humidity:");
  Serial.println(humidity);
  Serial.print("HeatIndex:");
  Serial.println(heatIndex);
#endif
}

void readMoistureLevels() {
  moisture0 = analogRead(MOISTURE0_PIN);
  moisture1 = analogRead(MOISTURE1_PIN);
  moisture2 = analogRead(MOISTURE2_PIN);
  moisture3 = analogRead(MOISTURE3_PIN);
  moisture4 = analogRead(MOISTURE4_PIN);
  moisture5 = analogRead(MOISTURE5_PIN);

#if DEBUG
  itoa(moisture0, float_buffer, 10);
  strcpy(string_buffer, "moisture0:");
  strcat(string_buffer, float_buffer);
  strcat(string_buffer, "%");
  Serial.println(string_buffer);

  itoa(moisture1, float_buffer, 10);
  strcpy(string_buffer, "moisture1:");
  strcat(string_buffer, float_buffer);
  strcat(string_buffer, "%");
  Serial.println(string_buffer);

  itoa(moisture2, float_buffer, 10);
  strcpy(string_buffer, "moisture2:");
  strcat(string_buffer, float_buffer);
  strcat(string_buffer, "%");
  Serial.println(string_buffer);

//  itoa(moisture3, float_buffer, 10);
//  strcpy(string_buffer, "moisture3:");
//  strcat(string_buffer, float_buffer);
//  strcat(string_buffer, "%");
//  Serial.println(string_buffer);
//
//  itoa(moisture4, float_buffer, 10);
//  strcpy(string_buffer, "moisture4:");
//  strcat(string_buffer, float_buffer);
//  strcat(string_buffer, "%");
//  Serial.println(string_buffer);

  itoa(moisture5, float_buffer, 10);
  strcpy(string_buffer, "moisture5:");
  strcat(string_buffer, float_buffer);
  strcat(string_buffer, "%");
  Serial.println(string_buffer);
#endif
}

void readFlow() {

  flow0 = analogRead(FLOW0_PIN);

#if DEBUG
  itoa(flow0, float_buffer, 10);
  strcpy(string_buffer, "flow0:");
  strcat(string_buffer, float_buffer);
  strcat(string_buffer, "%");
  Serial.println(string_buffer);
#endif
}

void switchOn(int pin) {
#if DEBUG
  char sPin[3];
  itoa(pin, sPin, 10);
  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_switch_on])));
  strcat(string_buffer, " pin ");
  strcat(string_buffer, sPin);
  Serial.println(string_buffer);
#endif
  digitalWrite(pin, HIGH);
}

void switchOff(int pin) {
#if DEBUG
  char sPin[3];
  itoa(pin, sPin, 10);
  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_switch_off])));
  strcat(string_buffer, " pin ");
  strcat(string_buffer, sPin);
  Serial.println(string_buffer);
#endif
  digitalWrite(pin, LOW);
}

void handleWebRequest() {

	unsigned long currentMillis = millis();

	for(int i=0; i<CHANNEL_SIZE; i++) {
		if(channel_table[i][0] != NULL_CHANNEL) {
			if(currentMillis - channel_table[i][1] > channel_table[i][2]) {

				digitalWrite(channel_table[i][0], LOW);

				#if DEBUG
				  Serial.print("Turning channel ");
				  Serial.print(i);
				  Serial.print(" (pin ");
				  Serial.print(channel_table[i][0]);
				  Serial.print(") off after ");
				  Serial.print(channel_table[i][2] / 1000);
				  Serial.println(" seconds");
				#endif

				channel_table[i][0] = NULL_CHANNEL;
				channel_table[i][1] = 0;
				channel_table[i][2] = 0;
			}
		}
	}

	httpClient = httpServer.available();

	char clientline[BUFSIZE] = {0};
	int index = 0;

	bool reboot = false;
	char json[375];
	char sPin[3];
	char state[3];

	if (httpClient) {

		// reset input buffer
		index = 0;

		while (httpClient.connected()) {

			if (httpClient.available()) {

				char c = httpClient.read();
				if (c != '\n' && c != '\r' && index < BUFSIZE) {
					clientline[index++] = c;
					continue;
				}

				httpClient.flush();

				String urlString = String(clientline);
				String op = urlString.substring(0, urlString.indexOf(' '));
				urlString = urlString.substring(urlString.indexOf('/'), urlString.indexOf(' ', urlString.indexOf('/')));
				urlString.toCharArray(clientline, BUFSIZE);

				char *resource = strtok(clientline, "/");
				char *param1 = strtok(NULL, "/");
				char *param2 = strtok(NULL, "/");

				#if DEBUG
				  Serial.print("Resource: ");
				  Serial.println(resource);

				  Serial.print("Param1: ");
				  Serial.println(param1);

				  Serial.print("Param2: ");
				  Serial.println(param2);
				#endif

				// /state
				if (strncmp(resource, "state", 5) == 0) {

					strcpy(json, json_bracket_open);

					  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_metrics])));
					  strcat(json, string_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_mem])));
					    strcat(json, string_buffer);
					    itoa(availableMemory(), float_buffer, 10);
					    strcat(json, float_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_tempF])));
					    strcat(json, string_buffer);
					    dtostrf(tempF, 4, 2, float_buffer);
					    strcat(json, float_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_tempC])));
					    strcat(json, string_buffer);
					    dtostrf(tempC, 4, 2, float_buffer);
					    strcat(json, float_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_humidity])));
					    strcat(json, string_buffer);
					    dtostrf(humidity, 4, 2, float_buffer);
					    strcat(json, float_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_heatIndex])));
					    strcat(json, string_buffer);
					    dtostrf(heatIndex, 4, 2, float_buffer);
					    strcat(json, float_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_vpd])));
					    strcat(json, string_buffer);
					    dtostrf(VPD, 4, 2, float_buffer);
					    strcat(json, float_buffer);

					    strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_flow0])));
					    strcat(json, string_buffer);
						itoa(flow0, float_buffer, 10);
						strcat(json, float_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_moisture0])));
						strcat(json, string_buffer);
						itoa(moisture0, float_buffer, 10);
						strcat(json, float_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_moisture1])));
						strcat(json, string_buffer);
						itoa(moisture1, float_buffer, 10);
						strcat(json, float_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_moisture2])));
						strcat(json, string_buffer);
						itoa(moisture2, float_buffer, 10);
						strcat(json, float_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_moisture3])));
						strcat(json, string_buffer);
						itoa(moisture3, float_buffer, 10);
						strcat(json, float_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_moisture4])));
						strcat(json, string_buffer);
						itoa(moisture4, float_buffer, 10);
						strcat(json, float_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_moisture5])));
						strcat(json, string_buffer);
						itoa(moisture5, float_buffer, 10);
						strcat(json, float_buffer);

					  strcat(json, json_bracket_close);

					  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_channels])));
					  strcat(json, string_buffer);
					  for(int i=0; i<CHANNEL_SIZE; i++) {
						itoa(channels[i], sPin, 10);
						itoa(digitalRead(channels[i]), state, 10);

						strcat(json, state);
						if(i + 1 < CHANNEL_SIZE) {
						  strcat(json, ",");
						}
					  }
					  strcat(json, json_array_bracket_close);

					strcat(json, json_bracket_close);
				}

				// /timer/{channel}/{seconds}
				else if (strncmp(resource, "timer", 5) == 0) {

					if(param1 == NULL || param1 == "") {
						#if DEBUG
							Serial.println("parameter required");
						#endif
						//send500("parameter required");
						break;
					}

					int channel = atoi(param1);
					unsigned long duration = strtoul(param2, NULL, 10);

					if(channel >= 0 && channel < CHANNEL_SIZE && duration > 0) {

						#if DEBUG
						  Serial.print("Channel: ");
						  Serial.println(channel);

						  Serial.print("Duration: ");
						  Serial.println(duration);
						#endif

						channel_table[channel][0] = channels[channel];
						channel_table[channel][1] = millis();
						channel_table[channel][2] = duration * 1000;

						digitalWrite(channels[channel], HIGH);
					}
					else {
					  #if DEBUG
						Serial.println("Invalid channel/duration");
					  #endif
					}

					strcpy(json, json_bracket_open);

						strcat(json, "\"channel\":");
						itoa(channel, float_buffer, 10);
						strcat(json, float_buffer);

						strcat(json, ",\"duration\":");
						itoa(duration, float_buffer, 10);
						strcat(json, float_buffer);

					strcat(json, json_bracket_close);
				}

				// /switch/?     1 = on, else off
				else if (strncmp(resource, "switch", 6) == 0) {

					bool valid = false;
					int channel = atoi(param1);
					int position = atoi(param2);

					if(channel >= 0 && channel < CHANNEL_SIZE) {
						valid = true;
					}

					if(valid) {

						position == 1 ? switchOn(channels[channel]) : switchOff(channels[channel]);

						strcpy(json, json_bracket_open);

						  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_channel])));
						  strcat(json, string_buffer);
						  strcat(json, param1);

						  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_pin])));
						  strcat(json, string_buffer);
						  itoa(channels[channel], string_buffer, 10);
						  strcat(json, string_buffer);

						  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_position])));
						  strcat(json, string_buffer);
						  itoa(digitalRead(channels[channel]), string_buffer, 10);
						  strcat(json, string_buffer);

						strcat(json, json_bracket_close);

						#if DEBUG
							Serial.print("/switch: ");
							Serial.println(json);
						#endif

					}
					else {
						strcpy(json, json_bracket_open);
							strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_error_invalid_channel])));
							strcat(json, string_buffer);
						strcat(json, json_bracket_close);
					}
				}

				// /eeprom
				else if (strncmp(resource, "eeprom", 6) == 0) {
					#if EEPROM_DEBUG
						Serial.println("/eeprom");
						Serial.println(param1);
						Serial.println(param2);
					#endif

					EEPROM.write(atoi(param1), atoi(param2));

					strcpy(json, json_bracket_open);

					  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_address])));
					  strcat(json, string_buffer);
					  strcat(json, param1);

					  strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_value])));
					  strcat(json, string_buffer);
					  strcat(json, param2);

					strcat(json, json_bracket_close);
				}

				// /reboot
				else if (strncmp(resource, "reboot", 6) == 0) {
					#if DEBUG
						Serial.println("/reboot");
					#endif

					strcpy(json, json_bracket_open);
						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_reboot_true])));
						strcat(json, string_buffer);
					strcat(json, json_bracket_close);

					reboot = true;
				}

				// /reset
				else if (strncmp(resource, "reset", 5) == 0) {
					#if DEBUG
						Serial.println("/reset");
					#endif

					resetDefaults();

					strcpy(json, json_bracket_open);
						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_reset_true])));
						strcat(json, string_buffer);
					strcat(json, json_bracket_close);

					reboot = true;
				}

				// /sys
				else if (strncmp(resource, "sys", 3) == 0) {

					strcpy(json, json_bracket_open);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_hardware_version])));
						strcat(json, string_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_firmware_version])));
						strcat(json, string_buffer);

						strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_json_key_uptime])));
						strcat(json, string_buffer);
						itoa(millis(), string_buffer, 10);
						strcat(json, string_buffer);

					strcat(json, json_bracket_close);
				}

				else {
					send404();
					break;
				}

				strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_http_200])));
				httpClient.println(string_buffer);

				strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_http_xpowered_by])));
				httpClient.println(string_buffer);

				strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_http_content_type_json])));
				httpClient.println(string_buffer);

				httpClient.println();
				httpClient.println(json);

				break;
			}
		}
	}

	// give the web browser time to receive the data
	delay(100);

	// close the connection:
	httpClient.stop();

	if(reboot)  {
	  resetFunc();
	  return;
	}
}

void send404() {

	strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_http_404])));
	httpClient.println(string_buffer);

	#if DEBUG
	  Serial.println(string_buffer);
	#endif

	strcpy_P(string_buffer, (char*)pgm_read_word(&(string_table[idx_http_xpowered_by])));
	httpClient.println(string_buffer);

	httpClient.println();
}

void resetDefaults() {
	EEPROM.write(0, defaultMac[0]);
	EEPROM.write(1, defaultMac[1]);
	EEPROM.write(2, defaultMac[2]);
	EEPROM.write(3, defaultMac[3]);
	EEPROM.write(4, defaultMac[4]);
	EEPROM.write(5, defaultMac[5]);
	EEPROM.write(6, 192);
	EEPROM.write(7, 168);
	EEPROM.write(8, 0);
	EEPROM.write(9, 94);
}

int availableMemory() {
  int free_memory;
  return ((int) __brkval == 0) ? ((int) &free_memory) - ((int) &__bss_end) :
      ((int) &free_memory) - ((int) __brkval);
}
