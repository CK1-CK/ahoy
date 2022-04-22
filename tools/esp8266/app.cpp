#include "app.h"

#include "html/h/index_html.h"
#include "html/h/hoymiles_html.h"
extern String setup_html;

//-----------------------------------------------------------------------------
app::app() : Main() {
    mHoymiles = new hoymiles();
    mDecoder  = new hm1200Decode();
    mBufCtrl  = new CircularBuffer(mBuffer, PACKET_BUFFER_SIZE);

    mSendCnt    = 0;
    mSendTicker = new Ticker();
    mFlagSend   = false;

    mMqttTicker = NULL;
    mMqttEvt    = false;

    memset(mCmds, 0, sizeof(uint32_t));
    memset(mChannelStat, 0, sizeof(uint32_t));
}


//-----------------------------------------------------------------------------
app::~app(void) {

}


//-----------------------------------------------------------------------------
void app::setup(const char *ssid, const char *pwd, uint32_t timeout) {
    Main::setup(ssid, pwd, timeout);

    mWeb->on("/",          std::bind(&app::showIndex,         this));
    mWeb->on("/setup",     std::bind(&app::showSetup,         this));
    mWeb->on("/save",      std::bind(&app::showSave,          this));
    mWeb->on("/cmdstat",   std::bind(&app::showCmdStatistics, this));
    mWeb->on("/hoymiles",  std::bind(&app::showHoymiles,      this));
    mWeb->on("/livedata",  std::bind(&app::showLiveData,      this));
    mWeb->on("/mqttstate", std::bind(&app::showMqtt,          this));

    if(mSettingsValid) {
        uint16_t interval;

        // hoymiles
        mEep->read(ADDR_INV0_ADDR,    mHoymiles->mAddrBytes, INV_ADDR_LEN);
        mEep->read(ADDR_INV_INTERVAL, &interval);

        if(interval < 1000)
            interval = 1000;
        mSendTicker->attach_ms(interval, std::bind(&app::sendTicker, this));


        // mqtt
        uint8_t mqttAddr[MQTT_ADDR_LEN];
        char mqttUser[MQTT_USER_LEN];
        char mqttPwd[MQTT_PWD_LEN];
        char mqttTopic[MQTT_TOPIC_LEN];
        mEep->read(ADDR_MQTT_ADDR,     mqttAddr,  MQTT_ADDR_LEN);
        mEep->read(ADDR_MQTT_USER,     mqttUser,  MQTT_USER_LEN);
        mEep->read(ADDR_MQTT_PWD,      mqttPwd,   MQTT_PWD_LEN);
        mEep->read(ADDR_MQTT_TOPIC,    mqttTopic, MQTT_TOPIC_LEN);
        mEep->read(ADDR_MQTT_INTERVAL, &interval);

        char addr[16] = {0};
        sprintf(addr, "%d.%d.%d.%d", mqttAddr[0], mqttAddr[1], mqttAddr[2], mqttAddr[3]);

        if(interval < 1000)
            interval = 1000;
        mMqtt.setup(addr, mqttTopic, mqttUser, mqttPwd);
        mMqttTicker = new Ticker();
        mMqttTicker->attach_ms(interval, std::bind(&app::mqttTicker, this));

        mMqtt.sendMsg("version", mVersion);
    }
    else {
        memset(mHoymiles->mAddrBytes, 0, 6);
    }
    mHoymiles->serial2RadioId();

    initRadio();

    if(!mSettingsValid)
        Serial.println("Warn: your settings are not valid! check [IP]/setup");
}


