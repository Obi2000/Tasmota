/*
  xdrv_95_httphook.ino - HttpHook support for Tasmota

  Copyright (C) 2019  Eric Maycock (erocm123)
  
  Copyright (C) 2020 Updated by Markus Liljergren (markus-li)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef USE_HTTPHOOK
#define XDRV_95                95

#include <ESP8266SSDP.h>

unsigned long httphookConnectionFailures;
unsigned long httphookFailureTimeout = millis();

enum HttpHookCommands {
  CMND_HTTPHOOKHOST, CMND_HTTPHOOKPORT };
const char kHttpHookCommands[] PROGMEM =
  D_CMND_HTTPHOOKHOST "|"
  D_CMND_HTTPHOOKPORT;

int httphook_command_code = 0;

boolean httphookPublish() {
  boolean success = false;
  if (String(Settings.httphook_host) != "" && String(Settings.httphook_port) != "") {
    String authHeader = "";
    String message = "";
    const char* host = Settings.httphook_host;
    int port = Settings.httphook_port;
    if (httphookConnectionFailures >= 3) { // Too many errors; Trying not to get stuck
      if (millis() - httphookFailureTimeout < 1800000) {
        return false;
      } else {
        httphookFailureTimeout = millis();
      }
    }
    WiFiClient client;
    if (!client.connect(host, port))
    {
      httphookConnectionFailures++;
      return false;
    }
    if (httphookConnectionFailures)
      httphookConnectionFailures = 0;

    // TODO: Add full custom URL support
    String url = F("/");

    message = String(mqtt_data);

    client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + ":" + port + "\r\n" + authHeader +
                 "Content-Type: application/json;charset=utf-8\r\n" +
                 "Content-Length: " + message.length() + "\r\n" +
                 "Server: Tasmota\r\n" +
                 "Connection: close\r\n\r\n" +
                 message + "\r\n");

    unsigned long timer = millis() + 200;
    while (!client.available() && millis() < timer)
      delay(1);

    // Read all the lines of the reply from server
    while (client.available()) {
      String line = client.readStringUntil('\n');
      if (line.substring(0, 15) == "HTTP/1.1 200 OK")
      {
        success = true;
      }
      delay(1);
    }

    client.flush();
    client.stop();
  }

  return success;
}

String deblank(const char* input)
{
  String output = String(input);
  output.replace(" ", "");
  return output;
}

/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

#ifdef USE_WEBSERVER

#define WEB_HANDLE_HTTPHOOK "hs"

const char S_CONFIGURE_HTTPHOOK[] PROGMEM = D_CONFIGURE_HTTPHOOK;

const char HTTP_BTN_MENU_HTTPHOOK[] PROGMEM =
  "<p><form action='" WEB_HANDLE_HTTPHOOK "' method='get'><button>" D_CONFIGURE_HTTPHOOK "</button></form></p>";

const char HTTP_FORM_HTTPHOOK[] PROGMEM =
  "<fieldset><legend><b>&nbsp;" D_HTTPHOOK_PARAMETERS "&nbsp;</b></legend>"
  "<form method='get' action='" WEB_HANDLE_HTTPHOOK "'>"
  "<br/><b>" D_HOST "</b> (" HTTPHOOK_HOST ")<br/><input id='mh' name='mh' placeholder='" HTTPHOOK_HOST" ' value='%s'><br/>"
  "<br/><b>" D_PORT "</b> (" STR(HTTPHOOK_PORT) ")<br/><input id='ml' name='ml' placeholder='" STR(HTTPHOOK_PORT) "' value='%d'><br/>";

void HandleHttpHookConfiguration(void)
{
  if (!HttpCheckPriviledgedAccess()) { return; }
  
  AddLog_P(LOG_LEVEL_DEBUG, S_LOG_HTTP, S_CONFIGURE_HTTPHOOK);

  if (Webserver->hasArg("save")) {
    HttpHookSaveSettings();
    WebRestart(1);
    return;
  }

  char str[sizeof(Settings.ex_mqtt_client)];

  WSContentStart_P(S_CONFIGURE_HTTPHOOK);
  WSContentSendStyle();
  WSContentSend_P(HTTP_FORM_HTTPHOOK,
    Settings.httphook_host,
    Settings.httphook_port);
 
  WSContentSend_P(HTTP_FORM_END);
  WSContentSpaceButton(BUTTON_CONFIGURATION);
  WSContentStop();
}

void HttpHookSaveSettings(void)
{
  char tmp[100];
  char stemp[TOPSZ];
  char stemp2[TOPSZ];

  WebGetArg("mh", tmp, sizeof(tmp));
  strlcpy(Settings.httphook_host, (!strlen(tmp)) ? HTTPHOOK_HOST : (!strcmp(tmp,"0")) ? "" : tmp, sizeof(Settings.httphook_host));
  WebGetArg("ml", tmp, sizeof(tmp));
  Settings.httphook_port = (!strlen(tmp)) ? HTTPHOOK_PORT : atoi(tmp);
  // erocm123: Need to add logging
  //snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_MQTT D_CMND_MQTTHOST " %s, " D_CMND_MQTTPORT " %d, " D_CMND_MQTTCLIENT " %s, " D_CMND_MQTTUSER " %s, " D_CMND_TOPIC " %s, " D_CMND_FULLTOPIC " %s"),
  //  Settings.mqtt_host, Settings.mqtt_port, Settings.mqtt_client, Settings.mqtt_user, Settings.mqtt_topic, Settings.mqtt_fulltopic);
  //AddLog(LOG_LEVEL_INFO);
}
#endif  // USE_WEBSERVER

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

boolean HttpHookCommand(void)
{
  char command [CMDSZ];
  char sunit[CMDSZ];
  boolean serviced = true;
  uint8_t status_flag = 0;
  uint8_t unit = 0;
  unsigned long nvalue = 0;

  uint16_t data_len = XdrvMailbox.data_len;
  int32_t payload = XdrvMailbox.payload;
  char *dataBuf = XdrvMailbox.data;

  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, kHttpHookCommands);
  httphook_command_code = command_code;
  if (-1 == command_code) {
    serviced = false;  // Unknown command
  }
  else if (CMND_HTTPHOOKHOST == command_code) {
    if ((data_len > 0) && (data_len < sizeof(Settings.httphook_host))) {
      strlcpy(Settings.httphook_host, (SC_CLEAR == Shortcut()) ? "" : (SC_DEFAULT == Shortcut()) ? HTTPHOOK_HOST : dataBuf, sizeof(Settings.httphook_host));
      //restart_flag = 2;
    }
    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_SVALUE, command, Settings.httphook_host);
  }
  else if (CMND_HTTPHOOKPORT == command_code) {
    if (payload > 0) {
      Settings.httphook_port = (1 == payload) ? HTTPHOOK_PORT : payload;
      //restart_flag = 2;
    }
    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_NVALUE, command, Settings.httphook_port);
  }
  else serviced = false;  // Unknown command

  if (serviced && !status_flag) {

    // TODO: Disabling this, why do we need it?
    //if (UNIT_MICROSECOND == unit) {
    //  snprintf_P(command, sizeof(command), PSTR("%sCal"), command);
    //}

    //if (Settings.flag.value_units) {
    //  snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_LVALUE_SPACE_UNIT, command, nvalue, GetTextIndexed(sunit, sizeof(sunit), unit, kUnitNames));
    //} else {
    //  snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_LVALUE, command, nvalue);
    //}
  }

  return serviced;
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv95(uint8_t function)
{
  bool result = false;

  if (Settings.flag4.httphook_enabled) {
    switch (function) {
#ifdef USE_WEBSERVER
      case FUNC_INIT:
        SSDP.setSchemaURL("description.xml");
        SSDP.setHTTPPort(80);
        SSDP.setName(ModuleName().c_str()?ModuleName().c_str():"Sonoff");
        SSDP.setSerialNumber(ESP.getChipId());
        SSDP.setURL("index.html");
        SSDP.setModelName(ModuleName().c_str()?ModuleName().c_str():"Sonoff");
        SSDP.setModelNumber(deblank(ModuleName().c_str()?ModuleName().c_str():"Sonoff") + "_SL");
        SSDP.setModelURL("http://smartlife.tech");
        SSDP.setManufacturer("Smart Life Automated");
        SSDP.setManufacturerURL("http://smartlife.tech");
        SSDP.begin();
        if (!strlen(Settings.httphook_host)) {
          strlcpy(Settings.httphook_host, HTTPHOOK_HOST, sizeof(Settings.httphook_host));
          //Settings.httphook_host = HTTPHOOK_HOST;
        }
        if (!Settings.httphook_port) {
          Settings.httphook_port = HTTPHOOK_PORT;
        }
      break;
      case FUNC_LOOP:
        break;
      case FUNC_COMMAND:
        result = HttpHookCommand();
        break;
      case FUNC_WEB_ADD_BUTTON:
        WSContentSend_P(HTTP_BTN_MENU_HTTPHOOK);
        break;
      case FUNC_WEB_ADD_HANDLER:
        Webserver->on("/" WEB_HANDLE_HTTPHOOK, HandleHttpHookConfiguration);
        break;
#endif  // USE_WEBSERVER
    }
  }
  return result;
}
#endif
