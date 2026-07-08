#pragma once

#include <string>
#include <vector>

// Shared data model for the application-discovery subsystem (Phase 7).
// Plain data, no X11/Xlib dependency -- mirrors include/Config.h's convention
// so it stays unit-testable without Xvfb.
struct AppEntry {
    // Where this entry came from -- BinaryScan/Manual sources are added by
    // later plans in this phase; this plan only ever produces Desktop.
    enum class Source { Desktop, BinaryScan, Manual };

    std::string name;

    // Pre-tokenized argv: field codes already stripped, quoting already
    // resolved. Safe to pass directly to execvp() -- this is never a raw
    // shell string and must never be routed through /bin/sh -c.
    std::vector<std::string> execArgv;

    // Opaque display string only, per the Icon= path-traversal threat
    // disposition (T-7-02). Never opened, stat'd, or resolved as a
    // filesystem path in this phase -- icons are not rendered.
    std::string icon;

    // D-07: manual/uncategorized entries default to "Custom".
    std::string category = "Custom";

    Source source = Source::Desktop;
};
