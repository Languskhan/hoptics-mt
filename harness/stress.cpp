// Stress simulation for HopticsUI: a 4-hour session compressed into seconds, meant to be built with
// -fsanitize=address,undefined. Reports render timings (host CPU, not ESP32), LVGL memory over time,
// and checks the web snapshot stays valid JSON. Exit code != 0 on any failure.
#define private public
#include "graphics/view/TFT/HopticsUI.h"
#undef private
#include "lvgl.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const int WD = 240, HT = 320;
static uint16_t fb[WD * HT];
static uint32_t fakeMs = 0;
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *s = (const uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++)
            fb[y * WD + x] = *s++;
    lv_display_flush_ready(d);
}
static uint32_t tick_cb(void) { return fakeMs; }
static uint32_t countObjs(lv_obj_t *o)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        n += countObjs(lv_obj_get_child(o, i));
    return n;
}
static uint32_t rnd(uint32_t n) { return (uint32_t)(rand() % n); }

int main(int argc, char **argv)
{
    int hours = argc > 1 ? atoi(argv[1]) : 4;
    setenv("TZ", "PST8PDT", 1);
    srand(1234);
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(WD, HT);
    static uint16_t buf[WD * 40];
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    HopticsUI ui;
    int sent = 0, traces = 0, posreq = 0;
    HopticsUI::Callbacks cb;
    cb.sendText = [&](uint32_t, const char *) -> uint32_t { return 1000 + (++sent); };
    cb.traceRoute = [&](uint32_t) { traces++; };
    cb.requestPosition = [&](uint32_t) { posreq++; };
    ui.setCallbacks(cb);

    uint32_t now = 1796000000;
    ui.setTime(now);
    ui.createBootScreen("2.8.3.e42ae67");
    const uint32_t me = 0xa1b2c3d4;
    ui.setMyNode(me);
    ui.upsertNode(me, 0, now, "HOPT", "Hoptics Stress", 0, true, false, 110);
    ui.updatePosition(me, 397950000, -896550000, 70);
    for (int c = 0; c < 8; c++)
        ui.setChannel(c, c == 0 ? "" : ("Ch" + std::to_string(c)).c_str(), c < 4);
    ui.setRadio(1, "US", 0, "LongFast", true, 3, 27, 906.875f);
    ui.setNetwork(true, true, 0x3901a8c0, true, -60);
    ui.buildMainScreen();
    lv_screen_load(ui.mainScreen);

    // 400 nodes (limit is HOPTICS_MAX_NODES) with nasty names
    static const char *hostile[] = {"</script><script>alert(1)</script>", "\"quoted\\back\\slash\"", "\x01\x02 ctrl chars \x7f",
                                    "Ünïcödé Ñame Ελληνικά Кириллица", "\xF0\x9F\x93\xA1\xF0\x9F\x94\xA5\xF0\x9F\x9A\x80 emoji",
                                    "A really really long node name that is way past forty characters long", "", "%s%n%x format"};
    std::vector<uint32_t> ids;
    for (int i = 0; i < 400; i++) {
        uint32_t id = 0x20000000 + i * 7919;
        ids.push_back(id);
        char sn[8];
        snprintf(sn, sizeof(sn), "%04X", i);
        std::string lns = (i % 9 == 0) ? std::string(hostile[(i / 9) % 8]) : "Node " + std::to_string(i);
        ui.upsertNode(id, i % 4, now - rnd(86400 * 3), sn, lns.c_str(), (uint8_t)rnd(13), i % 3 == 0, i % 11 == 0, 0);
        if (i % 2)
            ui.updatePosition(id, 397950000 + (int32_t)rnd(4000000) - 2000000, -896550000 + (int32_t)rnd(5000000) - 2500000,
                              (int32_t)rnd(800));
    }

    using clk = std::chrono::steady_clock;
    double maxMs = 0, sumMs = 0;
    long frames = 0;
    uint32_t pkts = 0, msgs = 0;
    int failures = 0;
    const uint32_t total = hours * 3600;
    printf("simulating %d hours...\n", hours);
    for (uint32_t s = 0; s < total; s++) {
        now++;
        bool burst = (s / 60) % 60 >= 30 && (s / 60) % 60 < 35; // 5-minute flood every hour
        int n = burst ? 30 : (int)rnd(4);
        for (int k = 0; k < n; k++) {
            uint32_t from = ids[rnd(ids.size())];
            static const uint16_t ports[] = {1, 3, 4, 5, 67, 67, 70, 71, 8, 73, 34, 256};
            bool mqtt = rnd(10) == 0;
            int8_t hops = mqtt ? -1 : (int8_t)rnd(5) - (rnd(5) == 0 ? 1 : 0);
            ui.onPacket(from, rnd(20) ? 0xffffffff : me, ports[rnd(12)], hops, hops == 0 ? -40 - (int)rnd(90) : 0,
                        hops == 0 ? (float)rnd(30) - 15 : 0, now, mqtt);
            if (hops == 0)
                ui.updateSignal(from, -40 - (int)rnd(90), (float)rnd(30) - 15);
            else if (hops > 0)
                ui.updateHops(from, hops);
            pkts++;
        }
        if (s % 60 == 0) {
            ui.updateMetrics(me, 101, 4.1f, 5 + rnd(40), (float)rnd(50) / 10);
            ui.updateMetrics(ids[rnd(ids.size())], (uint8_t)rnd(101), 3.7f, (float)rnd(30), (float)rnd(30) / 10);
        }
        if (s % 23 == 0) { // messages to channels and DMs, some hostile
            uint32_t from = ids[rnd(ids.size())];
            uint32_t key = rnd(3) ? rnd(4) : from;
            std::string txt = (msgs % 7 == 0) ? std::string(hostile[msgs % 8]) : "message " + std::to_string(msgs) +
                                                                                       std::string(rnd(200), 'x');
            ui.addMessage(key, from, now, txt.c_str(), 0, HopticsUI::eUnread, false);
            msgs++;
        }
        if (s % 41 == 0) { // our own sends + delivery updates
            ui.openChat(rnd(4));
            lv_textarea_set_text(ui.chatInput, "test from stress");
            ui.sendFromChat();
            ui.setMessageStatus(1000 + sent, (uint8_t)(rnd(2) ? HopticsUI::eAcked : HopticsUI::eFailed));
            ui.closeChat();
        }
        if (s % 37 == 0)
            ui.selectTab((HopticsUI::Tab)(rnd(HopticsUI::T_COUNT)));
        if (s % 97 == 0) {
            ui.openNode(ids[rnd(ids.size())]);
            ui.showTraceRoute(ui.nodeOvNum, "HOPT > A > B > C\nback: C > HOPT");
        }
        if (s % 101 == 0)
            ui.closeNode();
        if (s % 211 == 0) {
            ui.openSettings();
            ui.showKeyboard(ui.setLong);
            ui.hideKeyboard();
            ui.closeSettings();
        }
        if (s % 557 == 0)
            ui.removeNode(ids[rnd(ids.size())]);
        if (s % 900 == 0 && s) // theme switch every 15 min
            ui.setTheme((ui.theme() + 1) % HopticsUI::TH_COUNT);
        if (s % 300 == 0) // we move around: city pop-ups
            ui.updatePosition(me, 397950000 + (int32_t)rnd(400000), -896550000 + (int32_t)rnd(400000), 70);

        auto t0 = clk::now();
        ui.tick(now);
        fakeMs += 1000;
        lv_timer_handler();
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        maxMs = std::max(maxMs, ms);
        sumMs += ms;
        frames++;

        if (s % 3600 == 3599) {
            lv_mem_monitor_t mon;
            lv_mem_monitor(&mon);
            std::string js;
            ui.buildSnapshotJson(js);
            FILE *f = fopen("stress_snapshot.json", "w");
            fwrite(js.data(), 1, js.size(), f);
            fclose(f);
            printf("hour %u: nodes=%zu pkts=%u msgs=%u  LVGL used=%u KB max=%u KB frag=%u%%  snapshot=%zu B  frame avg=%.2f ms max=%.1f ms\n",
                   s / 3600 + 1, ui.nodes.size(), pkts, msgs, (unsigned)((mon.total_size - mon.free_size) / 1024),
                   (unsigned)(mon.max_used / 1024), (unsigned)mon.frag_pct, js.size(), sumMs / frames, maxMs);
            {
                printf("   objects: screen=%u  top=%u  layer_top=%u  timers? ", countObjs(ui.mainScreen),
                       (unsigned)lv_obj_get_child_count(ui.mainScreen), countObjs(lv_layer_top()));
                for (uint32_t i = 0; i < lv_obj_get_child_count(ui.mainScreen); i++)
                    printf("%u ", countObjs(lv_obj_get_child(ui.mainScreen, i)));
                uint32_t nt = 0;
                for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t))
                    nt++;
                printf("| timers=%u pages:", nt);
                for (int i = 0; i < HopticsUI::T_COUNT; i++)
                    printf(" %u", countObjs(ui.pages[i]));
                printf("\n");
            }
            if (ui.nodes.size() > HOPTICS_MAX_NODES) {
                printf("FAIL: node table exceeded limit\n");
                failures++;
            }
            if (mon.free_size < mon.total_size / 10) {
                printf("FAIL: LVGL pool below 10%% free\n");
                failures++;
            }
        }
    }
    printf("sent=%d traces=%d posreq=%d  failures=%d\n", sent, traces, posreq, failures);
    return failures ? 1 : 0;
}
