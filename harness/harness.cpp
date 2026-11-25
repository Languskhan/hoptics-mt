// Host harness: renders HopticsUI into an in-memory RGB565 display with fake mesh data and writes
// one PPM screenshot per screen.
//   ./harness                       240x320 portrait (Heltec kits) into shots/
//   ./harness 320x240 shots_320x240 landscape (T-Deck Plus, ThinkNode M9) into its own folder
#define private public
#include "graphics/view/TFT/HopticsUI.h"
#undef private
#include "graphics/view/TFT/HopticsReport.h"
#include "lvgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static int WD = 240, HT = 320;
static std::string outDir = "shots";
static uint16_t fb[320 * 320];
static uint32_t fakeMs = 0;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++)
            fb[y * WD + x] = *src++;
    lv_display_flush_ready(disp);
}
static uint32_t tick_cb(void) { return fakeMs; }

static HopticsUI *gui = nullptr;

// let the map worker finish (tiles come from local files here), then pick up its picture
static void settleMap(void)
{
    for (int i = 0; i < 2000 && !hoptics::TileMap::get().idle(); i++)
        usleep(1000);
    usleep(3000);
    if (gui) {
        gui->mapNoteTick = 0;
        gui->service();
    }
}

static void pump(int ms)
{
    for (int i = 0; i < ms; i += 10) {
        fakeMs += 10;
        lv_timer_handler();
        if (gui)
            gui->service();
    }
    lv_refr_now(NULL);
}

static void shot(const char *name)
{
    settleMap();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    std::string path = outDir + "/" + name + ".ppm";
    FILE *f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", WD, HT);
    for (int i = 0; i < WD * HT; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1f) * 255 / 31), (uint8_t)(((c >> 5) & 0x3f) * 255 / 63),
                          (uint8_t)((c & 0x1f) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path.c_str());
}

