// wm2-config -- the settings window for wm2-born-again (CGUI-01, CGUI-03,
// CGUI-04, plan 09-06).
//
// One process, one window, one GTK main loop, launched per user. No daemon, no
// second-instance coordination and no state of its own beyond the form: two
// wm2-config processes running at once are two independent clients of the
// socket, each correct on its own, and the last one to press Save wins. That
// edge is named in the plan rather than closed by it.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE OWNS
// ---------------------------------------------------------------------------
//
// The window: a header carrying the connection state, a notebook with the three
// pages D-09 assigns, and a bottom bar with Save, Revert and the one control
// that has no meaning without a running window manager. It owns the form state
// and the protocol client, and it is the only file here that includes GTK
// besides the page itself.
//
// It is also where the GLib INTEGRATION lives: g_unix_fd_add() watches the
// client's descriptor and calls its onReadable(). The client deliberately does
// not do that itself -- see the comment at the top of ProtocolClient.h for why
// keeping GLib out of it is what makes it testable without a toolkit.
//
// ---------------------------------------------------------------------------
// D-03 -- THE FILE-ONLY BANNER
// ---------------------------------------------------------------------------
//
// The sentence a file-only session shows is fixed by decision D-03 and begins
// "Not connected to a running wm2-born-again". It has exactly ONE definition in
// the whole tree -- kFileOnlyBannerText in apps/wm2-config/ConnectionState.h,
// which is where the rest of that sentence lives -- and a [wm2_config_smoke]
// case compares against that constant rather than against a copy of its own,
// which is what makes the sentence unable to change quietly.
//
// It is defined in a header rather than in this file because this file defines
// main() and links GTK: a display-free Catch2 binary can neither link it nor
// compile it, so "declared once" and "compared against by a test" are only
// simultaneously true in a header. The line above quotes the sentence's opening
// so a reader of the window's source is not sent hunting for it.

#include "AppearancePage.h"
#include "BehaviourPage.h"
#include "ConnectionState.h"
#include "FormState.h"
#include "MenuPage.h"
#include "ProtocolClient.h"

#include "Config.h"
#include "ConfigFileWriter.h"
#include "SocketServer.h"

#include <gtk/gtk.h>
#include <glib-unix.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef WM2_VERSION
#define WM2_VERSION "unknown"
#endif

namespace {

// One window, and everything it owns.
class ConfigWindow {
public:
    ConfigWindow(GtkApplication* app, std::string socketPathOverride)
        : m_socketOverride(std::move(socketPathOverride))
    {
        m_layers = configLayersFromDisk();
        m_form.seedFromLayers(m_layers);

        build(app);
        connectToWindowManager();
        render();
    }

    ~ConfigWindow()
    {
        if (m_socketSource != 0) g_source_remove(m_socketSource);
    }

    ConfigWindow(const ConfigWindow&) = delete;
    ConfigWindow& operator=(const ConfigWindow&) = delete;

private:
    // --- Construction ----------------------------------------------------

    void build(GtkApplication* app)
    {
        m_window = gtk_application_window_new(app);
        gtk_window_set_title(GTK_WINDOW(m_window), "wm2-born-again settings");
        // Tall enough that the whole Appearance page is visible without
        // scrolling, and short enough to fit inside a 1024x768 VNC session
        // with the window manager's own frame around it -- which is the
        // display this project is actually used on.
        gtk_window_set_default_size(GTK_WINDOW(m_window), 660, 690);
        g_signal_connect(m_window, "realize",
                         G_CALLBACK(&ConfigWindow::onRealize), this);
        // D-07. Wired to the DELETE EVENT rather than to a button, so the
        // title-bar close, the window manager's own close path and any other
        // route all go through the same prompt.
        g_signal_connect(m_window, "delete-event",
                         G_CALLBACK(&ConfigWindow::onDeleteEvent), this);

        GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(m_window), column);

        // The connection state, in the header: noticeable without being loud.
        // An InfoBar would shout; a dimmed line under the title states the fact
        // and gets out of the way, which is what a state that is USUALLY
        // "connected" deserves.
        m_banner = gtk_label_new("");
        gtk_widget_set_halign(m_banner, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(m_banner), TRUE);
        gtk_widget_set_margin_top(m_banner, 8);
        gtk_widget_set_margin_bottom(m_banner, 8);
        gtk_widget_set_margin_start(m_banner, 12);
        gtk_widget_set_margin_end(m_banner, 12);
        gtk_box_pack_start(GTK_BOX(column), m_banner, FALSE, FALSE, 0);

