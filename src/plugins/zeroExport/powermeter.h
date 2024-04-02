//-----------------------------------------------------------------------------
// 2024 Ahoy, https://ahoydtu.de
// Creative Commons - https://creativecommons.org/licenses/by-nc-sa/4.0/deed
//-----------------------------------------------------------------------------

#ifndef __POWERMETER_H__
#define __POWERMETER_H__

#include <AsyncJson.h>
#include <HTTPClient.h>
#if defined(ZEROEXPORT_POWERMETER_TIBBER)
#include <base64.h>
#include <string.h>

#include <list>
#endif

#include "config/settings.h"
#if defined(ZEROEXPORT_POWERMETER_TIBBER)
#include "plugins/zeroExport/lib/sml.h"
#endif

#if defined(ZEROEXPORT_POWERMETER_TIBBER)
typedef struct {
    const unsigned char OBIS[6];
    void (*Fn)(double &);
    float *Arg;
} OBISHandler;
#endif

typedef struct {
    float P;
    float P1;
    float P2;
    float P3;
} PowermeterBuffer_t;

class powermeter {
   public:
    powermeter() {
    }

    ~powermeter() {
    }

    bool setup(zeroExport_t *cfg, JsonObject *log /*Hier muss noch geklärt werden was gebraucht wird*/) {
        mCfg = cfg;
        mLog = log;
        return true;
    }

    /** loop
     * abfrage der gruppen um die aktuellen Werte (Zähler) zu ermitteln.
     */
    void loop(unsigned long *tsp, bool *doLog) {
        if (*tsp - mPreviousTsp <= 1000) return;  // skip when it is to fast
        mPreviousTsp = *tsp;

        PowermeterBuffer_t power;

        for (u_short group = 0; group < ZEROEXPORT_MAX_GROUPS; group++) {
            switch (mCfg->groups[group].pm_type) {
#if defined(ZEROEXPORT_POWERMETER_SHELLY)
                case zeroExportPowermeterType_t::Shelly:
                    power = getPowermeterWattsShelly(*mLog, group);
                    break;
#endif
#if defined(ZEROEXPORT_POWERMETER_TASMOTA)
                case zeroExportPowermeterType_t::Tasmota:
                    power = getPowermeterWattsTasmota(*mLog, group);
                    break;
#endif
#if defined(ZEROEXPORT_POWERMETER_MQTT)
                case zeroExportPowermeterType_t::Mqtt:
                    power = getPowermeterWattsMqtt(*mLog, group);
                    break;
#endif
#if defined(ZEROEXPORT_POWERMETER_HICHI)
                case zeroExportPowermeterType_t::Hichi:
                    power = getPowermeterWattsHichi(*mLog, group);
                    break;
#endif
#if defined(ZEROEXPORT_POWERMETER_TIBBER)
                case zeroExportPowermeterType_t::Tibber:
                    power = getPowermeterWattsTibber(*mLog, group);
                    break;
#endif
            }

            bufferWrite(power, group);
            *doLog = true;
        }
    }

    /** groupGetPowermeter
     * Holt die Daten vom Powermeter
     * @param group
     * @returns true/false
     */
    PowermeterBuffer_t getDataAVG(uint8_t group) {
        PowermeterBuffer_t avg;
        avg.P = avg.P1 = avg.P2 = avg.P2 = avg.P3 = 0;

        for (int i = 0; i < 5; i++) {
            avg.P += mPowermeterBuffer[group][i].P;
            avg.P1 += mPowermeterBuffer[group][i].P1;
            avg.P2 += mPowermeterBuffer[group][i].P2;
            avg.P3 += mPowermeterBuffer[group][i].P3;
        }
        avg.P = avg.P / 5;
        avg.P1 = avg.P1 / 5;
        avg.P2 = avg.P2 / 5;
        avg.P3 = avg.P3 / 5;

        return avg;
    }

