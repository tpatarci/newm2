#pragma once

// What wm2-config is connected to, and what it says about it (D-03, D-15).
//
// Header-only and free of GTK, GLib and X11 for the same reason FormState.h is:
// the banner sentence D-03 fixed is compared against BY A TEST, and a test that
// spelled the sentence out a second time would pass while the window said
// something else entirely. The sentence therefore has exactly one definition in
// the whole tree -- kFileOnlyBannerText, below -- and both the window and the
// test read it from here.
//
// This file exists rather than the constant living in main.cpp because main.cpp
// defines main() and links GTK: a display-free Catch2 binary can neither link
// it nor compile it. Put another way, "declared once" and "compared against by
// a test" are only simultaneously true in a header. main.cpp names D-03 and the
// first words of the sentence in a comment so a reader of the window's source
// is not sent hunting.

#include <string>


enum class ConnectionState {
    Connected,          // hello acknowledged; live apply is available
    FileOnlyNoSocket,   // nothing was listening: no window manager to talk to
    FileOnlyRefused     // something answered and was not a wm2-born-again we speak to
};


// D-03, character for character. Changing this sentence changes what every
// file-only session says AND fails the case that compares against it, which is
// the point of there being one of it.
inline constexpr const char* kFileOnlyBannerText =
    "Not connected to a running wm2-born-again; changes take effect at next start";


// The connected counterpart. Not fixed by any decision, so it is free to be
// improved; it lives here beside its twin so the two cannot drift in tone.
inline constexpr const char* kConnectedBannerText =
    "Connected to the running wm2-born-again; changes apply as you make them";


// The whole banner line for a state.
//
// A refused handshake names the reason the client reported (D-15) on top of the
// fixed sentence, because "I could not talk to it" and "I talked to it and it
// was not what it claimed to be" are different situations for the person
// reading the window, and only the second one is worth investigating.
inline std::string connectionBannerText(ConnectionState state,
                                        const std::string& reason)
{
    switch (state) {
    case ConnectionState::Connected:
        return kConnectedBannerText;
    case ConnectionState::FileOnlyNoSocket:
        return kFileOnlyBannerText;
    case ConnectionState::FileOnlyRefused:
        return reason.empty()
                   ? std::string(kFileOnlyBannerText)
                   : std::string(kFileOnlyBannerText) + " (" + reason + ")";
    }
    return kFileOnlyBannerText;
}


// The wire spelling of a state, published on the window (see below) so a test
// -- or a script -- can read the connection state without reading pixels or
// synthesising input.
inline const char* connectionStateName(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Connected:        return "connected";
    case ConnectionState::FileOnlyNoSocket: return "file-only-no-socket";
    case ConnectionState::FileOnlyRefused:  return "file-only-refused";
    }
    return "file-only-no-socket";
}


// The property wm2-config sets on its own toplevel window, holding one of the
// three spellings above.
//
// It is how the smoke test finds the window at all (nothing else on a fixture
// display carries it) and how it observes the connection state. Reading a
// property is the project's own idiom for exactly this -- the window manager
// publishes its socket path the same way (_WM2_CONFIG_SOCKET, DISC-03) -- and
// it is the one observation that needs neither a screenshot comparison nor
// synthesised input, both of which would test the toolkit rather than this
// program.
inline constexpr const char* kConfigStateProperty = "_WM2_CONFIG_STATE";