//-----------------------------------------------------------------------------
void app::loop(void) {
    Main::loop();

    if(!mBufCtrl->empty()) {
        uint8_t len, rptCnt;
        NRF24_packet_t *p = mBufCtrl->getBack();

        //mHoymiles->dumpBuf("RAW ", p->packet, MAX_RF_PAYLOAD_SIZE);

        if(mHoymiles->checkCrc(p->packet, &len, &rptCnt)) {
            // process buffer only on first occurrence
            if((0 != len) && (0 == rptCnt)) {
                //Serial.println("CMD " + String(p->packet[11], HEX));
                //mHoymiles->dumpBuf("Payload ", p->packet, len);

                mDecoder->convert(&p->packet[11], len);

                if(p->packet[11] == 0x01)      mCmds[0]++;
                else if(p->packet[11] == 0x02) mCmds[1]++;
                else if(p->packet[11] == 0x03) mCmds[2]++;
                else if(p->packet[11] == 0x81) mCmds[3]++;
                else if(p->packet[11] == 0x84) mCmds[4]++;
                else                           mCmds[5]++;

                if(p->sendCh == 23)      mChannelStat[0]++;
                else if(p->sendCh == 40) mChannelStat[1]++;
                else if(p->sendCh == 61) mChannelStat[2]++;
                else                     mChannelStat[3]++;
            }
        }
        mBufCtrl->popBack();
    }

    if(mFlagSend) {
        mFlagSend = false;

        uint8_t size = 0;
        if((mSendCnt % 6) == 0)
            size = mHoymiles->getTimePacket(mSendBuf, mTimestamp);
        else if((mSendCnt % 6) == 1)
            size = mHoymiles->getCmdPacket(mSendBuf, 0x15, 0x81);
        else if((mSendCnt % 6) == 2)
            size = mHoymiles->getCmdPacket(mSendBuf, 0x15, 0x80);
        else if((mSendCnt % 6) == 3)
            size = mHoymiles->getCmdPacket(mSendBuf, 0x15, 0x83);
        else if((mSendCnt % 6) == 4)
            size = mHoymiles->getCmdPacket(mSendBuf, 0x15, 0x82);
        else if((mSendCnt % 6) == 5)
            size = mHoymiles->getCmdPacket(mSendBuf, 0x15, 0x84);

        //Serial.println("sent packet: #" + String(mSendCnt));
        //dumpBuf(mSendBuf, size);
        sendPacket(mSendBuf, size);

        mSendCnt++;
    }


    // mqtt
    mMqtt.loop();
    if(mMqttEvt) {
        mMqttEvt = false;
        mMqtt.isConnected(true);
    }
}


//-----------------------------------------------------------------------------
void app::handleIntr(void) {
    uint8_t lostCnt = 0, pipe, len;
    NRF24_packet_t *p;

    DISABLE_IRQ;

    while(mRadio->available(&pipe)) {
        if(!mBufCtrl->full()) {
            p = mBufCtrl->getFront();
            memset(p->packet, 0xcc, MAX_RF_PAYLOAD_SIZE);
            p->sendCh = mSendChannel;
            len = mRadio->getPayloadSize();
            if(len > MAX_RF_PAYLOAD_SIZE)
                len = MAX_RF_PAYLOAD_SIZE;

            mRadio->read(p->packet, len);
            mBufCtrl->pushFront(p);
            lostCnt = 0;
        }
        else {
            bool tx_ok, tx_fail, rx_ready;
            if(lostCnt < 255)
                lostCnt++;
            mRadio->whatHappened(tx_ok, tx_fail, rx_ready); // reset interrupt status
            mRadio->flush_rx(); // drop the packet
        }
    }

    RESTORE_IRQ;
}


//-----------------------------------------------------------------------------
void app::initRadio(void) {
    mRadio = new RF24(RF24_CE_PIN, RF24_CS_PIN);

    mRadio->begin();
    mRadio->setAutoAck(false);
    mRadio->setRetries(0, 0);

    mRadio->setChannel(DEFAULT_RECV_CHANNEL);
    mRadio->setDataRate(RF24_250KBPS);
    mRadio->disableCRC();
    mRadio->setAutoAck(false);
    mRadio->setPayloadSize(MAX_RF_PAYLOAD_SIZE);
    mRadio->setAddressWidth(5);
    mRadio->openReadingPipe(1, DTU_RADIO_ID);

    // enable only receiving interrupts
    mRadio->maskIRQ(true, true, false);

    // Use lo PA level, as a higher level will disturb CH340 serial usb adapter
    mRadio->setPALevel(RF24_PA_MAX);
    mRadio->startListening();

    Serial.println("Radio Config:");
    mRadio->printPrettyDetails();

    mSendChannel = mHoymiles->getDefaultChannel();
}


//-----------------------------------------------------------------------------
void app::sendPacket(uint8_t buf[], uint8_t len) {
    DISABLE_IRQ;
    mRadio->stopListening();

#ifdef CHANNEL_HOP
    if(mSendCnt % 6 == 0)
        mSendChannel = mHoymiles->getNxtChannel();
    else
        mSendChannel = mHoymiles->getLastChannel();
#else
    mSendChannel = mHoymiles->getDefaultChannel();
#endif
    mRadio->setChannel(mSendChannel);
    //Serial.println("CH: " + String(mSendChannel));

    mRadio->openWritingPipe(mHoymiles->mRadioId);
    mRadio->setCRCLength(RF24_CRC_16);
    mRadio->enableDynamicPayloads();
    mRadio->setAutoAck(true);
    mRadio->setRetries(3, 15);

    mRadio->write(buf, len);

    // Try to avoid zero payload acks (has no effect)
    mRadio->openWritingPipe(DUMMY_RADIO_ID); // TODO: why dummy radio id?

    mRadio->setAutoAck(false);
    mRadio->setRetries(0, 0);
    mRadio->disableDynamicPayloads();
    mRadio->setCRCLength(RF24_CRC_DISABLED);

    mRadio->setChannel(DEFAULT_RECV_CHANNEL);
    mRadio->startListening();

    RESTORE_IRQ;
}