   private:
#if defined(ZEROEXPORT_POWERMETER_SHELLY)
    /** getPowermeterWattsShelly
     * ...
     * @param logObj
     * @param group
     * @returns true/false
     */
    PowermeterBuffer_t getPowermeterWattsShelly(JsonObject logObj, uint8_t group) {
        PowermeterBuffer_t result;
        result.P = result.P1 = result.P2 = result.P3 = 0;

        logObj["mod"] = "getPowermeterWattsShelly";

        HTTPClient http;
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setUserAgent("Ahoy-Agent");
        // TODO: Ahoy-0.8.850024-zero
        http.setConnectTimeout(500);
        http.setTimeout(1000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");

        String url = String("http://") + String(mCfg->groups[group].pm_url) + String("/") + String(mCfg->groups[group].pm_jsonPath);
        logObj["HTTP_URL"] = url;

        http.begin(url);

        if (http.GET() == HTTP_CODE_OK) {
            // Parsing
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, http.getString());
            if (error) {
                logObj["err"] = "deserializeJson: " + String(error.c_str());
                return result;
            } else {
                if (doc.containsKey(F("total_power"))) {
                    // Shelly 3EM
                    result.P = doc["total_power"];
                } else if (doc.containsKey(F("em:0"))) {
                    // Shelly pro 3EM
                } else if (doc.containsKey(F("total_act_power"))) {
                    // Shelly pro 3EM
                    result.P = doc["total_act_power"];
                } else {
                    // Keine Daten
                    result.P = 0;
                }

                if (doc.containsKey(F("emeters"))) {
                    // Shelly 3EM
                    result.P1 = doc["emeters"][0]["power"];
                } else if (doc.containsKey(F("em:0"))) {
                    // Shelly pro 3EM
                    result.P1 = doc["em:0"]["a_act_power"];
                } else if (doc.containsKey(F("a_act_power"))) {
                    // Shelly pro 3EM
                    result.P1 = doc["a_act_power"];
                } else if (doc.containsKey(F("switch:0"))) {
                    // Shelly plus1pm plus2pm
                    result.P1 = doc["switch:0"]["apower"];
                    result.P += result.P1;
                } else if (doc.containsKey(F("apower"))) {
                    // Shelly Alternative
                    result.P1 = doc["apower"];
                    result.P += result.P1;
                } else {
                    // Keine Daten
                    result.P1 = 0;
                }

                if (doc.containsKey(F("emeters"))) {
                    // Shelly 3EM
                    result.P2 = doc["emeters"][1]["power"];
                } else if (doc.containsKey(F("em:0"))) {
                    // Shelly pro 3EM
                    result.P2 = doc["em:0"]["b_act_power"];
                } else if (doc.containsKey(F("b_act_power"))) {
                    // Shelly pro 3EM
                    result.P2 = doc["b_act_power"];
                } else if (doc.containsKey(F("switch:1"))) {
                    // Shelly plus1pm plus2pm
                    result.P2 = doc["switch.1"]["apower"];
                    result.P += result.P2;
                    //} else if (doc.containsKey(F("apower"))) {
                    // Shelly Alternative
                    //    mCfg->groups[group].pmPowerL2 = doc["apower"];
                    //    mCfg->groups[group].pmPower += mCfg->groups[group].pmPowerL2;
                    //    ret = true;
                } else {
                    // Keine Daten
                    result.P2 = 0;
                }

                if (doc.containsKey(F("emeters"))) {
                    // Shelly 3EM
                    result.P3 = doc["emeters"][2]["power"];
                } else if (doc.containsKey(F("em:0"))) {
                    // Shelly pro 3EM
                    result.P3 = doc["em:0"]["c_act_power"];
                } else if (doc.containsKey(F("c_act_power"))) {
                    // Shelly pro 3EM
                    result.P3 = doc["c_act_power"];
                } else if (doc.containsKey(F("switch:2"))) {
                    // Shelly plus1pm plus2pm
                    result.P3 = doc["switch:2"]["apower"];
                    result.P += result.P3;
                    //} else if (doc.containsKey(F("apower"))) {
                    // Shelly Alternative
                    //    mCfg->groups[group].pmPowerL3 = doc["apower"];
                    //    mCfg->groups[group].pmPower += mCfg->groups[group].pmPowerL3;
                    //    result = true;
                } else {
                    // Keine Daten
                    result.P3 = 0;
                }
            }
        }
        http.end();
        return result;
    }
#endif

#if defined(ZEROEXPORT_POWERMETER_TASMOTA)
    /** getPowermeterWattsTasmota
     * ...
     * @param logObj
     * @param group
     * @returns true/false
     *
     * Vorlage:
     * http://IP/cm?cmnd=Status0
     * {
     *  "Status":{
     *      "Module":1,
     *      "DeviceName":"Tasmota",
     *      "FriendlyName":["Tasmota"],
     *      "Topic":"Tasmota",
     *      "ButtonTopic":"0",
     *      "Power":0,
     *      "PowerOnState":3,
     *      "LedState":1,
     *      "LedMask":"FFFF",
     *      "SaveData":1,
     *      "SaveState":1,
     *      "SwitchTopic":"0",
     *      "SwitchMode":[0,0,0,0,0,0,0,0],
     *      "ButtonRetain":0,
     *      "SwitchRetain":0,
     *      "SensorRetain":0,
     *      "PowerRetain":0,
     *      "InfoRetain":0,
     *      "StateRetain":0},
     *      "StatusPRM":{"Baudrate":9600,"SerialConfig":"8N1","GroupTopic":"tasmotas","OtaUrl":"http://ota.tasmota.com/tasmota/release/tasmota.bin.gz","RestartReason":"Software/System restart","Uptime":"202T01:24:51","StartupUTC":"2023-08-13T15:21:13","Sleep":50,"CfgHolder":4617,"BootCount":27,"BCResetTime":"2023-02-04T16:45:38","SaveCount":150,"SaveAddress":"F5000"},
     *      "StatusFWR":{"Version":"11.1.0(tasmota)","BuildDateTime":"2022-05-05T03:23:22","Boot":31,"Core":"2_7_4_9","SDK":"2.2.2-dev(38a443e)","CpuFrequency":80,"Hardware":"ESP8266EX","CR":"378/699"},
     *      "StatusLOG":{"SerialLog":0,"WebLog":2,"MqttLog":0,"SysLog":0,"LogHost":"","LogPort":514,"SSId":["Odyssee2001",""],"TelePeriod":300,"Resolution":"558180C0","SetOption":["00008009","2805C80001000600003C5A0A190000000000","00000080","00006000","00004000"]},
     *      "StatusMEM":{"ProgramSize":658,"Free":344,"Heap":17,"ProgramFlashSize":1024,"FlashSize":1024,"FlashChipId":"14325E","FlashFrequency":40,"FlashMode":3,"Features":["00000809","87DAC787","043E8001","000000CF","010013C0","C000F989","00004004","00001000","04000020"],"Drivers":"1,2,3,4,5,6,7,8,9,10,12,16,18,19,20,21,22,24,26,27,29,30,35,37,45,56,62","Sensors":"1,2,3,4,5,6,53"},
     *      "StatusNET":{"Hostname":"Tasmota","IPAddress":"192.168.100.81","Gateway":"192.168.100.1","Subnetmask":"255.255.255.0","DNSServer1":"192.168.100.1","DNSServer2":"0.0.0.0","Mac":"4C:11:AE:11:F8:50","Webserver":2,"HTTP_API":1,"WifiConfig":4,"WifiPower":17.0},
     *      "StatusMQT":{"MqttHost":"192.168.100.80","MqttPort":1883,"MqttClientMask":"Tasmota","MqttClient":"Tasmota","MqttUser":"mqttuser","MqttCount":156,"MAX_PACKET_SIZE":1200,"KEEPALIVE":30,"SOCKET_TIMEOUT":4},
     *      "StatusTIM":{"UTC":"2024-03-02T16:46:04","Local":"2024-03-02T17:46:04","StartDST":"2024-03-31T02:00:00","EndDST":"2024-10-27T03:00:00","Timezone":"+01:00","Sunrise":"07:29","Sunset":"18:35"},
     *      "StatusSNS":{
     *          "Time":"2024-03-02T17:46:04",
     *          "PV":{
     *              "Bezug":0.364,
     *              "Einspeisung":3559.439,
     *              "Leistung":-14
     *          }
     *      },
     *      "StatusSTS":{"Time":"2024-03-02T17:46:04","Uptime":"202T01:24:51","UptimeSec":17457891,"Heap":16,"SleepMode":"Dynamic","Sleep":50,"LoadAvg":19,"MqttCount":156,"POWER":"OFF","Wifi":{"AP":1,"SSId":"Odyssee2001","BSSId":"34:31:C4:22:92:74","Channel":6,"Mode":"11n","RSSI":100,"Signal":-50,"LinkCount":15,"Downtime":"0T00:08:22"}
     *      }
     *  }
     */
    PowermeterBuffer_t getPowermeterWattsTasmota(JsonObject logObj, uint8_t group) {
        PowermeterBuffer_t result;
        result.P = result.P1 = result.P2 = result.P3 = 0;

        logObj["mod"] = "getPowermeterWattsTasmota";
        /*
        // TODO: nicht komplett

                    HTTPClient http;
                    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
                    http.setUserAgent("Ahoy-Agent");
        // TODO: Ahoy-0.8.850024-zero
                    http.setConnectTimeout(500);
                    http.setTimeout(500);
        // TODO: Timeout von 1000 reduzieren?
                    http.addHeader("Content-Type", "application/json");
                    http.addHeader("Accept", "application/json");

        //            String url = String("http://") + String(mCfg->groups[group].pm_url) + String("/") + String(mCfg->groups[group].pm_jsonPath);
                    String url = String(mCfg->groups[group].pm_url);
                    logObj["HTTP_URL"] = url;

                    http.begin(url);

                    if (http.GET() == HTTP_CODE_OK)
                    {

                        // Parsing
                        DynamicJsonDocument doc(2048);
                        DeserializationError error = deserializeJson(doc, http.getString());
                        if (error)
                        {
                            logObj["error"] = "deserializeJson() failed: " + String(error.c_str());
                            return result;
                        }

        // TODO: Sum
                            result = true;

        // TODO: L1

        // TODO: L2

        // TODO: L3

        /*
                        JsonObject Tasmota_ENERGY = doc["StatusSNS"]["ENERGY"];
                        int Tasmota_Power = Tasmota_ENERGY["Power"]; // 0
                        return Tasmota_Power;
        */
        /*
        String url = "http://" + String(TASMOTA_IP) + "/cm?cmnd=status%2010";
        ParsedData = http.get(url).json();
        int Watts = ParsedData[TASMOTA_JSON_STATUS][TASMOTA_JSON_PAYLOAD_MQTT_PREFIX][TASMOTA_JSON_POWER_MQTT_LABEL].toInt();
        return Watts;
        */
        /*
                        logObj["P"]   = mCfg->groups[group].pmPower;
                        logObj["P1"] = mCfg->groups[group].pmPowerL1;
                        logObj["P2"] = mCfg->groups[group].pmPowerL2;
                        logObj["P3"] = mCfg->groups[group].pmPowerL3;
                    }
                    http.end();
        */
        return result;
    }
#endif

#if defined(ZEROEXPORT_POWERMETER_MQTT)
    /** getPowermeterWattsMqtt
     * ...
     * @param logObj
     * @param group
     * @returns true/false
     */
    PowermeterBuffer_t getPowermeterWattsMqtt(JsonObject logObj, uint8_t group) {
        PowermeterBuffer_t result;
        result.P = result.P1 = result.P2 = result.P3 = 0;

        logObj["mod"] = "getPowermeterWattsMqtt";

        // Hier neuer Code - Anfang

        // TODO: Noch nicht komplett

        // Hier neuer Code - Ende

        return result;
    }
#endif

#if defined(ZEROEXPORT_POWERMETER_HICHI)
    /** getPowermeterWattsHichi
     * ...
     * @param logObj
     * @param group
     * @returns true/false
     */
    PowermeterBuffer_t getPowermeterWattsHichi(JsonObject logObj, uint8_t group) {
        PowermeterBuffer_t result;
        result.P = result.P1 = result.P2 = result.P3 = 0;

        logObj["mod"] = "getPowermeterWattsHichi";

        // Hier neuer Code - Anfang

        // TODO: Noch nicht komplett

        // Hier neuer Code - Ende

        return result;
    }
#endif

#if defined(ZEROEXPORT_POWERMETER_TIBBER)
    /** getPowermeterWattsTibber
     * ...
     * @param logObj
     * @param group
     * @returns true/false
     * @TODO: Username & Passwort wird mittels base64 verschlüsselt. Dies wird für die Authentizierung benötigt. Wichtig diese im WebUI unkenntlich zu machen und base64 im eeprom zu speichern, statt klartext.
     * @TODO: Abfrage Interval einbauen. Info: Datei-Size kann auch mal 0-bytes sein!
     */

