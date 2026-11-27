// Host harness: renders HopticsUI into an in-memory RGB565 display with fake mesh data and writes
// one PPM screenshot per screen.
//   ./harness                       240x320 portrait (Heltec kits) into shots/
//   ./harness 320x240 shots_320x240 landscape (T-Deck Plus, ThinkNode M9) into its own folder
#define private public
#include "graphics/view/TFT/HopticsUI.h"
#undef private
#include "graphics/view/TFT/HopticsReport.h"
#include "graphics/view/TFT/HopticsReach.h"
#include "lvgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
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

// Virtual keyboard and trackball, read by LVGL like real indevs: a keypad (T-Deck Plus / ThinkNode M9
// keyboard) and an encoder (T-Deck trackball, which device-ui's EncoderInputDriver reports as turns for
// up/down and LV_KEY_DOWN / LV_KEY_UP keys for left/right).
struct InEv {
    uint32_t key;
    int diff;
    bool pressed;
};
static std::deque<InEv> kbdQ, encQ;
static uint32_t kbdLast = 0, encLast = 0;
static void kbd_read(lv_indev_t *, lv_indev_data_t *d)
{
    if (!kbdQ.empty()) {
        kbdLast = kbdQ.front().key;
        d->state = kbdQ.front().pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        kbdQ.pop_front();
        d->continue_reading = !kbdQ.empty();
    } else
        d->state = LV_INDEV_STATE_RELEASED;
    d->key = kbdLast;
}
static void enc_read(lv_indev_t *, lv_indev_data_t *d)
{
    if (!encQ.empty()) {
        const InEv &e = encQ.front();
        encLast = e.key ? e.key : encLast;
        d->enc_diff = (int16_t)e.diff;
        d->state = e.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        encQ.pop_front();
        d->continue_reading = !encQ.empty();
    } else
        d->state = LV_INDEV_STATE_RELEASED;
    d->key = encLast;
}

static std::deque<InEv> touchQ;
static lv_point_t touchAt{};
static void touch_read(lv_indev_t *, lv_indev_data_t *d) // key = x, diff = y
{
    if (!touchQ.empty()) {
        touchAt = {(int32_t)touchQ.front().key, (int32_t)touchQ.front().diff};
        d->state = touchQ.front().pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        touchQ.pop_front();
    } else
        d->state = LV_INDEV_STATE_RELEASED;
    d->point = touchAt;
}

static void pump(int ms);
static void tap(lv_obj_t *o) // a finger on the middle of an object
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const uint32_t x = (uint32_t)(a.x1 + a.x2) / 2;
    const int y = (a.y1 + a.y2) / 2;
    touchQ.push_back({x, y, true});
    touchQ.push_back({x, y, true});
    touchQ.push_back({x, y, false});
    pump(150);
}
static void key(uint32_t k, int times = 1) // press and release on the keyboard
{
    for (int i = 0; i < times; i++) {
        kbdQ.push_back({k, 0, true});
        kbdQ.push_back({k, 0, false});
        pump(80);
    }
}
static void type(const char *text)
{
    for (const char *c = text; *c; c++)
        key((uint8_t)*c);
}
static void turn(int steps) // trackball up (-) / down (+)
{
    encQ.push_back({0, steps, false});
    pump(80);
}
static void ball(uint32_t k) // trackball press (LV_KEY_ENTER) or left / right (LV_KEY_DOWN / LV_KEY_UP)
{
    encQ.push_back({k, 0, true});
    encQ.push_back({k, 0, false});
    pump(80);
}

