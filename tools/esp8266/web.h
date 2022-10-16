//-----------------------------------------------------------------------------
// 2022 Ahoy, https://www.mikrocontroller.net/topic/525778
// Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//-----------------------------------------------------------------------------

#ifndef __WEB_H__
#define __WEB_H__

#include "dbg.h"
#ifdef ESP32
    #include "AsyncTCP.h"
    #include "Update.h"
#else
    #include "ESPAsyncTCP.h"
#endif
#include "ESPAsyncWebServer.h"
#include "app.h"
#include "webApi.h"

#define WEB_SERIAL_BUF_SIZE 2048

class app;
class webApi;

class web {
    public:
        web(app *main, sysConfig_t *sysCfg, config_t *config, statistics_t *stat, char version[]);
        ~web() {}

        void setup(void);
        void loop(void);

        void onConnect(AsyncEventSourceClient *client);

        void onIndex(AsyncWebServerRequest *request);
        void onCss(AsyncWebServerRequest *request);
        void onApiJs(AsyncWebServerRequest *request);
        void onFavicon(AsyncWebServerRequest *request);
        void showNotFound(AsyncWebServerRequest *request);
        void onReboot(AsyncWebServerRequest *request);
        void showErase(AsyncWebServerRequest *request);
        void showFactoryRst(AsyncWebServerRequest *request);
        void onSetup(AsyncWebServerRequest *request);
        void showSave(AsyncWebServerRequest *request);

        void onLive(AsyncWebServerRequest *request);
        void showWebApi(AsyncWebServerRequest *request);

        void onUpdate(AsyncWebServerRequest *request);
        void showUpdate(AsyncWebServerRequest *request);
        void showUpdate2(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

        void serialCb(String msg);

    private:
        void onSerial(AsyncWebServerRequest *request);

        AsyncWebServer *mWeb;
        AsyncEventSource *mEvts;

        config_t *mConfig;
        sysConfig_t *mSysCfg;
        statistics_t *mStat;
        char *mVersion;
        app *mMain;
        webApi *mApi;

        bool mSerialAddTime;
        char mSerialBuf[WEB_SERIAL_BUF_SIZE];
        uint16_t mSerialBufFill;
        uint32_t mWebSerialTicker;
        uint32_t mWebSerialInterval;
};

#endif /*__WEB_H__*/