    sml_states_t currentState;

    float _powerMeterTotal = 0.0;

    float _powerMeter1Power = 0.0;
    float _powerMeter2Power = 0.0;
    float _powerMeter3Power = 0.0;

    float _powerMeterImport = 0.0;
    float _powerMeterExport = 0.0;

    /*
     07 81 81 c7 82 03 ff		#objName: OBIS Kennzahl für den Hersteller
     07 01 00 01 08 00 ff		#objName: OBIS Kennzahl für Wirkenergie Bezug gesamt tariflos
     07 01 00 01 08 01 ff 		#objName: OBIS-Kennzahl für Wirkenergie Bezug Tarif1
     07 01 00 01 08 02 ff		#objName: OBIS-Kennzahl für Wirkenergie Bezug Tarif2
     07 01 00 02 08 00 ff		#objName: OBIS-Kennzahl für Wirkenergie Einspeisung gesamt tariflos
     07 01 00 02 08 01 ff		#objName: OBIS-Kennzahl für Wirkenergie Einspeisung Tarif1
     07 01 00 02 08 02 ff		#objName: OBIS-Kennzahl für Wirkenergie Einspeisung Tarif2
    */
    const std::list<OBISHandler> smlHandlerList{
        {{0x01, 0x00, 0x10, 0x07, 0x00, 0xff}, &smlOBISW, &_powerMeterTotal},  // total - OBIS-Kennzahl für momentane Gesamtwirkleistung

        {{0x01, 0x00, 0x24, 0x07, 0x00, 0xff}, &smlOBISW, &_powerMeter1Power},  // OBIS-Kennzahl für momentane Wirkleistung in Phase L1
        {{0x01, 0x00, 0x38, 0x07, 0x00, 0xff}, &smlOBISW, &_powerMeter2Power},  // OBIS-Kennzahl für momentane Wirkleistung in Phase L2
        {{0x01, 0x00, 0x4c, 0x07, 0x00, 0xff}, &smlOBISW, &_powerMeter3Power},  // OBIS-Kennzahl für momentane Wirkleistung in Phase L3

        {{0x01, 0x00, 0x01, 0x08, 0x00, 0xff}, &smlOBISWh, &_powerMeterImport},
        {{0x01, 0x00, 0x02, 0x08, 0x00, 0xff}, &smlOBISWh, &_powerMeterExport}};