static void shot(const char *name);
static void kshot(const char *name) // after keys: let scroll-into-view animations finish first
{
    pump(400);
    shot(name);
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
    ui.upsertNode(me, 0, now, "BLF1", "Bluffs V4 Kit", 0, true, false, 110);
    ui.updatePosition(me, 398000000, -896500000, 70);
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
        {0x11aa2201, "RDGE", "Ridge Repeater", 2, 0, -78, 9.5f, 101, 398750000, -894650000, 40},
        {0x11aa2202, "FLSM", "Lake Node", 0, 0, -96, 4.25f, 72, 397850000, -895550000, 95},
        {0x11aa2203, "AUBN", "Ridge East", 11, 1, 0, 0, 101, 400050000, -894550000, 300},
        {0x11aa2204, "OVL1", "Home Base", 0, 0, -61, 11.0f, 100, 397840000, -896150000, 12},
        {0x11aa2205, "TRK7", "Trail Tracker 7", 5, 2, 0, 0, 34, 0, 0, 1800},
        {0x11aa2206, "WX01", "Weather Station", 6, 1, 0, 0, 58, 397550000, -897350000, 600},
        {0x11aa2207, "\xF0\x9F\x93\xA1", "Rooftop \xF0\x9F\x93\xA1 Gateway", 2, 0, -104, -6.5f, 101, 397450000, -896850000, 150},
        {0x11aa2208, "CAMP", "Camp Kit", 0, 3, 0, 0, 12, 0, 0, 5400},
        {0x11aa2209, "RSVL", "Hilltop North", 0, 2, 0, 0, 80, 398550000, -896750000, 2400},
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
    // reach check: MQTT gateways uploaded our channel message (id 11); the DM (id 12) got no copies back
    hoptics::Reach::get().track(11, 1, now - 1300, "Hoptics checking in from home base \xF0\x9F\x91\x8B");
    hoptics::Reach::get().track(12, 0x11aa2204, now - 120, "Yes! I'll bring the V4 kit");
    {
        struct { const char *gw; uint8_t start, limit; } echoes[] = {
            {"!11aa2201", 3, 3}, {"!11aa2205", 3, 3}, {"!11aa2202", 3, 2}, {"!7c3a91f2", 3, 2}, {"!0be1d004", 3, 2},
            {"!11aa2203", 3, 1}, {"!5a7720c1", 3, 1}, {"!9e02aa11", 3, 1}, {"!44c1b2e0", 3, 1}, {"!11aa2208", 3, 0},
            {"!c0ffee01", 3, 0}, {"!3b8d0a77", 0, 0}, {"!11aa2201", 3, 2}, // the last one: same gateway again, counted once
        };
        for (auto &e : echoes)
            hoptics_mqtt_echo(11, e.gw, e.start, e.limit, false);
        hoptics_mqtt_echo(11, "!11aa2200", 3, 3, true); // this radio uploaded it too
    }
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
    ui.showTraceRoute(0x11aa2201, "BLF1 > OVL1 (11.0 dB) > RDGE (9.5 dB)\nback: RDGE > OVL1 > BLF1");
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
    // every theme, the same five screens: the website's theme comparison uses these
    for (int t = 0; t < 4; t++) {
        ui.setTheme(t);
        ui.selectTab(HopticsUI::T_HOME);
        pump(100);
        shot((std::string("20_home_") + tn[t]).c_str());
        ui.selectTab(HopticsUI::T_LIVE);
        pump(50);
        shot((std::string("25_live_") + tn[t]).c_str());
        ui.selectTab(HopticsUI::T_STATS);
        lv_obj_scroll_to_y(ui.pages[HopticsUI::T_STATS], 0, LV_ANIM_OFF);
        pump(20);
        shot((std::string("21_stats_") + tn[t]).c_str());
        ui.selectTab(HopticsUI::T_MAP);
        pump(50);
        shot((std::string("22_map_") + tn[t]).c_str());
        ui.openChat(1);
        lv_textarea_set_text(ui.chatInput, "");
        pump(50);
        shot((std::string("23_chat_") + tn[t]).c_str());
        ui.closeChat();
    }
    ui.setTheme(0);
    ui.selectTab(HopticsUI::T_HOME);
    ui.updatePosition(me, 397950000, -895650000, 80); // drive into the next town -> "Now in" pop-up
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
    ui.setGpsMode(2); // "not present" as left by a stock build: the switch shows on boards with GPS
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setLnaDd, LV_ANIM_OFF);
    pump(20);
    shot("43_settings_lna");
    if (!lv_obj_has_flag(ui.setGpsRow, LV_OBJ_FLAG_HIDDEN)) { // boards with a GPS receiver
        lv_obj_scroll_to_view_recursive(ui.setGpsSw, LV_ANIM_OFF);
        pump(20);
        shot("43b_settings_gps");
    }
    { // Radio settings (Meshtastic 2.8 region rules): presets per region, a region change keeps a legal preset,
      // ham bands only in licensed mode, licensed mode asks twice
        int sentRegion = -1, sentPreset = -1, sentLic = -1, presetSet = -1;
        ui.cbs.setRegion = [&](int r, int p) { sentRegion = r; sentPreset = p; };
        ui.cbs.setPreset = [&](int p) { presetSet = p; };
        ui.cbs.setLicensed = [&](bool on) { sentLic = on; };
        int rfails = 0;
        auto rexpect = [&](bool ok, const char *what) {
            printf("%s radio: %s\n", ok ? "ok  " : "FAIL", what);
            rfails += !ok;
        };
        auto toastHas = [&](const char *t) { return !lv_obj_has_flag(ui.toast, LV_OBJ_FLAG_HIDDEN) && strstr(lv_label_get_text(ui.toastLabel), t); };
        auto pickRegion = [&](int code) {
            for (uint32_t i = 0; i < lv_dropdown_get_option_count(ui.setRegionDd); i++) {
                lv_dropdown_set_selected(ui.setRegionDd, i);
                char b[64];
                lv_dropdown_get_selected_str(ui.setRegionDd, b, sizeof(b));
                const char *want = code == 29 ? "EU_866" : code == 3 ? "EU_868" : code == 35 ? "ITU2_70CM" : code == 1 ? "US -" : "Unset";
                if (strstr(b, want) == b || (code == 0 && !strcmp(b, "Unset")))
                    break;
            }
            lv_obj_send_event(ui.setRegionDd, LV_EVENT_VALUE_CHANGED, nullptr);
            pump(20);
        };
        auto opts = [&]() { return std::string(lv_dropdown_get_options(ui.setPresetDd)); };
        ui.setLicensed(false);
        ui.setRadio(0, "UNSET", 0, "LongFast", true, 3, 0, 0);
        ui.openSettings();
        pump(20);
        rexpect(opts() == "Pick a region first" && lv_obj_has_state(ui.setPresetDd, LV_STATE_DISABLED), "no region: preset waits for one");
        lv_obj_scroll_to_view_recursive(ui.setPresetDd, LV_ANIM_OFF);
        pump(20);
        shot("09c_settings_radio_unset");
        pickRegion(1); // US picked for the first time on the device: Long Turbo, as Meshtastic 2.8 does
        rexpect(sentRegion == 1 && sentPreset == 9, "first US pick: Long Turbo");
        ui.setRadio(1, "US", 16, "MediumTurbo", true, 3, 27, 920.25f);
        pump(20);
        rexpect(!lv_obj_has_state(ui.setPresetDd, LV_STATE_DISABLED) && opts().find("Medium Turbo") != std::string::npos &&
                    opts().find("Lite") == std::string::npos && opts().find("Tiny") == std::string::npos,
                "US: the 10 presets the firmware allows");
        size_t n = 1;
        for (char c : opts())
            n += c == '\n';
        rexpect(n == 10, "US: exactly 10 presets");
        pickRegion(29); // EU_866 does not allow Medium Turbo: the region change carries its default, Lite Fast
        rexpect(sentRegion == 29 && sentPreset == 10 && toastHas("Region set"), "region change picks a legal preset");
        ui.setRadio(1, "US", 4, "MediumFast", true, 3, 27, 913.125f);
        sentRegion = sentPreset = -1;
        pickRegion(3); // EU_868 allows Medium Fast: kept
        rexpect(sentRegion == 3 && sentPreset == 4, "region change keeps an allowed preset");
        ui.setRadio(1, "US", 4, "MediumFast", true, 3, 27, 913.125f);
        sentRegion = -1;
        pickRegion(35); // a ham band, not licensed: refused, dropdown back on US
        char b[64];
        lv_dropdown_get_selected_str(ui.setRegionDd, b, sizeof(b));
        rexpect(sentRegion == -1 && toastHas("Licensed ham mode") && strstr(b, "US") == b, "ham band needs licensed mode");
        rexpect(!lv_obj_has_state(ui.setPresetDd, LV_STATE_DISABLED), "preset dropdown enabled once a region is set");
        pump(300);
        shot("09d_settings_radio_ham_refused");
        lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(ui.setHamSw, LV_STATE_CHECKED); // first flip: asks
        lv_obj_send_event(ui.setHamSw, LV_EVENT_VALUE_CHANGED, nullptr);
        pump(20);
        rexpect(sentLic == -1 && !lv_obj_has_state(ui.setHamSw, LV_STATE_CHECKED) && toastHas("Flip again"), "licensed: first flip asks");
        lv_obj_scroll_to_view_recursive(ui.setHamSw, LV_ANIM_OFF);
        pump(20);
        shot("09e_settings_licensed_ask");
        lv_obj_add_state(ui.setHamSw, LV_STATE_CHECKED); // second flip within 5 s: on
        lv_obj_send_event(ui.setHamSw, LV_EVENT_VALUE_CHANGED, nullptr);
        pump(20);
        rexpect(sentLic == 1 && ui.licensed, "licensed: second flip turns it on");
        pickRegion(35);
        rexpect(sentRegion == 35 && sentPreset == 13, "licensed: ham band allowed, Narrow Slow");
        ui.setRadio(35, "ITU2_70CM", 13, "NarrowSlow", true, 3, 27, 420.03f);
        pump(20);
        rexpect(opts() == "Narrow Fast\nNarrow Slow", "ham 70 cm: the narrow presets");
        lv_obj_remove_state(ui.setHamSw, LV_STATE_CHECKED); // off: no question
        lv_obj_send_event(ui.setHamSw, LV_EVENT_VALUE_CHANGED, nullptr);
        pump(20);
        rexpect(sentLic == 0 && !ui.licensed, "licensed: off at once");
        int sentTx = -1, sentSig = -1;
        ui.cbs.setTransmit = [&](bool on) { sentTx = on; };
        ui.cbs.setSigPolicy = [&](int p) { sentSig = p; };
        ui.setTransmit(true);
        ui.setSigPolicy(0);
        ui.openSettings();
        pump(20);
        rexpect(lv_obj_has_state(ui.setTxSw, LV_STATE_CHECKED) && !lv_obj_has_flag(ui.setSigRow, LV_OBJ_FLAG_HIDDEN),
                "transmit on, signed-packets row shown");
        lv_obj_remove_state(ui.setTxSw, LV_STATE_CHECKED);
        lv_obj_send_event(ui.setTxSw, LV_EVENT_VALUE_CHANGED, nullptr);
        pump(20);
        rexpect(sentTx == 0 && !ui.txEnabled && toastHas("Receive only"), "receive-only mode");
        lv_dropdown_set_selected(ui.setSigDd, 2);
        lv_obj_send_event(ui.setSigDd, LV_EVENT_VALUE_CHANGED, nullptr);
        pump(20);
        rexpect(sentSig == 2 && toastHas("Strict"), "signed packets: strict");
        lv_obj_scroll_to_view_recursive(ui.setSigDd, LV_ANIM_OFF);
        pump(300);
        shot("09f_settings_transmit_signing");
        ui.closeSettings();
        ui.selectTab(HopticsUI::T_HOME);
        pump(50);
        rexpect(strstr(lv_label_get_text(ui.homeRadio), "Transmit off") != nullptr, "Home says Transmit off");
        ui.setTransmit(true);
        ui.setSigPolicy(0);
        ui.cbs.setTransmit = nullptr;
        ui.cbs.setSigPolicy = nullptr;
        ui.cbs.setRegion = nullptr;
        ui.cbs.setPreset = nullptr;
        ui.cbs.setLicensed = nullptr;
        lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
        ui.setRadio(1, "US", 0, "LongFast", true, 3, 27, 906.875f);
        printf("radio settings: %d failed checks\n", rfails);
        if (rfails)
            return 1;
    }
    lv_obj_scroll_to_view_recursive(ui.setPsk, LV_ANIM_OFF); lv_obj_scroll_by(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(ui.setPsk))), 0, -90, LV_ANIM_OFF);
    pump(20);
    shot("44_settings_wifi");
    // MQTT + reach check: connected to a regional server, uploads allowed, but not listening on the channel yet
    ui.setMqttConfig(true, "mqtt.bayme.sh", "meshdev", true, "msh/US/bayarea");
    ui.setOkToMqtt(true);
    ui.setDownlink(false);
    ui.openSettings();
    pump(20);
    lv_obj_scroll_to_view_recursive(ui.setMqttCheck[0], LV_ANIM_OFF);
    lv_obj_scroll_by(lv_obj_get_parent(lv_obj_get_parent(ui.setMqttCheck[0])), 0, -86, LV_ANIM_OFF);
    pump(20);
    shot("56_settings_mqtt");
    lv_obj_scroll_to_view_recursive(ui.setMqttRoot, LV_ANIM_OFF);
    pump(20);
    shot("56b_settings_mqtt_fields");
    lv_obj_scroll_to_view_recursive(ui.setDownSw, LV_ANIM_OFF);
    lv_obj_scroll_by(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(ui.setDownSw))), 0, -230, LV_ANIM_OFF);
    pump(20);
    shot("56c_settings_mqtt_setup");
    { // All Meshtastic settings: a generated schema, values in, only the changed ones out
        std::vector<std::string> secs = {"Device", "LoRa"};
        std::vector<HopticsUI::AllSetting> fl = {
            {0, "Role", HopticsUI::AK_ENUM, "Client\nClient mute\nRouter", 0},
            {0, "Time zone (POSIX)", HopticsUI::AK_STR, "", 64},
            {1, "Hop limit", HopticsUI::AK_NUM, "", 0},
            {1, "TX enabled", HopticsUI::AK_BOOL, "", 0},
            {1, "Frequency offset", HopticsUI::AK_FLOAT, "", 0},
            {1, "Wi-Fi password", HopticsUI::AK_SECRET, "", 64},
            {1, "IPv4 config IP", HopticsUI::AK_IP, "", 0},
        };
        std::vector<std::string> vals = {"2", "PST8PDT", "3", "1", "0", "", "192.168.1.20"};
        int reqs = 0, savedSec = -1;
        std::vector<std::pair<int, std::string>> got;
        ui.setAllSchema(secs, fl);
        ui.cbs.allGet = [&](int i) { return vals[i]; };
        ui.cbs.allLoaded = [&](int) { return true; };
        ui.cbs.allRequest = [&](int) { reqs++; };
        ui.cbs.allSave = [&](int s, const std::vector<std::pair<int, std::string>> &c) {
            savedSec = s;
            got = c;
            return 0;
        };
        ui.openSettings();
        pump(20);
        ui.openAllSettings();
        pump(50);
        bool ok = ui.allCur == 0 && ui.allRowW.size() == 2 && reqs == 1;
        shot("57_all_settings_device");
        lv_dropdown_set_selected(ui.allSecDd, 1);
        lv_obj_send_event(ui.allSecDd, LV_EVENT_VALUE_CHANGED, nullptr);
        pump(50);
        ok = ok && ui.allCur == 1 && ui.allRowW.size() == 5 && reqs == 2 &&
             !strcmp(lv_textarea_get_text(ui.allRowW[4]), "192.168.1.20");
        shot("57b_all_settings_lora");
        lv_textarea_set_text(ui.allRowW[0], "5");   // hop limit
        lv_obj_remove_state(ui.allRowW[1], LV_STATE_CHECKED); // TX off
        lv_obj_send_event(ui.allSaveBtn, LV_EVENT_CLICKED, nullptr);
        pump(20);
        ok = ok && savedSec == 1 && got.size() == 2 && got[0].first == 2 && got[0].second == "5" && got[1].first == 3 &&
             got[1].second == "0";
        vals[2] = "5";
        vals[3] = "0";
        ui.allSectionArrived(1); // the radio's answer after the save: nothing new, no redraw
        ok = ok && ui.allRowW.size() == 5;
        printf("%s all settings: sections, values in, only the changes out\n", ok ? "ok  " : "FAIL");
        ui.closeAllSettings();
        ui.closeSettings();
        if (!ok)
            return 1;
    }
    { // MQTT: BayMesh-style gateway (uplink on, downlink off) and the other module settings reach the callback
        int up = -1, down = -1, fl = -1;
        ui.cbs.setMqtt = [&](bool, const char *, const char *, const char *, const char *, bool, bool d, bool u, uint8_t f) {
            down = d;
            up = u;
            fl = f;
        };
        ui.setMqttConfig(true, "mqtt.bayme.sh", "meshdev", true, "msh/US/bayarea", HopticsUI::MQTT_ENC);
        ui.setDownlink(false);
        ui.setUplink(false);
        ui.openSettings();
        pump(20);
        bool ok = lv_obj_has_state(ui.setMqttFlag[0], LV_STATE_CHECKED) && !lv_obj_has_state(ui.setMqttFlag[3], LV_STATE_CHECKED);
        lv_obj_add_state(ui.setUpSw, LV_STATE_CHECKED);
        lv_obj_add_state(ui.setMqttFlag[3], LV_STATE_CHECKED); // map report
        ui.cbs.setMqtt(true, "mqtt.bayme.sh", "meshdev", nullptr, "msh/US/bayarea", true,
                       lv_obj_has_state(ui.setDownSw, LV_STATE_CHECKED), lv_obj_has_state(ui.setUpSw, LV_STATE_CHECKED),
                       ui.mqttFlagsFromSwitches());
        ok = ok && up == 1 && down == 0 && fl == (HopticsUI::MQTT_ENC | HopticsUI::MQTT_MAP);
        printf("%s mqtt: gateway uplink, no downlink, encryption + map report\n", ok ? "ok  " : "FAIL");
        lv_obj_scroll_to_view_recursive(ui.setMqttFlag[4], LV_ANIM_OFF);
        pump(20);
        shot("56d_settings_mqtt_more");
        ui.cbs.setMqtt = nullptr;
        ui.setUplink(false);
        if (!ok)
            return 1;
    }
    ui.setDownlink(true);
    ui.closeSettings();
    ui.openChat(1);
    pump(50);
    shot("57_chat_reach");
    ui.openReach(11);
    pump(50);
    shot("58_reach");
    lv_obj_scroll_by(lv_obj_get_child(ui.reachOv, 1), 0, -200, LV_ANIM_OFF);
    pump(20);
    shot("58b_reach_gateways");
    ui.closeReach();
    ui.closeChat();
    ui.openChat(0x11aa2204);
    ui.openReach(12);
    pump(50);
    shot("58c_reach_none");
    ui.closeReach();
    ui.closeChat();
    {
        std::string js;
        ui.buildSnapshotJson(js);
        FILE *jf = fopen("shots/snapshot_reach.json", "w");
        fwrite(js.data(), 1, js.size(), jf);
        fclose(jf);
    }
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

    // ------------------------------------------------------------ keyboard and trackball navigation
    // Landscape panels are the keyboard devices, so their run adds these (HARNESS_KEYS=1/0 overrides).
    const char *kv = getenv("HARNESS_KEYS");
    if (kv ? atoi(kv) != 0 : WD > HT) {
        lv_obj_t *phone = ui.phoneScreen;
        ui.phoneScreen = nullptr;
        lv_screen_load(ui.mainScreen);
        lv_obj_delete(phone);
        ui.closeSettings(); // still open under the phone-mode shots
        lv_indev_t *kbd = lv_indev_create();
        lv_indev_set_type(kbd, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(kbd, kbd_read);
        lv_indev_t *enc = lv_indev_create();
        lv_indev_set_type(enc, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(enc, enc_read);
        ui.inputDevicesChanged(); // like a board whose keyboard came up after the screen
        int fails = 0;
        auto focused = [&]() { return lv_group_get_focused(lv_group_get_default()); };
        auto expect = [&](bool ok, const char *what) {
            if (!ok) {
                lv_obj_t *f = focused();
                printf("KEYS FAIL: %s (focus %p, parent %p, index %d, tab %d)\n", what, (void *)f,
                       f ? (void *)lv_obj_get_parent(f) : nullptr, f ? (int)lv_obj_get_index(f) : -1, (int)ui.activeTab);
                fails++;
            }
        };
        expect(lv_group_get_default() && lv_indev_get_group(kbd) == lv_group_get_default(), "keyboard on the default group");

        ui.selectTab(HopticsUI::T_HOME);
        pump(100);
        expect(focused() == ui.homeEnv, "Home starts on the environment card");
        kshot("60_keys_home");
        key(LV_KEY_DOWN);
        expect(focused() == ui.homeLoc, "DOWN: your location (opens the Location card)");
        kshot("60a_keys_home_location");
        key(LV_KEY_DOWN);
        expect(focused() == ui.homeLast[0], "DOWN: first last-heard row");
        kshot("60b_keys_home_last_heard");

        key(LV_KEY_RIGHT); // next tab
        expect(ui.activeTab == HopticsUI::T_LIVE, "RIGHT: Live tab");
        key(LV_KEY_DOWN, 4); // past the three filter chips to the second row
        expect(focused() == ui.liveRows[1].row, "DOWN x4: second live row");
        kshot("61_keys_live_row");
        uint32_t rowNode = (uint32_t)(uintptr_t)lv_obj_get_user_data(ui.liveRows[1].row);
        key(LV_KEY_ENTER);
        expect(!lv_obj_has_flag(ui.nodeOv, LV_OBJ_FLAG_HIDDEN) && ui.nodeOvNum == rowNode, "ENTER: that node's page");
        kshot("62_keys_node");
        key(LV_KEY_DOWN);
        kshot("62b_keys_node_actions");
        key(LV_KEY_ESC);
        expect(lv_obj_has_flag(ui.nodeOv, LV_OBJ_FLAG_HIDDEN) && focused() == ui.liveRows[1].row, "ESC: back on the same row");
        kshot("63_keys_back_to_list");

        key('c'); // shortcut
        expect(ui.activeTab == HopticsUI::T_CHAT, "'c': Chat tab");
        key(LV_KEY_DOWN);
        key(LV_KEY_ENTER);
        expect(!lv_obj_has_flag(ui.chatOv, LV_OBJ_FLAG_HIDDEN) && focused() == ui.chatInput, "ENTER on a chat: typing in the message");
        type("On my way, ETA 20 min");
        kshot("64_keys_chat_typing");
        key(LV_KEY_ENTER); // send
        expect(lv_textarea_get_text(ui.chatInput)[0] == 0, "ENTER: sent");
        key(LV_KEY_UP); // to the conversation, then scroll it
        expect(focused() == ui.chatMsgs, "UP: the conversation");
        key(LV_KEY_UP, 2);
        kshot("64b_keys_chat_scrolled");
        key(LV_KEY_ESC);
        expect(lv_obj_has_flag(ui.chatOv, LV_OBJ_FLAG_HIDDEN) && ui.activeTab == HopticsUI::T_CHAT, "ESC: back to the chat list");
        kshot("64c_keys_chat_list");

        for (int i = 0; i < 30 && focused() != ui.statusGear; i++)
            key(LV_KEY_NEXT);
        expect(focused() == ui.statusGear, "NEXT: reaches the settings gear");
        kshot("65_keys_gear");
        key(LV_KEY_ENTER);
        expect(focused() == ui.setThemeDd, "Settings start on the theme");
        kshot("65b_keys_settings");
        key(LV_KEY_ENTER);
        expect(lv_dropdown_is_open(ui.setThemeDd), "ENTER: list open");
        key(LV_KEY_DOWN);
        kshot("65c_keys_settings_list");
        key(LV_KEY_ESC);
        expect(!lv_dropdown_is_open(ui.setThemeDd) && !lv_obj_has_flag(ui.setOv, LV_OBJ_FLAG_HIDDEN), "ESC: list closed, still in Settings");
        key(LV_KEY_DOWN);
        expect(focused() == ui.setBright, "DOWN: brightness");
        int32_t b0 = lv_slider_get_value(ui.setBright);
        key(LV_KEY_LEFT, 3);
        expect(lv_slider_get_value(ui.setBright) < b0 && ui.brightness == lv_slider_get_value(ui.setBright), "LEFT: dimmer, applied");
        kshot("66_keys_settings_slider");
        for (int i = 0; i < 40 && focused() != ui.setShort; i++)
            key(LV_KEY_DOWN);
        expect(focused() == ui.setShort, "DOWN: short name field");
        key(LV_KEY_BACKSPACE);
        type("Hx");
        key(LV_KEY_ENTER);
        expect(focused() == ui.setLong, "ENTER in a field: next field");
        kshot("66b_keys_settings_names");
        key(LV_KEY_ESC);
        expect(lv_obj_has_flag(ui.setOv, LV_OBJ_FLAG_HIDDEN) && focused() == ui.statusGear, "ESC: Settings closed, back on the gear");

        key('m');
        expect(ui.activeTab == HopticsUI::T_MAP, "'m': Map tab");
        key(LV_KEY_DOWN, 3);
        expect(focused() == ui.mapArea, "DOWN x3: the map");
        key(LV_KEY_ENTER);
        expect(ui.mapPan, "ENTER: panning");
        key(LV_KEY_RIGHT);
        key(LV_KEY_UP);
        kshot("67_keys_map_pan");
        key(LV_KEY_ENTER);
        expect(!ui.mapPan && focused() == ui.mapArea, "ENTER: done panning");

        key('s');
        key(LV_KEY_DOWN, 2);
        kshot("68_keys_stats_scroll");

        // trackball: turns move, left / right switch tabs, press opens
        key('n');
        turn(4);
        expect(focused() == ui.nodeRows[0].row, "trackball down x4: first node row");
        kshot("69_keys_trackball_nodes");
        ball(LV_KEY_DOWN); // trackball left
        expect(ui.activeTab == HopticsUI::T_CHAT, "trackball left: previous tab");
        ball(LV_KEY_UP); // trackball right
        expect(ui.activeTab == HopticsUI::T_NODES, "trackball right: next tab");
        turn(4);
        ball(LV_KEY_ENTER);
        expect(!lv_obj_has_flag(ui.nodeOv, LV_OBJ_FLAG_HIDDEN), "trackball press: node page");
        ball(LV_KEY_DOWN);
        expect(lv_obj_has_flag(ui.nodeOv, LV_OBJ_FLAG_HIDDEN), "trackball left in a page: back");
        ui.openChat(1);
        pump(50);
        turn(-1); // from the message field up to the conversation
        expect(focused() == ui.chatMsgs, "trackball up: the conversation");
        ball(LV_KEY_ENTER); // edit mode: turning scrolls
        turn(-2);
        kshot("69b_keys_trackball_scroll");
        ball(LV_KEY_ENTER);
        ball(LV_KEY_DOWN);
        expect(lv_obj_has_flag(ui.chatOv, LV_OBJ_FLAG_HIDDEN), "trackball left: chat closed");

        // focus in every theme
        const char *tn[4] = {"midnight", "daylight", "nightred", "amber"};
        for (int t = 0; t < 4; t++) {
            ui.setTheme(t);
            key('n');
            lv_group_focus_obj(ui.sortBtns[3]); // the tab remembers its last row: start from the sort buttons
            key(LV_KEY_DOWN, 2);
            expect(focused() == ui.nodeRows[1].row, "theme: second node row");
            kshot((std::string("70_keys_nodes_") + tn[t]).c_str());
            ui.openSettings();
            pump(50);
            key(LV_KEY_DOWN, 3);
            kshot((std::string("70b_keys_settings_") + tn[t]).c_str());
            key(LV_KEY_ESC);
        }
        ui.setTheme(0);

        // first-boot wizard by keys
        ui.wizardShown = false;
        ui.region = 0;
        ui.maybeShowWizard();
        pump(50);
        expect(focused() == ui.wizRegion, "wizard starts on the region");
        kshot("71_keys_wizard_region");
        key(LV_KEY_RIGHT);
        expect(ui.wizStep == 1 && focused() == ui.wizLong, "RIGHT: names, on the long name");
        type(" 2");
        key(LV_KEY_ENTER);
        expect(focused() == ui.wizShort, "ENTER: on to the short name");
        kshot("71b_keys_wizard_name");
        key(LV_KEY_ENTER);
        expect(focused() == ui.wizNext, "ENTER: on to Next (before Skip)");
        key(LV_KEY_ENTER);
        expect(ui.wizStep == 2, "ENTER on Next: the look");
        key(LV_KEY_DOWN);
        kshot("71c_keys_wizard_theme");
        key(LV_KEY_ESC, 2);
        expect(ui.wizStep == 0 && !lv_obj_has_flag(ui.wizOv, LV_OBJ_FLAG_HIDDEN), "ESC: back two steps, still in the wizard");
        lv_obj_add_flag(ui.wizOv, LV_OBJ_FLAG_HIDDEN);
        ui.keyScope();

        ui.openReport(false);
        pump(20);
        kshot("72_keys_report");
        key(LV_KEY_ESC);
        expect(lv_obj_has_flag(ui.repOv, LV_OBJ_FLAG_HIDDEN), "ESC: report closed");

        ui.keyCommand(HopticsUI::KEY_COMPOSE); // ThinkNode M9 "preset" key
        expect(!lv_obj_has_flag(ui.chatOv, LV_OBJ_FLAG_HIDDEN) && focused() == ui.chatInput, "compose key: primary channel, typing");
        ui.keyCommand(HopticsUI::KEY_HOME);
        expect(lv_obj_has_flag(ui.chatOv, LV_OBJ_FLAG_HIDDEN) && ui.activeTab == HopticsUI::T_HOME, "home key");

        // ThinkNode M9 location key: short press = the Location card, long press = GPS on / off
        int sent = 0, gpsSet = -1;
        ui.cbs.sendPosition = [&](int32_t, int32_t, int32_t) { return ++sent > 0; };
        ui.cbs.setGps = [&](bool on) { gpsSet = on; };
        auto hidden = [](lv_obj_t *o) { return lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); };
        auto toastIs = [&](const char *t) { return !hidden(ui.toast) && strstr(lv_label_get_text(ui.toastLabel), t); };
        ui.setGpsMode(1);
        ui.updatePosition(me, 397950000, -895650000, 80, 9); // a fresh fix from 9 satellites
        ui.keyCommand(HopticsUI::KEY_LOCATION);
        expect(!hidden(ui.locOv) && focused() == ui.locMapBtn, "location key: the card, on Show on map");
        kshot("75_keys_location");
        key(LV_KEY_DOWN);
        key(LV_KEY_ENTER);
        expect(sent == 1 && toastIs("Position sent"), "Send my position");
        kshot("75b_keys_location_sent");
        key(LV_KEY_DOWN);
        key(LV_KEY_ENTER);
        expect(focused() == ui.locGpsBtn && gpsSet < 0, "GPS button: asks for a second press");
        kshot("75c_keys_location_gps_confirm");
        key(LV_KEY_ENTER);
        expect(gpsSet == 0 && ui.gpsMode == 0 && toastIs("Turning GPS off"), "second press: GPS off, the radio restarts");
        kshot("75d_keys_location_gps_off");
        ui.keyCommand(HopticsUI::KEY_LOCATION);
        expect(hidden(ui.locOv) && ui.activeTab == HopticsUI::T_HOME, "location key again: card closed");
        ui.keyCommand(HopticsUI::KEY_GPS);
        expect(gpsSet == 1 && ui.gpsMode == 1 && toastIs("Turning GPS on"), "long press: GPS on");
        kshot("75e_keys_gps_long_press");
        lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
        ui.keyCommand(HopticsUI::KEY_COMPOSE); // over a chat: ESC goes back to it
        ui.keyCommand(HopticsUI::KEY_LOCATION);
        expect(!hidden(ui.locOv) && !hidden(ui.chatOv), "location key over a chat");
        key(LV_KEY_ESC);
        expect(hidden(ui.locOv) && !hidden(ui.chatOv) && focused() == ui.chatInput, "ESC: back in the chat, typing");
        ui.keyCommand(HopticsUI::KEY_LOCATION);
        ui.mapUserPan = true;
        key(LV_KEY_ENTER);
        expect(hidden(ui.locOv) && hidden(ui.chatOv) && ui.activeTab == HopticsUI::T_MAP && !ui.mapUserPan,
               "Show on map: the map, following us");
        kshot("75f_keys_location_map");
        ui.keyCommand(HopticsUI::KEY_HOME);
        {
            HopticsUI::Node *mn = ui.nodePtr(me); // no position yet: only the GPS button can do anything
            mn->hasPos = false;
            const uint32_t fixAt = ui.myFixTime;
            ui.myFixTime = 0;
            ui.keyCommand(HopticsUI::KEY_LOCATION);
            expect(focused() == ui.locGpsBtn && lv_obj_has_state(ui.locMapBtn, LV_STATE_DISABLED), "no position: on the GPS button");
            kshot("75g_keys_location_no_fix");
            key(LV_KEY_ESC);
            ui.dirtyHome = true;
            ui.tick(ui.nowTs);
            lv_obj_scroll_to_view_recursive(ui.homeLoc, LV_ANIM_OFF);
            kshot("75h_keys_home_no_fix");
            mn->hasPos = true; // only the stored position from the node list: shown, not sent
            ui.keyCommand(HopticsUI::KEY_LOCATION);
            expect(focused() == ui.locMapBtn && lv_obj_has_state(ui.locSendBtn, LV_STATE_DISABLED), "stored position: map, no send");
            kshot("75j_keys_location_last_known");
            key(LV_KEY_ESC);
            ui.myFixTime = fixAt;
            ui.gpsMode = -1; // a radio without a receiver
            ui.keyCommand(HopticsUI::KEY_GPS);
            expect(gpsSet == 1 && toastIs("No GPS on this radio"), "long press without GPS");
            kshot("75i_keys_gps_none");
            lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
            ui.gpsMode = 1;
        }
        for (int t = 0; t < 4; t++) {
            ui.setTheme(t);
            ui.keyCommand(HopticsUI::KEY_LOCATION);
            key(LV_KEY_DOWN);
            kshot((std::string("76_keys_location_") + tn[t]).c_str());
            key(LV_KEY_ESC);
        }
        ui.setTheme(0);
        ui.keyCommand(HopticsUI::KEY_HOME);

        // keyboard plus touch screen (T-Deck Plus): a tap hides the ring, the next key brings it back
        lv_indev_t *touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, touch_read);
        ui.inputDevicesChanged();
        key('n');
        key(LV_KEY_DOWN);
        expect(focused() && lv_obj_has_state(focused(), LV_STATE_FOCUS_KEY), "keys: ring on");
        tap(ui.nodeRows[2].row);
        expect(!lv_obj_has_flag(ui.nodeOv, LV_OBJ_FLAG_HIDDEN), "tap: node page");
        expect(!focused() || !lv_obj_has_state(focused(), LV_STATE_FOCUS_KEY), "tap: no ring");
        kshot("74_keys_touch_no_ring");
        key(LV_KEY_DOWN);
        expect(focused() && lv_obj_has_state(focused(), LV_STATE_FOCUS_KEY), "key after a tap: ring back");
        key(LV_KEY_ESC);
        expect(lv_obj_has_flag(ui.nodeOv, LV_OBJ_FLAG_HIDDEN) && focused() == ui.nodeRows[2].row &&
                   lv_obj_has_state(focused(), LV_STATE_FOCUS_KEY),
               "ESC: back on the tapped row, with the ring");
        ui.selectTab(HopticsUI::T_HOME);
        lv_obj_scroll_to_view_recursive(ui.homeLoc, LV_ANIM_OFF);
        pump(50);
        tap(ui.homeLoc);
        expect(!hidden(ui.locOv), "tap on Home's location card: the Location card");
        tap(ui.locGpsBtn);
        kshot("77_touch_location_gps_confirm");
        tap(lv_obj_get_child(lv_obj_get_child(ui.locOv, 0), 0));
        expect(hidden(ui.locOv), "tap on back: card closed");
        key(LV_KEY_DOWN); // keys last again (the ring is back), as the phone-mode shot expects

        ui.showPhoneMode(482913, false);
        pump(30);
        kshot("73_keys_phone_mode");
        printf("key navigation: %d failed checks\n", fails);
        if (fails)
            return 1;
    } else {
        // the Location card from Home's location card; this board has no GPS receiver
        if (lv_obj_t *phone = ui.phoneScreen) {
            ui.phoneScreen = nullptr;
            lv_screen_load(ui.mainScreen);
            lv_obj_delete(phone);
        }
        ui.closeSettings();
        ui.selectTab(HopticsUI::T_HOME);
        pump(50);
        lv_obj_send_event(ui.homeLoc, LV_EVENT_CLICKED, nullptr);
        pump(50);
        shot("78_location");
        ui.setGpsMode(1);
        ui.myFixTime = ui.nowTs - 25;
        ui.mySats = 7;
        ui.refreshLocation();
        pump(50);
        shot("78b_location_fix");
    }
    return 0;
}
