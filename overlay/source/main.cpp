// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink overlay: switches server routing on or off.
//
// Routing on = one marked line in the hosts file Atmosphere reads, pointing a
// Minecraft featured server at a BedrockConnect server (or straight at your server
// when it runs on port 19132). Changes apply at once: dns.mitm reloads the hosts
// file. Off = no line, and the console behaves exactly as it did before BedrockLink.
// Settings are made in the BedrockLink app.
#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstdio>
#include <string>

extern "C" {
#include "featured.h"
#include "nx_control.h"
#include "route.h"
}

namespace {

    constexpr const char *kVersion = "v1.4.0";
    constexpr SplConfigItem kEmummcType = static_cast<SplConfigItem>(65007);
    constexpr tsl::Color kGreen = {0x5, 0xF, 0x5, 0xF};
    constexpr tsl::Color kRed = {0xF, 0x6, 0x6, 0xF};
    constexpr tsl::Color kYellow = {0xF, 0xD, 0x4, 0xF};

    /* 1 = emuMMC, 0 = sysMMC, -1 = could not tell */
    int detect_boot() {
        u64 v = 0;
        if (R_FAILED(splInitialize())) return -1;
        Result rc = splGetConfig(kEmummcType, &v);
        splExit();
        return R_SUCCEEDED(rc) ? (v != 0) : -1;
    }

    Result restart_console() {
        Result rc = bpcInitialize();
        if (R_SUCCEEDED(rc)) {
            rc = bpcRebootSystem();
            bpcExit();
        }
        if (R_FAILED(rc) && R_SUCCEEDED(spsmInitialize())) {
            rc = spsmShutdown(true);
            spsmExit();
        }
        return rc;
    }

    class MainGui : public tsl::Gui {
        public:
            tsl::elm::Element *createUI() override {
                refresh();
                auto *frame = new tsl::elm::OverlayFrame("BedrockLink", kVersion);
                auto *list = new tsl::elm::List();

                list->addItem(new tsl::elm::CategoryHeader("Server routing"));
                list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    draw_status(r, x, y);
                }), 190);

                m_toggle = new tsl::elm::ToggleListItem("Route to my server", m_st.hosts_route_on);
                m_toggle->setStateChangedListener([this](bool on) { apply(on); });
                list->addItem(m_toggle);

                auto *restart = new tsl::elm::ListItem("Restart now");
                restart->setClickListener([this](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    Result rc = restart_console();
                    char b[96];
                    std::snprintf(b, sizeof b, "Restart failed (0x%x): use the Power menu", rc);
                    m_msg = b;
                    m_msg_ok = false;
                    return true;
                });
                list->addItem(restart);

                list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r, s32 x, s32 y, s32, s32) {
                    if (!m_msg.empty())
                        r->drawString(m_msg.c_str(), false, x + 15, y + 24, 16, r->a(m_msg_ok ? kGreen : kRed));
                    r->drawString("Changes apply at once. Server, featured server", false, x + 15, y + 50, 15,
                                  r->a(tsl::style::color::ColorDescription));
                    r->drawString("and route are set in the BedrockLink app.", false, x + 15, y + 72, 15,
                                  r->a(tsl::style::color::ColorDescription));
                }), 90);

                frame->setContent(list);
                return frame;
            }

        private:
            void refresh() {
                tsl::hlp::doWithSDCardHandle([&] { route_query(m_boot != 0, &m_st); });
            }

            void apply(bool on) {
                char msg[160] = "";
                int ok = 0;
                if (m_boot == 0) {
                    std::snprintf(msg, sizeof msg, "sysMMC boot: routing is for emuMMC only");
                } else if (on && !m_st.config_ok) {
                    std::snprintf(msg, sizeof msg, "%s", m_st.config_error);
                } else {
                    char target[64] = "", err[128];
                    if (on && !route_target(&m_st.cfg, target, sizeof target, err, sizeof err)) {
                        std::snprintf(msg, sizeof msg, "%s", err);
                    } else if (on && !route_is_ipv4(target)) {
                        std::snprintf(msg, sizeof msg, "%s is a name: switch on in the app", target);
                    } else {
                        tsl::hlp::doWithSDCardHandle([&] { ok = route_apply(1, on ? 1 : 0, target, msg, sizeof msg); });
                        if (ok) {
                            u32 rrc = bl_reload_hosts();
                            std::snprintf(msg, sizeof msg, "Routing %s%s", on ? "on" : "off",
                                          rrc ? ": restart to finish" : " - in effect now");
                        }
                    }
                }
                m_msg = msg;
                m_msg_ok = ok != 0;
                refresh();
                m_toggle->setState(m_st.hosts_route_on);
            }

            void line(tsl::gfx::Renderer *r, s32 x, s32 &y, const std::string &text, tsl::Color c, u32 size = 15) {
                r->drawString(text.c_str(), false, x + 15, y, size, r->a(c));
                y += 23;
            }

            void draw_status(tsl::gfx::Renderer *r, s32 x, s32 y0) {
                const auto desc = tsl::style::color::ColorDescription;
                s32 y = y0 + 24;
                if (!m_st.config_ok) {
                    line(r, x, y, m_st.config_error, kRed, 16);
                    line(r, x, y, "Set it up in the BedrockLink app.", desc);
                    return;
                }
                const route_config &c = m_st.cfg;
                line(r, x, y, c.name, tsl::style::color::ColorText, 18);
                if (c.address[0]) line(r, x, y, std::string(c.address) + " : " + std::to_string(c.port), desc);
                const char *fname = featured_name(c.replaces);
                std::string featured = fname ? fname : c.replaces;
                if (c.via == ROUTE_VIA_DIRECT)
                    line(r, x, y, featured + " -> your server (direct)", desc);
                else
                    line(r, x, y, featured + " -> BedrockConnect (" + c.bedrockconnect + ")", desc);

                bool on = m_st.hosts_route_on;
                if (on && m_st.dns_active) line(r, x, y, "Routing: ON", kGreen, 17);
                else if (!on && !m_st.dns_active) line(r, x, y, "Routing: off", desc, 17);
                else if (on) line(r, x, y, "Routing: loading (restart if it stays)", kYellow);
                else line(r, x, y, "Routing: off, still active until a restart", kYellow);
                if (on && c.via == ROUTE_VIA_BEDROCKCONNECT && c.address[0])
                    line(r, x, y, "In its menu: " + std::string(c.address) + " port " + std::to_string(c.port), desc);
                if (m_boot < 0) line(r, x, y, "Boot type unknown: assuming emuMMC", kYellow);
            }

            int m_boot = detect_boot();
            route_state m_st{};
            std::string m_msg;
            bool m_msg_ok = true;
            tsl::elm::ToggleListItem *m_toggle = nullptr;
    };

    class BedrockLinkOverlay : public tsl::Overlay {
        public:
            /* libtesla has already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys */
            void initServices() override {}
            void exitServices() override {}
            std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }
    };

}

int main(int argc, char **argv) {
    return tsl::loop<BedrockLinkOverlay>(argc, argv);
}