//-----------------------------------------------------------------------------
void app::sendTicker(void) {
    mFlagSend = true;
}


//-----------------------------------------------------------------------------
void app::mqttTicker(void) {
    mMqttEvt = true;
}


//-----------------------------------------------------------------------------
void app::showIndex(void) {
    String html = index_html;
    html.replace("{DEVICE}", mDeviceName);
    html.replace("{VERSION}", mVersion);
    mWeb->send(200, "text/html", html);
}


//-----------------------------------------------------------------------------
void app::showSetup(void) {
    // overrides same method in main.cpp

    uint16_t interval;

    String html = setup_html;
    html.replace("{SSID}", mStationSsid);
    // PWD will be left at the default value (for protection)
    // -> the PWD will only be changed if it does not match the placeholder "{PWD}"

    char addr[20] = {0};
    sprintf(addr, "%02X:%02X:%02X:%02X:%02X:%02X", mHoymiles->mAddrBytes[0], mHoymiles->mAddrBytes[1], mHoymiles->mAddrBytes[2], mHoymiles->mAddrBytes[3], mHoymiles->mAddrBytes[4], mHoymiles->mAddrBytes[5]);
    html.replace("{INV0_ADDR}", String(addr));

    html.replace("{DEVICE}", String(mDeviceName));
    html.replace("{VERSION}", String(mVersion));

    if(mSettingsValid) {
        mEep->read(ADDR_INV_INTERVAL, &interval);
        html.replace("{INV_INTERVAL}", String(interval));

        uint8_t mqttAddr[MQTT_ADDR_LEN] = {0};
        mEep->read(ADDR_MQTT_ADDR,     mqttAddr, MQTT_ADDR_LEN);
        mEep->read(ADDR_MQTT_INTERVAL, &interval);

        char addr[16] = {0};
        sprintf(addr, "%d.%d.%d.%d", mqttAddr[0], mqttAddr[1], mqttAddr[2], mqttAddr[3]);
        html.replace("{MQTT_ADDR}",     String(addr));
        html.replace("{MQTT_USER}",     String(mMqtt.getUser()));
        html.replace("{MQTT_PWD}",      String(mMqtt.getPwd()));
        html.replace("{MQTT_TOPIC}",    String(mMqtt.getTopic()));
        html.replace("{MQTT_INTERVAL}", String(interval));
    }
    else {
        html.replace("{INV_INTERVAL}", "1000");

        html.replace("{MQTT_ADDR}", "");
        html.replace("{MQTT_USER}", "");
        html.replace("{MQTT_PWD}", "");
        html.replace("{MQTT_TOPIC}", "/inverter");
        html.replace("{MQTT_INTERVAL}", "10000");
    }

    mWeb->send(200, "text/html", html);
}


//-----------------------------------------------------------------------------
void app::showSave(void) {
    saveValues(true);
}


//-----------------------------------------------------------------------------
void app::showCmdStatistics(void) {
    String content = "CMDs:\n";
    content += String("0x01: ") + String(mCmds[0]) + String("\n");
    content += String("0x02: ") + String(mCmds[1]) + String("\n");
    content += String("0x03: ") + String(mCmds[2]) + String("\n");
    content += String("0x81: ") + String(mCmds[3]) + String("\n");
    content += String("0x84: ") + String(mCmds[4]) + String("\n");
    content += String("other: ") + String(mCmds[5]) + String("\n");

    content += "\nCHANNELs:\n";
    content += String("23: ") + String(mChannelStat[0]) + String("\n");
    content += String("40: ") + String(mChannelStat[1]) + String("\n");
    content += String("61: ") + String(mChannelStat[2]) + String("\n");
    content += String("75: ") + String(mChannelStat[3]) + String("\n");
    mWeb->send(200, "text/plain", content);
}


//-----------------------------------------------------------------------------
void app::showHoymiles(void) {
    String html = hoymiles_html;
    html.replace("{DEVICE}", mDeviceName);
    html.replace("{VERSION}", mVersion);
    mWeb->send(200, "text/html", html);
}


