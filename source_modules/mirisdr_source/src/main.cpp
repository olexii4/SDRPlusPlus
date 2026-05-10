#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <mirisdr.h>
#include <atomic>

#ifdef __ANDROID__
#include <android_backend.h>
#endif

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "mirisdr_source",
    /* Description:     */ "MSI-SDR source module for SDR++ (MSi2500/SDRplay RSP1)",
    /* Author:          */ "olexii4",
    /* Version:         */ 0, 3, 0,
    /* Max instances    */ 1
};

ConfigManager config;

#define STREAM_MAX_RETRIES   10
#define STREAM_RETRY_DELAY_MS 1000
#define ASYNC_BUF_COUNT      8
#define ASYNC_BUF_SIZE       (16 * 16384)

static const double sampleRates[] = {
    1536000, 2048000, 4000000, 6000000, 8000000, 10000000,
};
static const char* sampleRatesTxt[] = {
    "1.536 MHz", "2.048 MHz", "4 MHz", "6 MHz", "8 MHz", "10 MHz",
};
static const int NUM_SAMPLE_RATES = 6;

#ifdef __ANDROID__
static const std::vector<backend::DevVIDPID> MIRISDR_VIDPIDS = {
    { 0x1df7, 0x2500 },
    { 0x1df7, 0x3000 },
    { 0x1df7, 0x3010 },
    { 0x2040, 0xd300 },
    { 0x07ca, 0x8591 },
    { 0x04bb, 0x0537 },
    { 0x0511, 0x0037 },
};
#endif

class MsiSdrSourceModule : public ModuleManager::Instance {
public:
    MsiSdrSourceModule(std::string name) : name(name) {
        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        for (int i = 0; i < NUM_SAMPLE_RATES; i++) {
            sampleRateListTxt += sampleRatesTxt[i];
            sampleRateListTxt += '\0';
        }

        config.acquire();
        if (config.conf.contains("device") && config.conf["device"].is_string()) {
            selectedDevName = config.conf["device"];
        }
        if (config.conf.contains("devices") && config.conf["devices"].contains(selectedDevName)) {
            auto& devConf = config.conf["devices"][selectedDevName];
            if (devConf.contains("sampleRate")) {
                double sr = devConf["sampleRate"];
                for (int i = 0; i < NUM_SAMPLE_RATES; i++) {
                    if (sampleRates[i] == sr) { srId = i; break; }
                }
            }
            if (devConf.contains("gain")) {
                gainId = devConf["gain"];
                if (gainId > 102) gainId = 102;
                if (gainId < 0) gainId = 0;
            }
        }
        config.release(false);

        refresh();

        sigpath::sourceManager.registerSource("MSI-SDR", &handler);
    }

    ~MsiSdrSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("MSI-SDR");
    }

    void postInit() {}
    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() { return enabled; }