    PowermeterBuffer_t getPowermeterWattsTibber(JsonObject logObj, uint8_t group) {
        mPreviousTsp = mPreviousTsp + 2000;  // Zusätzliche Pause

        PowermeterBuffer_t result;
        result.P = result.P1 = result.P2 = result.P3 = 0;

        logObj["mod"] = "getPowermeterWattsTibber";

        HTTPClient http;
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setUserAgent("Ahoy-Agent");
        // TODO: Ahoy-0.8.850024-zero
        http.setConnectTimeout(500);
        http.setTimeout(1000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");

        String url = String("http://") + mCfg->groups[group].pm_url + String("/") + String(mCfg->groups[group].pm_jsonPath);
        String auth = base64::encode(String(mCfg->groups[group].pm_user) + String(":") + String(mCfg->groups[group].pm_pass));

        http.begin(url);
        http.addHeader("Authorization", "Basic " + auth);

        if (http.GET() == HTTP_CODE_OK && http.getSize() > 0) {
            String myString = http.getString();
            double readVal = 0;
            unsigned char c;

            for (int i = 0; i < http.getSize(); ++i) {
                c = myString[i];
                sml_states_t smlCurrentState = smlState(c);

                switch (smlCurrentState) {
                    case SML_FINAL:
                        result.P = _powerMeterTotal;
                        result.P1 = _powerMeter1Power;
                        result.P2 = _powerMeter2Power;
                        result.P3 = _powerMeter3Power;

                        if (!(_powerMeter1Power && _powerMeter2Power && _powerMeter3Power)) {
                            result.P1 = result.P2 = result.P3 = _powerMeterTotal / 3;
                        }
                        break;
                    case SML_LISTEND:
                        // check handlers on last received list
                        for (auto &handler : smlHandlerList) {
                            if (smlOBISCheck(handler.OBIS)) {
                                handler.Fn(readVal);
                                *handler.Arg = readVal;
                            }
                        }
                        break;
                }
            }
        }

        http.end();
        return result;
    }
#endif

    void bufferWrite(PowermeterBuffer_t raw, short group) {
        mPowermeterBuffer[group][mPowermeterBufferPos[group]] = raw;
        mPowermeterBufferPos[group]++;
        if (mPowermeterBufferPos[group] >= 5) mPowermeterBufferPos[group] = 0;
    }

    zeroExport_t *mCfg;
    JsonObject *mLog;

    unsigned long mPreviousTsp = 0;

    PowermeterBuffer_t mPowermeterBuffer[ZEROEXPORT_MAX_GROUPS][5] = {0};
    short mPowermeterBufferPos[ZEROEXPORT_MAX_GROUPS] = {0};
};

// TODO: Vorlagen für Powermeter-Analyse

/** Shelly Pro 3EM
 * Stand: 02.04.2024
 * Analysiert: tictrick
 * /rpc/EM.GetStatus?id=0
 * {"id":0,"a_current":1.275,"a_voltage":228.8,"a_act_power":243.2,"a_aprt_power":291.6,"a_pf":0.86,"a_freq":50.0,"b_current":0.335,"b_voltage":228.2,"b_act_power":24.5,"b_aprt_power":76.6,"b_pf":0.59,"b_freq":50.0,"c_current":0.338,"c_voltage":227.6,"c_act_power":34.9,"c_aprt_power":76.9,"c_pf":0.65,"c_freq":50.0,"n_current":null,"total_current":1.949,"total_act_power":302.554,"total_aprt_power":445.042, "user_calibrated_phase":[]}
 * /rpc/Shelly.GetStatus
 * {"ble":{},"cloud":{"connected":true},"em:0":{"id":0,"a_current":1.269,"a_voltage":228.5,"a_act_power":239.8,"a_aprt_power":289.7,"a_pf":0.86,"a_freq":50.0,"b_current":0.348,"b_voltage":227.5,"b_act_power":24.7,"b_aprt_power":79.0,"b_pf":0.58,"b_freq":50.0,"c_current":0.378,"c_voltage":228.5,"c_act_power":42.3,"c_aprt_power":86.3,"c_pf":0.66,"c_freq":50.0,"n_current":null,"total_current":1.995,"total_act_power":306.811,"total_aprt_power":455.018, "user_calibrated_phase":[]},"emdata:0":{"id":0,"a_total_act_energy":648103.86,"a_total_act_ret_energy":0.00,"b_total_act_energy":142793.47,"b_total_act_ret_energy":171396.02,"c_total_act_energy":493778.01,"c_total_act_ret_energy":0.00,"total_act":1284675.34, "total_act_ret":171396.02},"eth":{"ip":null},"modbus":{},"mqtt":{"connected":false},"script:1":{"id":1,"running":true,"mem_used":1302,"mem_peak":3094,"mem_free":23898},"sys":{"mac":"3CE90E6EBE5C","restart_required":false,"time":"18:09","unixtime":1712074162,"uptime":3180551,"ram_size":240004,"ram_free":96584,"fs_size":524288,"fs_free":176128,"cfg_rev":23,"kvs_rev":9104,"schedule_rev":0,"webhook_rev":0,"available_updates":{},"reset_reason":3},"temperature:0":{"id": 0,"tC":46.2, "tF":115.1},"wifi":{"sta_ip":"192.168.0.69","status":"got ip","ssid":"Riker","rssi":-70},"ws":{"connected":false}}
 */



/*
Shelly 1pm
Der Shelly 1pm verfügt über keine eigene Spannungsmessung sondern geht von 220V * Korrekturfaktor aus. Dadurch wird die Leistungsmessung verfälscht und der Shelly ist ungeeignet.


http://192.168.xxx.xxx/status
Shelly 3em (oder em3) :
ok
{"wifi_sta":{"connected":true,"ssid":"Odyssee2001","ip":"192.168.100.85","rssi":-23},"cloud":{"enabled":false,"connected":false},"mqtt":{"connected":true},"time":"17:13","unixtime":1709223219,"serial":27384,"has_update":false,"mac":"3494547B94EE","cfg_changed_cnt":1,"actions_stats":{"skipped":0},"relays":[{"ison":false,"has_timer":false,"timer_started":0,"timer_duration":0,"timer_remaining":0,"overpower":false,"is_valid":true,"source":"input"}],"emeters":[{"power":51.08,"pf":0.27,"current":0.78,"voltage":234.90,"is_valid":true,"total":1686297.2,"total_returned":428958.4},{"power":155.02,"pf":0.98,"current":0.66,"voltage":235.57,"is_valid":true,"total":878905.6,"total_returned":4.1},{"power":6.75,"pf":0.26,"current":0.11,"voltage":234.70,"is_valid":true,"total":206151.1,"total_returned":0.0}],"total_power":212.85,"emeter_n":{"current":0.00,"ixsum":1.29,"mismatch":false,"is_valid":false},"fs_mounted":true,"v_data":1,"ct_calst":0,"update":{"status":"idle","has_update":false,"new_version":"20230913-114244/v1.14.0-gcb84623","old_version":"20230913-114244/v1.14.0-gcb84623","beta_version":"20231107-165007/v1.14.1-rc1-g0617c15"},"ram_total":49920,"ram_free":30192,"fs_size":233681,"fs_free":154616,"uptime":13728721}


Shelly plus 2pm :
ok
{"ble":{},"cloud":{"connected":false},"input:0":{"id":0,"state":false},"input:1":{"id":1,"state":false},"mqtt":{"connected":true},"switch:0":{"id":0, "source":"MQTT", "output":false, "apower":0.0, "voltage":237.0, "freq":50.0, "current":0.000, "pf":0.00, "aenergy":{"total":62758.285,"by_minute":[0.000,0.000,0.000],"minute_ts":1709223337},"temperature":{"tC":35.5, "tF":96.0}},"switch:1":{"id":1, "source":"MQTT", "output":false, "apower":0.0, "voltage":237.1, "freq":50.0, "current":0.000, "pf":0.00, "aenergy":{"total":61917.211,"by_minute":[0.000,0.000,0.000],"minute_ts":1709223337},"temperature":{"tC":35.5, "tF":96.0}},"sys":{"mac":"B0B21C10A478","restart_required":false,"time":"17:15","unixtime":1709223338,"uptime":8746115,"ram_size":245016,"ram_free":141004,"fs_size":458752,"fs_free":135168,"cfg_rev":7,"kvs_rev":0,"schedule_rev":0,"webhook_rev":0,"available_updates":{"stable":{"version":"1.2.2"}}},"wifi":{"sta_ip":"192.168.100.87","status":"got ip","ssid":"Odyssee2001","rssi":-62},"ws":{"connected":false}}

http://192.168.xxx.xxx/rpc/Shelly.GetStatus
Shelly plus 1pm :
nok keine negativen Leistungswerte
{"ble":{},"cloud":{"connected":false},"input:0":{"id":0,"state":false},"mqtt":{"connected":true},"switch:0":{"id":0, "source":"MQTT", "output":false, "apower":0.0, "voltage":235.9, "current":0.000, "aenergy":{"total":20393.619,"by_minute":[0.000,0.000,0.000],"minute_ts":1709223441},"temperature":{"tC":34.6, "tF":94.3}},"sys":{"mac":"FCB467A66E3C","restart_required":false,"time":"17:17","unixtime":1709223443,"uptime":8644483,"ram_size":246256,"ram_free":143544,"fs_size":458752,"fs_free":147456,"cfg_rev":9,"kvs_rev":0,"schedule_rev":0,"webhook_rev":0,"available_updates":{"stable":{"version":"1.2.2"}}},"wifi":{"sta_ip":"192.168.100.88","status":"got ip","ssid":"Odyssee2001","rssi":-42},"ws":{"connected":false}}
*/

#endif /*__POWERMETER_H__*/