//-----------------------------------------------------------------------------
void app::showLiveData(void) {
    String modHtml = "";

    String unit[5] = {"V", "A", "W", "Wh", "kWh"};
    String info[5] = {"VOLTAGE", "CURRENT", "POWER", "YIELD DAY", "YIELD"};

    for(uint8_t i = 0; i < 4; i++) {
        modHtml += "<div class=\"module\"><span class=\"header\">CHANNEL " + String(i) + "</span>";
        for(uint8_t j = 0; j < 5; j++) {
            modHtml += "<span class=\"value\">";
            switch(j) {
                default: modHtml += String(mDecoder->mData.ch_dc[i/2].u); break;
                case 1:  modHtml += String(mDecoder->mData.ch_dc[i].i);   break;
                case 2:  modHtml += String(mDecoder->mData.ch_dc[i].p);   break;
                case 3:  modHtml += String(mDecoder->mData.ch_dc[i].y_d); break;
                case 4:  modHtml += String(mDecoder->mData.ch_dc[i].y_t); break;
            }
            modHtml += "<span class=\"unit\">" + unit[j] + "</span></span>";
            modHtml += "<span class=\"info\">" + info[j] + "</span>";
        }
        modHtml += "</div>";
    }

    mWeb->send(200, "text/html", modHtml);
}


//-----------------------------------------------------------------------------
void app::showMqtt(void) {
    String txt = "connected";
    if(mMqtt.isConnected())
        txt = "not " + txt;
    mWeb->send(200, "text/plain", txt);
}


//-----------------------------------------------------------------------------
void app::saveValues(bool webSend = true) {
    Main::saveValues(false); // general configuration

    if(mWeb->args() > 0) {
        char *p;
        char addr[20] = {0};
        uint8_t i = 0;
        uint16_t interval;

        // hoymiles
        memset(mHoymiles->mAddrBytes, 0, 6);
        mWeb->arg("inv0Addr").toCharArray(addr, 20);
        p = strtok(addr, ":");
        while(NULL != p) {
            mHoymiles->mAddrBytes[i++] = strtol(p, NULL, 16);
            p = strtok(NULL, ":");
        }
        interval = mWeb->arg("invInterval").toInt();
        mEep->write(ADDR_INV0_ADDR, mHoymiles->mAddrBytes, INV_ADDR_LEN);
        mEep->write(ADDR_INV_INTERVAL, interval);


        // mqtt
        uint8_t mqttAddr[MQTT_ADDR_LEN] = {0};
        char mqttUser[MQTT_USER_LEN];
        char mqttPwd[MQTT_PWD_LEN];
        char mqttTopic[MQTT_TOPIC_LEN];
        mWeb->arg("mqttAddr").toCharArray(addr, 20);
        i = 0;
        p = strtok(addr, ".");
        while(NULL != p) {
            mqttAddr[i++] = atoi(p);
            p = strtok(NULL, ".");
        }
        mWeb->arg("mqttUser").toCharArray(mqttUser, MQTT_USER_LEN);
        mWeb->arg("mqttPwd").toCharArray(mqttPwd, MQTT_PWD_LEN);
        mWeb->arg("mqttTopic").toCharArray(mqttTopic, MQTT_TOPIC_LEN);
        interval = mWeb->arg("mqttInterval").toInt();
        mEep->write(ADDR_MQTT_ADDR, mqttAddr, MQTT_ADDR_LEN);
        mEep->write(ADDR_MQTT_USER, mqttUser, MQTT_USER_LEN);
        mEep->write(ADDR_MQTT_PWD,  mqttPwd,  MQTT_PWD_LEN);
        mEep->write(ADDR_MQTT_TOPIC, mqttTopic, MQTT_TOPIC_LEN);
        mEep->write(ADDR_MQTT_INTERVAL, interval);

        updateCrc();
        if((mWeb->arg("reboot") == "on"))
            showReboot();
        else {
            mWeb->send(200, "text/html", "<!doctype html><html><head><title>Setup saved</title><meta http-equiv=\"refresh\" content=\"0; URL=/setup\"></head><body>"
                "<p>saved</p></body></html>");
        }
    }
    else {
        mWeb->send(200, "text/html", "<!doctype html><html><head><title>Error</title><meta http-equiv=\"refresh\" content=\"3; URL=/setup\"></head><body>"
            "<p>Error while saving</p></body></html>");
    }
}


//-----------------------------------------------------------------------------
void app::dumpBuf(uint8_t buf[], uint8_t len) {
    for(uint8_t i = 0; i < len; i ++) {
        if((i % 8 == 0) && (i != 0))
            Serial.println();
        Serial.print(String(buf[i], HEX) + " ");
    }
    Serial.println();
}