int main(int argc, char **argv)
{
    if (argc > 1 && sscanf(argv[1], "%dx%d", &WD, &HT) != 2) {
        fprintf(stderr, "usage: %s [WxH] [out dir]\n", argv[0]);
        return 2;
    }
    if (WD * HT > (int)(sizeof(fb) / sizeof(fb[0])))
        return 2;
    if (argc > 2)
        outDir = argv[2];
    setenv("TZ", "PST8PDT", 1);
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(WD, HT);
    static uint16_t buf[320 * 40]; // partial render buffer: 40 lines
    lv_display_set_buffers(d, buf, NULL, WD * 40 * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    // street map: stand-in tiles from make_test_tiles.py instead of the internet, saved tiles in a scratch dir
    // scratch files go under $HARNESS_TMP (default /tmp), so two checkouts can render at the same time
    const char *tmp = getenv("HARNESS_TMP") ? getenv("HARNESS_TMP") : "/tmp";
    const std::string cache = std::string(tmp) + "/hm-harness", tiles = std::string(tmp) + "/tilesrc";
    system(("rm -rf '" + cache + "'").c_str());
    hoptics::TileMap::hostSetup(cache.c_str(), tiles.c_str());
    HopticsUI ui;
    gui = &ui;
    uint32_t now = 1795999000; // late Nov 2026
    ui.setTime(now);
    ui.createBootScreen("2.8.3.e42ae67");
    ui.setBootStatus("Syncing 47 nodes...");
    pump(400);
    shot("00_boot");

    const uint32_t me = 0xa1b2c3d4;
    ui.setMyNode(me);
    ui.upsertNode(me, 0, now, "HOPT", "Hoptics V4 Kit", 0, true, false, 110);
    ui.updatePosition(me, 386950000, -1212650000, 70);
    ui.updateMetrics(me, 87, 4.02f, 14.6f, 2.3f);
    ui.setChannel(0, "", true);
    ui.setChannel(1, "Hikers", true);
    ui.setChannel(2, "Ops", true);
    ui.setRadio(1, "US", 0, "LongFast", true, 3, 27, 906.875f);
    ui.setDeviceRole(0);
    ui.setUiConfig(153, 30, true);
    ui.setDeviceInfo("2.8.3.e42ae67", 110, true, true);
    ui.setBluetoothEnabled(false);
    ui.setConnected(true);
    ui.setNetwork(true, true, (192) | (168 << 8) | (1 << 16) | (57u << 24), true, -58);

    struct N {
        uint32_t num;
        const char *s, *l;
        uint8_t role;
        int hops;
        int rssi;
        float snr;
        int batt;
        int32_t lat, lon;
        int ago;
    } ns[] = {
        {0x11aa2201, "RDGE", "Ridge Repeater", 2, 0, -78, 9.5f, 101, 387700000, -1210800000, 40},
        {0x11aa2202, "FLSM", "Lake Node", 0, 0, -96, 4.25f, 72, 386800000, -1211700000, 95},
        {0x11aa2203, "AUBN", "Ridge East", 11, 1, 0, 0, 101, 389000000, -1210700000, 300},
        {0x11aa2204, "OVL1", "Home Base", 0, 0, -61, 11.0f, 100, 386790000, -1212300000, 12},
        {0x11aa2205, "TRK7", "Trail Tracker 7", 5, 2, 0, 0, 34, 0, 0, 1800},
        {0x11aa2206, "WX01", "Weather Station", 6, 1, 0, 0, 58, 386500000, -1213500000, 600},
        {0x11aa2207, "\xF0\x9F\x93\xA1", "Rooftop \xF0\x9F\x93\xA1 Gateway", 2, 0, -104, -6.5f, 101, 386400000, -1213000000, 150},
        {0x11aa2208, "CAMP", "Camp Kit", 0, 3, 0, 0, 12, 0, 0, 5400},
        {0x11aa2209, "RSVL", "Hilltop North", 0, 2, 0, 0, 80, 387500000, -1212900000, 2400},
        {0x11aa220a, "GRLD", "Mobile Unit", 1, 1, 0, 0, 66, 0, 0, 900},
        {0x11aa220b, "c3f1", "", 0, -1, 0, 0, 255, 0, 0, 40000},
    };
    for (auto &n : ns) {
        ui.upsertNode(n.num, 0, now - n.ago, n.s, n.l, n.role, true, false, 0);
        if (n.hops == 0)
            ui.updateSignal(n.num, n.rssi, n.snr);
        else
            ui.updateHops(n.num, n.hops);
        if (n.batt != 255)
            ui.updateMetrics(n.num, n.batt, 3.9f, 8.0f, 1.0f);
        if (n.lat)
            ui.updatePosition(n.num, n.lat, n.lon, 120);
    }
    // a few SNR samples for the detail chart
    float snrs[] = {6, 7.5f, 5, 9, 10, 8.25f, 9.5f, 11, 9, 9.5f};
    for (float s : snrs)
        ui.updateSignal(0x11aa2201, -80 + (int)s, s);

    ui.buildMainScreen();
    lv_screen_load(ui.mainScreen);

    // environment: telemetry settings, 5.5 h of 5-minute history, then live readings below
    ui.setEnvConfig(false, 3600, false);
    auto envAt = [](int minutesAgo) {
        HopticsUI::EnvReading r;
        float x = minutesAgo / 60.0f;
        r.temp = 22.4f - 1.6f * sinf(x * 0.55f) - 0.04f * x * x;
        r.hum = 46.0f + 5.0f * sinf(x * 0.4f + 1.0f);
        r.press = 1013.1f + 0.62f * x;
        return r;
    };
    for (int i = 66; i > 0; i--) {
        HopticsUI::EnvReading r = envAt(60 + i * 5);
        float v[3] = {r.temp, r.hum, r.press};
        ui.envPush(v);
    }
    {
        HopticsUI::EnvReading w;
        w.temp = 11.6f;
        w.hum = 71.0f;
        w.press = 1012.4f;
        ui.updateEnvironment(0x11aa2206, w);
        ui.nodePtr(0x11aa2206)->envTime = now - 420;
        HopticsUI::EnvReading g;
        g.temp = 8.2f;
        g.hum = 58.0f;
        ui.updateEnvironment(0x11aa2201, g);
        ui.nodePtr(0x11aa2201)->envTime = now - 1500;
        HopticsUI::EnvReading o;
        o.temp = 18.9f;
        o.hum = 49.0f;
        o.press = 1014.0f;
        ui.updateEnvironment(0x11aa2204, o);
        ui.nodePtr(0x11aa2204)->envTime = now - 2700;
    }

    // simulate an hour of history, minute by minute
    uint32_t t = now - 3600;
    ui.lastMinute = t;
    srand(7);
    for (int m = 0; m < 60; m++) {
        ui.updateEnvironment(me, envAt(60 - m));
        int pk = 3 + (rand() % 7) + (m > 40 ? 6 : 0);
        for (int p = 0; p < pk; p++) {
            const N &n = ns[rand() % 10];
            static const uint16_t ports[] = {3, 4, 67, 67, 5, 1, 3, 70, 71, 67};
            uint16_t port = ports[rand() % 10];
            int8_t hops = n.hops;
            ui.onPacket(n.num, 0xffffffff, port, hops, hops == 0 ? n.rssi + (rand() % 9) - 4 : 0,
                        hops == 0 ? n.snr + ((rand() % 7) - 3) * 0.5f : 0, t + p, false);
        }
        ui.lastChUtil = 9 + (rand() % 8) + (m > 40 ? 8 : 0);
        ui.lastAirUtil = 1.5f + (rand() % 20) / 10.0f;
        t += 60;
        ui.tick(t);
    }
    ui.updateMetrics(me, 87, 4.02f, 18.4f, 2.6f);
    // final fresh packets
    ui.onPacket(0x11aa2204, 0xffffffff, 1, 0, -61, 11.0f, now - 12, false);
    ui.onPacket(0x11aa2201, 0xffffffff, 67, 0, -78, 9.5f, now - 40, false);
    ui.onPacket(0x11aa2203, 0xffffffff, 3, 1, 0, 0, now - 64, false);
    ui.onPacket(0x11aa2207, me, 5, 0, -104, -6.5f, now - 70, false);
    ui.onPacket(0x11aa2209, 0xffffffff, 4, 0, 0, 0, now - 90, true);
    ui.onPacket(0x11aa2205, 0xffffffff, 3, 0, 0, 0, now - 30, true);
    ui.onPacket(0x11aa2208, 0xffffffff, 1, 0, 0, 0, now - 20, true);

    // messages
    ui.addMessage(1, 0x11aa2204, now - 1500, "Net check-in tonight at 8 on Hikers", 0, 0, true);
    ui.addMessage(1, 0x11aa2201, now - 1400, "Ridge copies, solar at 100%", 0, 0, true);
    ui.addMessage(1, me, now - 1300, "Hoptics checking in from home base \xF0\x9F\x91\x8B", 11, 4, true);
    ui.addMessage(1, 0x11aa2203, now - 600, "Ridge East here, 2 hops but solid", 0, 7, true);
    ui.addMessage(0, 0x11aa2202, now - 3000, "anyone near the lake?", 0, 7, true);
    ui.addMessage(0x11aa2204, 0x11aa2204, now - 200, "Want to test range from the overlook tomorrow?", 0, 7, true);
    ui.addMessage(0x11aa2204, me, now - 120, "Yes! I'll bring the V4 kit", 12, 2, true);
    ui.tick(now);

    ui.selectTab(HopticsUI::T_HOME);
    pump(300);
    { lv_mem_monitor_t mon; lv_mem_monitor(&mon); printf("LVGL mem used: %u KB of %u KB (max %u KB, frag %u%%)\n", (unsigned)((mon.total_size-mon.free_size)/1024), (unsigned)(mon.total_size/1024), (unsigned)(mon.max_used/1024), (unsigned)mon.frag_pct); }
    shot("01_home");
    lv_obj_scroll_to_y(ui.pages[HopticsUI::T_HOME], 200, LV_ANIM_OFF);
    shot("01b_home_scrolled");
    lv_obj_scroll_to_y(ui.pages[HopticsUI::T_HOME], 0, LV_ANIM_OFF);

    ui.selectTab(HopticsUI::T_LIVE);
    shot("02_live");
    ui.selectTab(HopticsUI::T_CHAT);
    shot("03_chats");
    ui.selectTab(HopticsUI::T_NODES);
    shot("04_nodes");
    ui.selectTab(HopticsUI::T_STATS);
    shot("05_stats");
    lv_obj_scroll_to_y(ui.pages[HopticsUI::T_STATS], 230, LV_ANIM_OFF);
    shot("05b_stats_scrolled");
    lv_obj_scroll_to_y(ui.pages[HopticsUI::T_STATS], 600, LV_ANIM_OFF);
    shot("05c_stats_bottom");

    ui.selectTab(HopticsUI::T_LIVE);
    ui.liveFilter = HopticsUI::LF_MQTT;
    ui.refreshLive();
    shot("02b_live_mqtt");
    ui.liveFilter = HopticsUI::LF_ALL;

    ui.selectTab(HopticsUI::T_MAP);
    pump(50);
    shot("11_map");
    ui.onPacket(0x11aa2201, 0xffffffff, 67, 0, -78, 9.5f, now, false);
    ui.onPacket(0x11aa2209, 0xffffffff, 3, 2, 0, 0, now, false);
    ui.onPacket(0x11aa2206, 0xffffffff, 67, 1, 0, 0, now, true);
    pump(350);
    shot("11b_map_pings");
    pump(2000);
    // street level, then drag
    for (int i = 0; i < 4; i++) {
        ui.mapZ++;
        ui.mapCX *= 2;
        ui.mapCY *= 2;
    }
    ui.mapUserZoom = true;
    ui.refreshMap();
    shot("12_map_street");
    ui.mapCommitPan(-70, 45);
    shot("13_map_dragged");
    // a zoom level with nothing saved while Wi-Fi is off
    hoptics::TileMap::hostSetOnline(false);
    ui.mapZ++;
    ui.mapCX *= 2;
    ui.mapCY *= 2;
    ui.refreshMap();
    shot("14_map_offline");
    hoptics::TileMap::hostSetOnline(true);
    pump(100);
    shot("14b_map_back_online");
    // Fit again for the theme shots
    ui.mapUserZoom = false;
    ui.mapUserPan = false;
    ui.mapFit();
    ui.refreshMap();
    {
        hoptics::MapStatus ms = hoptics::TileMap::get().status();
        printf("map: need %u shown %u pending %u saved %u tiles %u KB z=%d\n", ms.need, ms.shown, ms.pending, ms.savedTiles,
               ms.savedKB, ui.mapZ);
    }

    ui.openChat(1);
    pump(100);
    shot("06_chat");
    ui.showKeyboard(ui.chatInput);
    lv_textarea_set_text(ui.chatInput, "On my way");
    pump(100);
    shot("07_chat_keyboard");
    ui.hideKeyboard();
    ui.closeChat();

    ui.openNode(0x11aa2201);
    pump(100);
    shot("08_node");
    ui.showTraceRoute(0x11aa2201, "HOPT > OVL1 (11.0 dB) > RDGE (9.5 dB)\nback: RDGE > OVL1 > HOPT");
    lv_obj_scroll_to_y(lv_obj_get_child(ui.nodeOv, 1), 300, LV_ANIM_OFF);
    shot("08b_node_scrolled");
    ui.closeNode();

    // environment: detail page, a node that shares readings, the Settings card, the waiting state
    ui.openEnv();
    pump(50);
    shot("51_env_detail");
    lv_obj_scroll_to_y(lv_obj_get_child(ui.envOv, 1), 260, LV_ANIM_OFF);
    pump(20);
    shot("51b_env_detail_pressure");
    lv_obj_scroll_to_y(lv_obj_get_child(ui.envOv, 1), 2000, LV_ANIM_OFF);
    pump(20);
    shot("51c_env_detail_mesh");
    ui.openNode(0x11aa2206);
    pump(50);
    shot("52_node_env");
    ui.closeNode();
    ui.closeEnv();
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setEnvCorrNote, LV_ANIM_OFF);
    lv_obj_scroll_by(lv_obj_get_child(ui.setOv, 1), 0, -40, LV_ANIM_OFF);
    ui.stepEnvCorr(-1);
    ui.stepEnvCorr(-1);
    ui.stepEnvCorr(-1);
    pump(20);
    shot("53_settings_sensors");
    lv_obj_scroll_to_view_recursive(ui.setEnvEveryDd, LV_ANIM_OFF);
    lv_obj_scroll_by(lv_obj_get_child(ui.setOv, 1), 0, -60, LV_ANIM_OFF);
    pump(20);
    shot("53b_settings_sharing");
    ui.closeSettings();
    {
        HopticsUI::EnvReading saved = ui.envNow;
        ui.envNow = HopticsUI::EnvReading();
        ui.selectTab(HopticsUI::T_HOME);
        pump(20);
        shot("54_home_env_waiting");
        ui.envNow = saved;
        ui.envDirty = 0xff;
        ui.selectTab(HopticsUI::T_HOME);
    }

    ui.openSettings();
    pump(100);
    shot("09_settings");
    lv_obj_scroll_to_y(lv_obj_get_child(ui.setOv, 1), 330, LV_ANIM_OFF);
    shot("09b_settings_radio");
    ui.closeSettings();

    ui.selectTab(HopticsUI::T_HOME);
    ui.addMessage(0x11aa2202, 0x11aa2202, now, "Heading out to the lake, anyone on?", 0, 0, false);
    pump(100);
    shot("10_toast");
    pump(6000);

    std::string js;
    ui.buildSnapshotJson(js);
    FILE *jf = fopen((outDir + "/snapshot.json").c_str(), "w");
    fwrite(js.data(), 1, js.size(), jf);
    fclose(jf);
    printf("snapshot %zu bytes\n", js.size());

    const char *tn[4] = {"midnight", "daylight", "nightred", "amber"};
    for (int t = 1; t < 4; t++) {
        ui.setTheme(t);
        ui.selectTab(HopticsUI::T_HOME);
        pump(100);
        shot((std::string("20_home_") + tn[t]).c_str());
        ui.selectTab(HopticsUI::T_STATS);
        shot((std::string("21_stats_") + tn[t]).c_str());
        ui.selectTab(HopticsUI::T_MAP);
        pump(50);
        shot((std::string("22_map_") + tn[t]).c_str());
        ui.openChat(1);
        pump(50);
        shot((std::string("23_chat_") + tn[t]).c_str());
        ui.closeChat();
    }
    ui.setTheme(0);
    ui.selectTab(HopticsUI::T_HOME);
    ui.updatePosition(me, 386900000, -1211800000, 80); // drive into the next town -> "Now in" pop-up
    pump(50);
    shot("30_city_popup");
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    ui.openNode(0x11aa2201);
    pump(50);
    shot("31_node_city");
    ui.closeNode();
    // first-boot wizard (fresh node: no region)
    ui.wizardShown = false;
    ui.region = 0;
    ui.maybeShowWizard();
    pump(50);
    shot("32_wizard_region");
    ui.wizardStep(1);
    pump(50);
    shot("33_wizard_name");
    ui.wizardStep(2);
    pump(50);
    shot("34_wizard_theme");
    lv_obj_add_flag(ui.wizOv, LV_OBJ_FLAG_HIDDEN);
    ui.setTheme(1);
    ui.openSettings();
    pump(50);
    shot("24_settings_daylight");
    ui.setTheme(0);
    ui.hasBluetooth = true;
    ui.setBluetoothEnabled(true, 482913);
    ui.setNetwork(true, true, 0x05000a0a, true, -58);
    ui.setWifiConfig("HomeWiFi", true);
    ui.setLnaMode(3);
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setLnaDd, LV_ANIM_OFF);
    pump(20);
    shot("43_settings_lna");
    lv_obj_scroll_to_view_recursive(ui.setPsk, LV_ANIM_OFF); lv_obj_scroll_by(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(ui.setPsk))), 0, -90, LV_ANIM_OFF);
    pump(20);
    shot("44_settings_wifi");
    lv_obj_scroll_to_view_recursive(ui.setMapSw, LV_ANIM_OFF);
    pump(20);
    shot("45_settings_map");
    ui.setRingtone(3);
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setToneDd, LV_ANIM_OFF);
    pump(20);
    shot("46_settings_sound");
    lv_dropdown_open(ui.setToneDd);
    pump(20);
    shot("46b_sound_list");
    lv_dropdown_close(ui.setToneDd);
    ui.closeSettings();
    // problem reports: plain report from Settings, then a fresh crash and a restart loop
    ui.setDeviceInfo("2.8.3.e42ae67", 0, true, true);
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setInfo, LV_ANIM_OFF);
    lv_obj_scroll_by(lv_obj_get_child(ui.setOv, 1), 0, -40, LV_ANIM_OFF);
    pump(20);
    shot("50_settings_device");
    lv_obj_scroll_to_view_recursive(ui.setShutdownBtn, LV_ANIM_OFF);
    lv_obj_scroll_by(lv_obj_get_child(ui.setOv, 1), 0, -70, LV_ANIM_OFF);
    pump(20);
    shot("50c_settings_report_button");
    lv_obj_scroll_to_y(lv_obj_get_child(ui.setOv, 1), 100000, LV_ANIM_OFF);
    pump(20);
    shot("55_settings_support");
    ui.openReport(false);
    pump(20);
    shot("47_report_problem");
    ui.closeReport();
    ui.closeSettings();
    {
        hoptics::CrashRecord r;
        r.kind = hoptics::CK_CRASH;
        r.count = 2;
        r.streak = 1;
        r.build = 13;
        snprintf(r.ver, sizeof(r.ver), "0.2.1");
        snprintf(r.reason, sizeof(r.reason), "abort()");
        snprintf(r.task, sizeof(r.task), "loopTask");
        r.pc = 0x421e9caf;
        const uint32_t bt[8] = {0x421e9ce4, 0x421e9dbf, 0x421e9e52, 0x42013111, 0x42013146, 0x420134a3, 0x42076661, 0x420ad25e};
        for (int i = 0; i < 8; i++)
            r.bt[i] = bt[i];
        r.btDepth = 8;
        r.upSecs = 12;
        r.heapKB = 84;
        r.psramKB = 92;
        hoptics::Report::get().inject(r, true);
        ui.crashShown = false;
        ui.showCrashReportIfAny();
        pump(20);
        shot("48_report_crash");
        lv_obj_scroll_to_y(lv_obj_get_child(ui.repOv, 1), 400, LV_ANIM_OFF);
        pump(20);
        shot("48b_report_crash_details");
        ui.closeReport();
        r.streak = 4;
        r.count = 5;
        hoptics::Report::get().inject(r, true);
        ui.openReport(true);
        pump(20);
        shot("49_report_loop");
        ui.closeReport();
        ui.openSettings();
        pump(20);
        lv_obj_scroll_to_view_recursive(ui.setInfo, LV_ANIM_OFF);
        lv_obj_scroll_by(lv_obj_get_child(ui.setOv, 1), 0, -40, LV_ANIM_OFF);
        pump(20);
        shot("50b_settings_device_crash");
        ui.closeSettings();
        std::string d = hoptics::Report::get().details("2.8.3.e42ae67", ui.reportStatus());
        std::string u = hoptics::Report::get().issueUrl(true, d);
        FILE *uf = fopen((outDir + "/report_url.txt").c_str(), "w");
        fprintf(uf, "%s\n", u.c_str());
        fclose(uf);
        printf("report url %zu chars\n", u.size());
    }
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setBt, LV_ANIM_OFF);
    pump(20);
    shot("40_settings_phone_mode");
    ui.showPhoneMode(482913, false);
    pump(30);
    shot("41_phone_mode");
    ui.showPhoneMode(0, true);
    pump(30);
    shot("42_phone_mode_leaving");
    { lv_mem_monitor_t mon; lv_mem_monitor(&mon); printf("LVGL mem after theme switches: %u KB used (max %u KB, frag %u%%)\n", (unsigned)((mon.total_size-mon.free_size)/1024), (unsigned)(mon.max_used/1024), (unsigned)mon.frag_pct); }
    return 0;
}
