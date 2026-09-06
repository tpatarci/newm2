#pragma once

// The Menu page's model: the entry dialog's contents, the category list it
// offers, and the one bound the whole list has to fit inside (D-12, T-9-40,
// T-9-44, plan 09-07).
//
// ---------------------------------------------------------------------------
// WHY THIS IS A SEPARATE, GTK-FREE HEADER
// ---------------------------------------------------------------------------
//
// Three reasons, and the third is the one that made it necessary rather than
// merely tidy.
//
//  1. The tokenisation a user's typed command goes through, and the argument
//     vector the dialog SHOWS them (T-9-40), are the honest form of this
//     project's "manual entries are never shell-evaluated" guarantee. A
//     guarantee worth making is worth proving on a host with no toolkit.
//
//  2. The category fallback and its stated reason (D-12's file-only arm) are
//     wording a case compares against, and a case that spelled the list out a
//     second time would pass while the window offered something else.
//
//  3. D-08 forbids a reload notice from disturbing an open dialog. The dialog's
//     CONTENTS are this struct; the reload path writes into FormState and the
//     row list and cannot reach a MenuEntryDraft at all, and that is a fact a
//     display-free case can hold both of at once and assert.
//
// It includes Config.h (for the shared tokeniser and the entry grammar) and
// ConfigProtocol.h (for the line bound), both already free of X11, GTK and
// GLib, and nothing else.

#include "AppEntry.h"
#include "Config.h"
#include "ConfigProtocol.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>


// What the Add / Edit dialog is holding: three strings, exactly as typed.
//
// Not an AppEntry, because an AppEntry carries an argument VECTOR and the
// dialog carries the line the user is still editing. The conversion between
// them is one direction of this struct, and it is where the tokenisation
// happens -- once, through the config parser's own function.
struct MenuEntryDraft {
    std::string name;
    std::string command;    // as typed, whitespace and all
    std::string category;   // as chosen from the dropdown or typed into it

    // The argument vector this command becomes: whitespace-separated, no shell.
    // configTokeniseCommand() is the function Config::applyKeyValue() calls for
    // menu-entry-command, so what the dialog shows and what a hand-written
    // config line produces cannot differ.
    std::vector<std::string> argv() const
    {
        return configTokeniseCommand(command);
    }

    // T-9-40. What the dialog puts under the command field, so a user who types
    // a semicolon can SEE that it stayed inside one argument instead of
    // starting a second command. Square brackets rather than quotes because a
    // quote is a character a command may itself contain.
    std::string argvDisplay() const
    {
        std::string out;
        for (const std::string& token : argv()) {
            if (!out.empty()) out += " ";
            out += "[" + token + "]";
        }
        return out;
    }

    // Is there enough here to make a row? A reason rather than a bare false,
    // because the dialog shows it.
    bool complete(std::string& reasonOut) const
    {
        if (name.empty()) {
            reasonOut = "Give the entry a name -- it is what the menu row says.";
            return false;
        }
        if (argv().empty()) {
            reasonOut = "Give the entry a command to run.";
            return false;
        }
        // WR-15. The `menu-entries` WIRE grammar separates records with ';'
        // and has no escape; the FILE grammar has no such separator, so
        // `menu-entry-name = Foo;Bar` is a perfectly valid line. An entry
        // saved with a semicolon is therefore read back by the window manager,
        // returned by `get menu-entries`, and then fails to parse HERE -- for
        // the rest of the session and every session after it, with the Menu
        // page silently no longer tracking what the window manager holds.
        //
        // Refused where the user types it, which is the only place the
        // sentence can name the field it is about.
        for (const std::string* part : {&name, &command, &category}) {
            if (part->find(';') != std::string::npos) {
                reasonOut = "A ';' cannot be used here -- it is what separates "
                            "entries when the list is sent to the window manager.";
                return false;
            }
        }
        return true;
    }

    // The row. D-07 of phase 7: an entry with no category is a Custom entry,
    // applied here at creation time rather than left empty for the menu to
    // guess at later.
    AppEntry toEntry() const
    {
        AppEntry entry;
        entry.name = name;
        entry.execArgv = argv();
        entry.category = category.empty() ? "Custom" : category;
        entry.source = AppEntry::Source::Manual;
        return entry;
    }

    static MenuEntryDraft fromEntry(const AppEntry& entry)
    {
        MenuEntryDraft draft;
        draft.name = entry.name;
        for (const std::string& token : entry.execArgv) {
            if (!draft.command.empty()) draft.command += " ";
            draft.command += token;
        }
        draft.category = entry.category;
        return draft;
    }
};


// The categories a list of entries between them uses, in the ROOT MENU'S OWN
// ORDER -- alphabetical with "Custom" last -- and always including "Custom",
// which is the default a row with no category takes.
//
// This is the FALLBACK the dropdown offers when there is no window manager to
// ask. It deliberately sees only the entries in the file: with nothing running
// there is no discovery result to consult, and re-running the .desktop scan
// here would be a second implementation of discovery whose answer would
// disagree with the window manager's the moment either changed.
inline std::vector<std::string> menuCategoriesFrom(const std::vector<AppEntry>& entries)
{
    std::map<std::string, int> seen;    // std::map sorts its keys
    for (const AppEntry& entry : entries) {
        if (!entry.category.empty()) seen[entry.category] = 1;
    }

    std::vector<std::string> out;
    for (const auto& kv : seen) {
        if (kv.first == "Custom") continue;
        out.push_back(kv.first);
    }
    out.push_back("Custom");
    return out;
}


// The window manager's answer to `get menu-categories`, split back into a list.
// The separator is ';', the same one the menu-entry grammar uses, and it has no
// escape for the same reason: a category cannot contain a ';', because the
// .desktop Categories field uses ';' as its own separator.
inline std::vector<std::string> menuCategoriesFromValue(const std::string& value)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t sep = value.find(';', start);
        const std::string part = (sep == std::string::npos)
                                     ? value.substr(start)
                                     : value.substr(start, sep - start);
        if (!part.empty()) out.push_back(part);
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return out;
}


// Why the dropdown is offering only what the file contains. Shown beside the
// category control in file-only mode, and compared against by a case, so the
// window and the test cannot come to say two different things.
inline constexpr const char* kMenuCategoryFileOnlyReason =
    "No running window manager to ask, so this list is the categories your own "
    "configuration file already uses. Typing a new one here is fine.";


// T-9-44. The whole entry list travels as ONE value under `menu-entries`, and
// the protocol refuses a line longer than kConfigProtocolMaxLine before it
// scans it. A list that would not fit is refused HERE, with a sentence naming
// the limit, rather than sent and answered with an opaque framing error that
// tells the user nothing they can act on.
//
// The bound is measured on the ENCODED LINE, not on the value: the JSON
// wrapper and its escaping are part of what has to fit, and a check against the
// raw value would pass a list the window manager then rejects.
inline bool menuEntriesValueFits(const std::string& value, std::string& reasonOut)
{
    ConfigMessage message;
    message.type = ConfigMessageType::Set;
    message.key = kMenuEntriesKey;
    message.value = value;

    const std::string line = configProtocolEncode(message);
    if (line.size() <= kConfigProtocolMaxLine) return true;

    reasonOut = "That is more menu entries than can be sent in one message. The "
                "whole list travels as a single line and the limit is " +
                std::to_string(kConfigProtocolMaxLine) + " bytes; this one is " +
                std::to_string(line.size()) + ". Remove an entry, or shorten a "
                "command or a category name.";
    return false;
}