        // D-08's one-line notice, in the SAME status region as the banner.
        // A settings window with a notice at the top and another at the bottom
        // makes a user check two places for one answer, so this shares the
        // header rather than growing a second region beside it. Hidden until
        // there is something to say.
        m_notice = gtk_label_new("");
        gtk_widget_set_halign(m_notice, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(m_notice), TRUE);
        gtk_widget_set_margin_bottom(m_notice, 8);
        gtk_widget_set_margin_start(m_notice, 12);
        gtk_widget_set_margin_end(m_notice, 12);
        gtk_widget_set_no_show_all(m_notice, TRUE);
        gtk_box_pack_start(GTK_BOX(column), m_notice, FALSE, FALSE, 0);

        GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(column), separator, FALSE, FALSE, 0);

        m_notebook = gtk_notebook_new();
        gtk_widget_set_vexpand(m_notebook, TRUE);
        gtk_box_pack_start(GTK_BOX(column), m_notebook, TRUE, TRUE, 0);

        m_appearance.reset(new AppearancePage(
            m_form,
            [this](const std::string& key, const std::string& value) {
                applyLive(key, value);
            },
            [this](const std::string& message) { status(message); }));
        gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook),
                                 m_appearance->widget(),
                                 gtk_label_new("Appearance"));

        m_behaviour.reset(new BehaviourPage(
            m_form,
            [this](const std::string& key, const std::string& value) {
                applyLive(key, value);
            },
            [this](const std::string& message) { status(message); }));
        gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook),
                                 m_behaviour->widget(),
                                 gtk_label_new("Behaviour"));

        m_menu.reset(new MenuPage(
            m_form,
            [this](const std::string& key, const std::string& value) {
                applyLive(key, value);
            },
            [this](const std::string& message) { status(message); }));
        gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook),
                                 m_menu->widget(),
                                 gtk_label_new("Menu"));

        // --- The bottom bar ---
        GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_top(bar, 8);
        gtk_widget_set_margin_bottom(bar, 8);
        gtk_widget_set_margin_start(bar, 12);
        gtk_widget_set_margin_end(bar, 12);
        gtk_box_pack_start(GTK_BOX(column), bar, FALSE, FALSE, 0);

        m_status = gtk_label_new("");
        gtk_widget_set_halign(m_status, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(m_status), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(bar), m_status, TRUE, TRUE, 0);

        // The ONE control whose meaning depends on a live window manager, and
        // therefore the one that goes insensitive in file-only mode (D-03).
        // Every SETTING stays editable and Save keeps working -- a settings
        // window that refused to edit settings because the desktop was not
        // running would be useless on precisely the droplet this project is
        // for.
        m_reload = gtk_button_new_with_label("Re-read files");
        gtk_widget_set_tooltip_text(m_reload,
                                    "Tell the running window manager to read "
                                    "its configuration files again, discarding "
                                    "anything set here but not saved.");
        g_signal_connect(m_reload, "clicked",
                         G_CALLBACK(&ConfigWindow::onReloadClicked), this);
        gtk_box_pack_start(GTK_BOX(bar), m_reload, FALSE, FALSE, 0);

        m_revert = gtk_button_new_with_label("Revert");
        gtk_widget_set_tooltip_text(m_revert,
                                    "Put every setting back to what was last "
                                    "saved.");
        g_signal_connect(m_revert, "clicked",
                         G_CALLBACK(&ConfigWindow::onRevertClicked), this);
        gtk_box_pack_start(GTK_BOX(bar), m_revert, FALSE, FALSE, 0);

        m_save = gtk_button_new_with_label("Save");
        gtk_style_context_add_class(gtk_widget_get_style_context(m_save),
                                    "suggested-action");
        g_signal_connect(m_save, "clicked",
                         G_CALLBACK(&ConfigWindow::onSaveClicked), this);
        gtk_box_pack_start(GTK_BOX(bar), m_save, FALSE, FALSE, 0);

        gtk_widget_show_all(m_window);
    }

    // --- The socket ------------------------------------------------------

    void connectToWindowManager()
    {
        const std::string path = m_socketOverride.empty()
                                     ? ProtocolClient::resolveSocketPath()
                                     : m_socketOverride;

        m_client.setStateHandler([this]() { render(); });
        m_client.setNoticeHandler([this]() { onReloadNotice(); });

        if (!m_client.connect(path)) {
            // D-15: a refused handshake and an absent socket are DIFFERENT
            // situations for the person reading the banner, and only one of
            // them is worth investigating.
            m_state = (m_client.state() == ProtocolClient::State::Refused)
                          ? ConnectionState::FileOnlyRefused
                          : ConnectionState::FileOnlyNoSocket;
            return;
        }

        m_state = ConnectionState::Connected;
        attachSocketSource();
        readEffectiveValuesFromWindowManager();
        readMenuCategoriesFromWindowManager();
    }

    void attachSocketSource()
    {
        if (m_socketSource != 0) {
            g_source_remove(m_socketSource);
            m_socketSource = 0;
        }
        const int fd = m_client.fileDescriptor();
        if (fd < 0) return;
        // A SOCKET SOURCE, not a blocking read: the window keeps repainting
        // while the window manager takes its time, and a window manager that
        // never answers costs a pending callback rather than a frozen window.
        m_socketSource = g_unix_fd_add(fd,
                                       static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                       &ConfigWindow::onSocketReadable, this);
    }

    static gboolean onSocketReadable(gint, GIOCondition, gpointer userData)
    {
        ConfigWindow* self = static_cast<ConfigWindow*>(userData);
        self->m_client.onReadable();
        if (!self->m_client.connected()) {
            self->m_socketSource = 0;
            self->m_state = ConnectionState::FileOnlyNoSocket;
            self->render();
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    // Said ONCE per session, whatever else goes wrong. A window manager
    // answering a `get` with some other key's value is a defect in the window
    // manager, and repeating the complaint for each of the twenty-odd keys the
    // window reads would bury it.
    void warnOnceAboutKeyMismatch(const std::string& asked, const std::string& got)
    {
        if (m_warnedKeyMismatch) return;
        m_warnedKeyMismatch = true;
        std::fprintf(stderr,
                     "wm2-config: warning: asked the window manager for '%s' and "
                     "was answered with '%s'; that value was not adopted\n",
                     asked.c_str(), got.c_str());
    }

    // D-04: connected, the effective values are the ones the WINDOW MANAGER
    // reports -- which include its own command-line layer, something no file
    // read can see. The layered read has already run, so what a reply changes
    // is the value; where a value came from is only overwritten when the two
    // disagree, which is exactly the case the file cannot explain.
    void readEffectiveValuesFromWindowManager()
    {
        for (const std::string& key : configFileManagedKeys()) {
            const std::string k = key;
            m_client.sendGet(k, [this, k](const ConfigMessage& reply) {
                if (reply.type != ConfigMessageType::Value) return;
                // BELT TO THE TRANSPORT'S BRACES (CR-02). ProtocolClient now
                // correlates by the head request's expected type, so a reply
                // cannot reach the wrong handler through the queue -- but a
                // value adopted under the wrong key is written to the user's
                // configuration file on the next Save, and that consequence is
                // worth a second, local check that costs one comparison.
                // Reported once rather than per key, so a window manager that
                // has genuinely lost the plot says so without filling the log.
                if (reply.key != k) {
                    warnOnceAboutKeyMismatch(k, reply.key);
                    return;
                }
                const FormField* field = m_form.field(k);
                if (!field) return;
                if (field->effective == reply.value) return;
                m_form.adoptEffective(k, reply.value, ValueSource::WindowManager,
                                      std::string());
                refreshPages();
            });
        }

        // The manual entries, which travel as ONE value under their own key and
        // are not one of configFileManagedKeys() -- the writer emits them as a
        // three-key block rather than as an edit (D-12).
        m_client.sendGet(kMenuEntriesKey, [this](const ConfigMessage& reply) {
            if (reply.type != ConfigMessageType::Value) return;
            if (reply.key != kMenuEntriesKey) {
                warnOnceAboutKeyMismatch(kMenuEntriesKey, reply.key);
                return;
            }
            std::vector<AppEntry> entries;
            std::string reason;
            if (!parseMenuEntriesValue(reply.value, entries, reason)) return;
            m_form.adoptEffectiveMenuEntries(entries);
            refreshPages();
        });
    }

    // D-12's dropdown: the categories the running root menu shows, asked for
    // rather than recomputed. Read-only, and the window manager is the only
    // thing that knows the answer -- it is discovery plus the entry list, and
    // this process has no business running discovery a second time.
    void readMenuCategoriesFromWindowManager()
    {
        m_client.sendGet(kMenuCategoriesKey, [this](const ConfigMessage& reply) {
            if (reply.type != ConfigMessageType::Value) return;
            if (reply.key != kMenuCategoriesKey) {
                warnOnceAboutKeyMismatch(kMenuCategoriesKey, reply.key);
                return;
            }
            if (m_menu) {
                m_menu->setCategoriesFromWindowManager(
                    menuCategoriesFromValue(reply.value));
            }
        });
    }

    void onReloadNotice()
    {
        readMenuCategoriesFromWindowManager();
        // D-08: the window manager re-read its files, so every effective value
        // may have moved under this window. Unsaved edits are KEPT and stay
        // marked; only untouched fields follow.
        m_layers = configLayersFromDisk();
        readEffectiveValuesFromWindowManager();
        notice("The window manager re-read its configuration files. Anything "
               "you changed here and have not saved is still here, and is "
               "marked where it now differs from the file.");
    }

    void applyLive(const std::string& key, const std::string& value)
    {
        // D-05: instantly on commit, and nothing touches a file.
        if (!m_client.connected()) return;
        const std::string k = key;
        m_client.sendSet(k, value, [this, k](const ConfigMessage& reply) {
            if (reply.type == ConfigMessageType::Error) {
                status("The window manager refused " + k + ": " + reply.reason);
            }
        });
    }

    // --- The buttons -----------------------------------------------------

    void save()
    {
        const std::vector<ConfigEdit> edits = m_form.edits();
        // Asked of the MODEL, not of the edit list: the menu-entry block is
        // dirty without producing a single ConfigEdit, because the writer
        // rewrites it as a block rather than as an edit. Testing edits.empty()
        // here made a Menu-page-only change save nothing at all.
        if (!m_form.dirty()) {
            // NOT a call to the writer with an empty edit set: that would
            // rewrite the file byte-identically and still move its
            // modification time, which is a surprise nobody asked a Save they
            // pressed by reflex to produce.
            status("Nothing has changed, so nothing was written.");
            return;
        }

        std::string error;
        // The menu-entry block is rewritten only when the Menu page moved it.
        // Passing true unconditionally would rewrite those lines on every save,
        // which for a file the user hand-wrote them into is a change they did
        // not ask for (D-02).
        const ConfigWriteResult result =
            configFileWrite(m_layers.userFilePath, edits, m_form.menuEntries(),
                            m_form.menuEntriesChanged(), error);
        if (result != ConfigWriteResult::Ok) {
            status("Could not save: " + error);
            return;
        }

        m_form.markSaved();
        m_layers = configLayersFromDisk();
        refreshPages();
        status("Saved to " + m_layers.userFilePath);
    }

    void revert()
    {
        // Both halves of D-05: the form goes back to the last saved state, and
        // when there is a desktop listening it is put back too, so the file and
        // the screen agree again rather than diverging silently.
        const bool hadChanges = !m_form.divergentKeys().empty();
        discard();
        status(hadChanges ? "Reverted to the last saved settings."
                          : "There was nothing to revert.");
    }

    void reloadWindowManager()
    {
        if (!m_client.connected()) return;
        m_client.sendReload([this](const ConfigMessage& reply) {
            if (reply.type == ConfigMessageType::Error) {
                status("The window manager refused to re-read its files: " +
                       reply.reason);
            }
        });
    }

    // --- Rendering -------------------------------------------------------

    void render()
    {
        if (m_state != ConnectionState::Connected && m_client.connected()) {
            m_state = ConnectionState::Connected;
        } else if (m_client.state() == ProtocolClient::State::Refused) {
            m_state = ConnectionState::FileOnlyRefused;
        } else if (m_client.state() == ProtocolClient::State::NoSocket) {
            m_state = ConnectionState::FileOnlyNoSocket;
        }

        // D-12's file-only arm: with nothing to ask, the category dropdown
        // falls back to what the file already uses and says why.
        if (m_menu && m_state != ConnectionState::Connected) m_menu->setFileOnly();

        const std::string text = connectionBannerText(m_state, m_client.reason());
        if (m_banner) {
            char* markup = g_markup_printf_escaped(
                "<span alpha='75%%'>%s</span>", text.c_str());
            gtk_label_set_markup(GTK_LABEL(m_banner), markup);
            g_free(markup);
        }

        // Only the live-apply control loses sensitivity; every setting on every
        // page stays editable, and Save keeps working (D-03).
        if (m_reload) {
            gtk_widget_set_sensitive(m_reload, m_state == ConnectionState::Connected);
        }

        publishState();
    }

    // Every page, from the one model. Called wherever the model moved
    // underneath the window: after a save, after a revert, and when the window
    // manager reports a value this window did not set (D-08).
    void refreshPages()
    {
        if (m_appearance) m_appearance->refreshFromForm();
        if (m_behaviour)  m_behaviour->refreshFromForm();
        if (m_menu)       m_menu->refreshFromForm();
    }

    void status(const std::string& message)
    {
        if (m_status) gtk_label_set_text(GTK_LABEL(m_status), message.c_str());
    }

    // D-08's one line, in the banner's own region. Cleared by passing "".
    void notice(const std::string& message)
    {
        if (!m_notice) return;
        if (message.empty()) {
            gtk_label_set_text(GTK_LABEL(m_notice), "");
            gtk_widget_hide(m_notice);
            return;
        }
        char* markup = g_markup_printf_escaped("<i>%s</i>", message.c_str());
        gtk_label_set_markup(GTK_LABEL(m_notice), markup);
        g_free(markup);
        gtk_widget_show(m_notice);
    }

    // --- D-07: closing with unsaved changes -------------------------------
    //
    // Three responses, and the third one is the whole decision. Save writes and
    // closes. Cancel returns to the window with nothing changed. DISCARD puts
    // the form back to the last saved state AND SENDS THOSE VALUES BACK to the
    // running window manager -- without which closing this window can leave a
    // desktop that matches no file at all, which is the state a user cannot
    // reason about the next time they open anything.
    //
    // Returns true when the window may close.
    bool confirmClose()
    {
        if (!m_form.dirty()) return true;

        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(m_window),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                        GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "Save your changes before closing?");

        // What Discard actually does is stated here rather than left to the
        // word "discard", because the consequence a user needs to know is what
        // happens to the DESKTOP, not what happens to the form.
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "%s",
            m_client.connected()
                ? "Discard also puts the running desktop back to what your "
                  "configuration file says, so the two match afterwards."
                : "There is no running window manager to put back, so Discard "
                  "simply forgets these changes.");

        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_REJECT);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (response == GTK_RESPONSE_ACCEPT) {
            save();
            // A save that failed leaves the form dirty and its reason in the
            // status line; closing over it would throw the changes away after
            // the user asked for the opposite.
            return !m_form.dirty();
        }
        if (response == GTK_RESPONSE_REJECT) {
            discard();
            return true;
        }
        return false;   // Cancel, and anything else the toolkit may answer
    }

    // Put the form back AND return the desktop to it. One function with the
    // Revert button, because they are one operation (D-05 and D-07).
    void discard()
    {
        const std::vector<std::pair<std::string, std::string>> restores =
            revertAndCollectRestores(m_form);
        refreshPages();
        for (const auto& kv : restores) {
            applyLive(kv.first, kv.second);
        }
    }

    // The connection state, on this window, as a property.
    //
    // The project's own idiom -- the window manager publishes its socket path
    // the same way -- and the one observation a test can make without reading
    // pixels or synthesising input into the toolkit. A script driving several
    // desktops can use it for the same reason.
    void publishState()
    {
        if (!m_window) return;
        GdkWindow* gdkWindow = gtk_widget_get_window(m_window);
        if (!gdkWindow) return;      // not realized yet; onRealize will retry

        const char* value = connectionStateName(m_state);
        gdk_property_change(gdkWindow,
                            gdk_atom_intern(kConfigStateProperty, FALSE),
                            gdk_atom_intern("UTF8_STRING", FALSE),
                            8, GDK_PROP_MODE_REPLACE,
                            reinterpret_cast<const guchar*>(value),
                            static_cast<gint>(std::strlen(value)));
    }

    // --- Static callbacks (09-RESEARCH.md Pattern 1) ----------------------

    static void onRealize(GtkWidget*, gpointer userData)
    {
        static_cast<ConfigWindow*>(userData)->publishState();
    }

    static gboolean onDeleteEvent(GtkWidget*, GdkEvent*, gpointer userData)
    {
        // TRUE STOPS the close; FALSE lets it through. Inverted from what the
        // question reads like, so it is spelled out rather than returned bare.
        const bool mayClose = static_cast<ConfigWindow*>(userData)->confirmClose();
        return mayClose ? GDK_EVENT_PROPAGATE : GDK_EVENT_STOP;
    }

    static void onSaveClicked(GtkButton*, gpointer userData)
    {
        static_cast<ConfigWindow*>(userData)->save();
    }

    static void onRevertClicked(GtkButton*, gpointer userData)
    {
        static_cast<ConfigWindow*>(userData)->revert();
    }

    static void onReloadClicked(GtkButton*, gpointer userData)
    {
        static_cast<ConfigWindow*>(userData)->reloadWindowManager();
    }

    std::string      m_socketOverride;
    ConfigLayers     m_layers;
    FormState        m_form;
    ProtocolClient   m_client;
    ConnectionState  m_state = ConnectionState::FileOnlyNoSocket;
    guint            m_socketSource = 0;
    bool             m_warnedKeyMismatch = false;

    GtkWidget* m_window = nullptr;
    GtkWidget* m_banner = nullptr;
    GtkWidget* m_notice = nullptr;
    GtkWidget* m_notebook = nullptr;
    GtkWidget* m_status = nullptr;
    GtkWidget* m_save = nullptr;
    GtkWidget* m_revert = nullptr;
    GtkWidget* m_reload = nullptr;

    std::unique_ptr<AppearancePage> m_appearance;
    std::unique_ptr<BehaviourPage>  m_behaviour;
    std::unique_ptr<MenuPage>       m_menu;
};