private:
    void refresh() {
        devNames.clear();
        devListTxt = "";

#ifndef __ANDROID__
        devCount = mirisdr_get_device_count();
        flog::info("MSI-SDR: refresh found {0} device(s)", devCount);
        char venBuf[256], prodBuf[256], snBuf[256];
        for (uint32_t i = 0; i < devCount; i++) {
            const char* devName = mirisdr_get_device_name(i);
            int snErr = mirisdr_get_device_usb_strings(i, venBuf, prodBuf, snBuf);

            char buf[1024];
            if (venBuf[0] && prodBuf[0]) {
                snprintf(buf, sizeof(buf), "%s %s [%s]##%d", venBuf, prodBuf,
                        (!snErr && snBuf[0]) ? snBuf : "No Serial", i);
            } else {
                snprintf(buf, sizeof(buf), "%s [%s]##%d", devName,
                        (!snErr && snBuf[0]) ? snBuf : "No Serial", i);
            }
            devNames.push_back(buf);
            devListTxt += buf;
            devListTxt += '\0';
        }
#else
        devCount = 0;
        int vid, pid;
        devFd = backend::getDeviceFD(vid, pid, MIRISDR_VIDPIDS);
        if (devFd < 0) return;

        devCount = 1;
        std::string devName;
        if (vid == 0x1df7 && pid == 0x2500) devName = "MSi2500 / RSP1C";
        else if (vid == 0x1df7 && pid == 0x3000) devName = "SDRplay RSP1A";
        else if (vid == 0x1df7 && pid == 0x3010) devName = "SDRplay RSP2";
        else devName = "MSI-SDR Device";
        devNames.push_back(devName);
        devListTxt += devName;
        devListTxt += '\0';
#endif

        if (devCount > 0 && devId >= (int)devCount) devId = 0;
    }

    void saveDeviceConfig() {
        config.acquire();
        config.conf["device"] = selectedDevName;
        config.conf["devices"][selectedDevName]["sampleRate"] = sampleRates[srId];
        config.conf["devices"][selectedDevName]["gain"] = gainId;
        config.release(true);
    }

    static void menuSelected(void* ctx) {
        MsiSdrSourceModule* _this = (MsiSdrSourceModule*)ctx;
        core::setInputSampleRate(sampleRates[_this->srId]);
        flog::info("MSI-SDR: selected");
    }

    static void menuDeselected(void* ctx) {
        flog::info("MSI-SDR: deselected");
    }

    static void start(void* ctx) {
        MsiSdrSourceModule* _this = (MsiSdrSourceModule*)ctx;
        if (_this->running) return;
        if (_this->devCount == 0) {
            flog::error("MSI-SDR: no device available");
            return;
        }

        _this->deviceError = false;
        _this->openDev = NULL;

        int r;
#ifndef __ANDROID__
        r = mirisdr_open(&_this->openDev, _this->devId);
#else
        r = mirisdr_open_fd(&_this->openDev, _this->devFd);
#endif
        if (r < 0 || !_this->openDev) {
            flog::error("MSI-SDR: failed to open device (code {0})", r);
            return;
        }

        _this->selectedDevName = (_this->devId < (int)_this->devNames.size())
                                 ? _this->devNames[_this->devId] : "unknown";

        mirisdr_dev_t* dev = _this->openDev;
        double sr = sampleRates[_this->srId];
        int gain = _this->gainId;

        mirisdr_set_sample_rate(dev, (uint32_t)sr);
        mirisdr_set_center_freq(dev, (uint32_t)_this->freq);
        mirisdr_set_sample_format(dev, "AUTO");
        mirisdr_set_if_freq(dev, 0);
        mirisdr_set_bandwidth(dev, 8000000);
        mirisdr_set_hw_flavour(dev, MIRISDR_HW_DEFAULT);
        mirisdr_set_tuner_gain_mode(dev, 1);
        mirisdr_set_tuner_gain(dev, gain);

        _this->running = true;
        _this->workerThread = std::thread(&MsiSdrSourceModule::worker, _this);

        flog::info("MSI-SDR: started (SR={0}, gain={1} dB, freq={2})",
                   sr, gain, _this->freq);
    }

    static void stop(void* ctx) {
        MsiSdrSourceModule* _this = (MsiSdrSourceModule*)ctx;
        if (!_this->running) return;

        _this->running = false;
        _this->stream.stopWriter();

        if (!_this->deviceError && _this->openDev) {
            mirisdr_cancel_async(_this->openDev);
        }

        if (_this->workerThread.joinable()) {
            _this->workerThread.join();
        }

        _this->stream.clearWriteStop();

        if (_this->openDev) {
            if (!_this->deviceError) {
                mirisdr_close(_this->openDev);
            }
            _this->openDev = NULL;
        }

        _this->saveDeviceConfig();
        flog::info("MSI-SDR: stopped");
    }

    static void tune(double freq, void* ctx) {
        MsiSdrSourceModule* _this = (MsiSdrSourceModule*)ctx;
        _this->freq = freq;
        if (_this->running && !_this->deviceError && _this->openDev) {
            mirisdr_set_center_freq(_this->openDev, (uint32_t)freq);
        }
    }

    static void menuHandler(void* ctx) {
        MsiSdrSourceModule* _this = (MsiSdrSourceModule*)ctx;

#ifdef __ANDROID__
        // Auto-refresh every ~3 s while no device is detected and not streaming,
        // so the user doesn't have to press Refresh after granting USB permission.
        if (!_this->running && _this->devCount == 0) {
            if (++_this->autoRefreshFrames >= 60) {
                _this->autoRefreshFrames = 0;
                _this->refresh();
            }
        } else {
            _this->autoRefreshFrames = 0;
        }
#endif

        if (_this->running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_msisdr_dev_", _this->name), &_this->devId,
                         _this->devListTxt.c_str())) {
            // device selection changed — will take effect on next start
        }

        if (SmGui::Combo(CONCAT("##_msisdr_sr_", _this->name), &_this->srId,
                         _this->sampleRateListTxt.c_str())) {
            core::setInputSampleRate(sampleRates[_this->srId]);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_msisdr_", _this->name))) {
            _this->refresh();
        }

        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Gain");
        SmGui::FillWidth();
        char gainTxt[64];
        snprintf(gainTxt, sizeof(gainTxt), "%d dB", _this->gainId);
        if (ImGui::SliderInt(CONCAT("##_msisdr_gain_", _this->name),
                             &_this->gainId, 0, 102, gainTxt)) {
            if (_this->running && !_this->deviceError && _this->openDev) {
                mirisdr_set_tuner_gain(_this->openDev, _this->gainId);
            }
        }
    }

    void worker() {
        int retries = 0;

        mirisdr_reset_buffer(openDev);

        while (running && retries < STREAM_MAX_RETRIES) {
            int r = mirisdr_read_async(openDev, asyncHandler, this,
                                       ASYNC_BUF_COUNT, ASYNC_BUF_SIZE);

            if (!running) break;

            if (mirisdr_reset_buffer(openDev) < 0) {
                flog::error("MSI-SDR: USB device disconnected");
                deviceError = true;
                break;
            }

            retries++;
            flog::warn("MSI-SDR: streaming error (code {0}), retry {1}/{2}",
                       r, retries, STREAM_MAX_RETRIES);
            std::this_thread::sleep_for(std::chrono::milliseconds(STREAM_RETRY_DELAY_MS));
        }

        if (running && retries >= STREAM_MAX_RETRIES) {
            flog::error("MSI-SDR: device lost after {0} retries", retries);
            deviceError = true;
        }
    }

    static void asyncHandler(unsigned char* buf, uint32_t len, void* ctx) {
        MsiSdrSourceModule* _this = (MsiSdrSourceModule*)ctx;
        if (!_this->running) return;

        int16_t* samples = (int16_t*)buf;
        int sampleCount = len / 4;

        for (int i = 0; i < sampleCount; i++) {
            _this->stream.writeBuf[i].re = (float)samples[i * 2] / 32768.0f;
            _this->stream.writeBuf[i].im = -(float)samples[i * 2 + 1] / 32768.0f;
        }
        if (!_this->stream.swap(sampleCount)) return;
    }

    std::string name;
    bool enabled = true;

    mirisdr_dev_t* openDev = NULL;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    std::thread workerThread;

    std::atomic<bool> running{false};
    std::atomic<bool> deviceError{false};

    double freq = 100000000.0;
    int devId = 0;
    int srId = 1;
    int gainId = 40;
    uint32_t devCount = 0;

    std::string selectedDevName;
    std::vector<std::string> devNames;
    std::string devListTxt;
    std::string sampleRateListTxt;

#ifdef __ANDROID__
    int devFd = -1;
    int autoRefreshFrames = 0;
#endif
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/mirisdr_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new MsiSdrSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (MsiSdrSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