// The application's single window, kept alive for the process's lifetime and
// destroyed when GTK tears the window down.
struct AppState {
    std::string socketOverride;
    std::unique_ptr<ConfigWindow> window;
};

void onActivate(GtkApplication* app, gpointer userData)
{
    AppState* state = static_cast<AppState*>(userData);
    if (!state->window) {
        state->window.reset(new ConfigWindow(app, state->socketOverride));
    }
}

void printUsage(std::FILE* out)
{
    std::fprintf(out,
        "Usage: wm2-config [--socket PATH]\n"
        "\n"
        "The settings window for the wm2-born-again window manager. Changes\n"
        "reach a running desktop as you make them; nothing is written to a\n"
        "file until you press Save, and the only file it writes is your own.\n"
        "\n"
        "Options:\n"
        "  --socket PATH   talk to this socket instead of the one $DISPLAY names\n"
        "  --version       print the version and exit\n"
        "  --help          print this message and exit\n");
}

}  // namespace


int main(int argc, char** argv)
{
    AppState state;

    // Parsed here rather than through GApplication's option machinery, which
    // would put the window's own arguments behind a callback for no gain in a
    // program that takes exactly one of them.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(stdout);
            return 0;
        }
        if (arg == "--version") {
            std::printf("wm2-config %s\n", WM2_VERSION);
            return 0;
        }
        if (arg == "--socket") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "wm2: --socket needs a path\n");
                return 2;
            }
            state.socketOverride = argv[++i];
            continue;
        }
        if (arg.rfind("--socket=", 0) == 0) {
            state.socketOverride = arg.substr(std::strlen("--socket="));
            continue;
        }
        std::fprintf(stderr, "wm2: unknown option '%s'\n", arg.c_str());
        printUsage(stderr);
        return 2;
    }

    // NON_UNIQUE by decision, not by accident: a second wm2-config is a second
    // independent client of the socket, which the plan names as an edge it does
    // not close. Making the flag explicit means the behaviour is chosen rather
    // than inherited from a default.
    GtkApplication* app = gtk_application_new("org.wm2bornagain.Config",
                                              G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &state);

    // No argv handed to GApplication -- it has already been consumed above, and
    // passing it again would make GApplication reject --socket as unknown.
    const int status = g_application_run(G_APPLICATION(app), 0, nullptr);

    state.window.reset();
    g_object_unref(app);
    return status;
}
