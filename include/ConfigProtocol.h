#pragma once

// The version-1 wire contract between the window manager and its clients:
// one newline-delimited JSON message, encoded and decoded.
//
// This header is deliberately free of X11, of GTK, of GLib and of every
// project header -- it includes nothing but the C++ standard library. The
// window manager, wm2-ctl, wm2-config and a Catch2 case all compile this
// same file, so any dependency it acquires becomes a dependency of all four.
// wm2-ctl in particular ships in the window-manager package and must have no
// GTK dependency at all (D-17), and test_config_protocol must be runnable on
// a host with no X server. That constraint is the whole reason the codec
// lives in a header rather than inside the socket loop that reads from it.
//
// ---------------------------------------------------------------------------
// DISC-01 -- THE FROZEN CONTRACT
// ---------------------------------------------------------------------------
//
// Taken at the 09-02 decision checkpoint, by a human, before the first byte of
// the codec was written. Option `minimal` was chosen over
// `minimal-plus-capabilities` and over deferring: the smallest surface that
// satisfies every locked decision, with nothing speculative in version 1.
//
// From v1.0 a user's shell script, a monitoring cron job or a third-party
// client may depend on every name below. D-8.5-01 is this project's rule for
// that situation: a name is free to change before v1.0 and breaking after,
// with no deprecated aliases. So renaming any of these later is a documented
// breaking change, not a refactor.
//
// Frozen, in full:
//
//   * Eleven message types, and no twelfth: hello, hello-ack, get, value,
//     set, ack, error, reload, reloaded, status, status-reply. Every one is
//     required by a locked decision -- the handshake pair by D-15, get/value/
//     set/ack/error and reload/reloaded by D-17's wm2-ctl verbs, status/
//     status-reply by D-14.
//   * Framing: one JSON object per line, terminated by a single '\n'. A line
//     longer than kConfigProtocolMaxLine is rejected before it is scanned.
//   * Socket path: $XDG_RUNTIME_DIR/wm2-born-again/socket<display>, with the
//     documented fallback /tmp/wm2-born-again-<uid>/socket<display> (D-16).
//   * Discovery: the root window carries _WM2_CONFIG_SOCKET, an XA_STRING
//     holding that path.
//
// The last two are frozen HERE and built in 09-03, which owns
// configSocketPath() and the property publish. They are recorded in this file
// because this file is the contract; a client that reads only this header
// knows the whole of what it may rely on.
//
// DISC-01c -- an unknown MEMBER name is malformed, while an unknown TYPE name
// is a named verdict. The asymmetry is deliberate. A v1 peer that quietly
// ignored an unknown member would accept a v2 `set` while dropping the
// qualifier that changed its meaning -- it would act on a message it did not
// understand. An unknown type it cannot act on at all, so naming it
// (UnknownType) and declining is safe and is what lets a later version add a
// message additively. The protocol number in the hello is how a peer learns
// what it may send; a new member rides a version bump, not silence.
//
// THE MENU-ENTRY VALUE GRAMMAR (plan 09-05, D-12). Not a frozen message type
// and not a change to one: it is the agreed shape of the `value` member of a
// `get`/`set`/`value` whose `key` is the literal `menu-entries`, and it is
// written down HERE because this file is the contract between the window
// manager, wm2-ctl and the settings window, and all three have to spell it the
// same way.
//
// The config file's manual entries are an ORDERED, STATEFUL accumulator --
// `menu-entry-name` opens an entry and `menu-entry-command` /
// `menu-entry-category` fill in whichever is open -- which reads correctly as
// consecutive lines of a file and does not survive being cut into independent
// request-reply messages. So the WHOLE list travels as ONE value, in the file's
// own key order, records separated by ';':
//
//   menu-entry-name=Editor;menu-entry-command=/usr/bin/vim;menu-entry-category=Custom
//
// WHOLESALE REPLACEMENT, never a mutation of one row: a `set` of this key
// replaces the entire list, and the empty value clears it. That is what makes
// the operation idempotent, and what lets a settings window's Add, Edit and
// Remove rows map onto it with no per-row protocol and no row identity for the
// two ends to keep in sync. A client that wants to change one row sends the
// whole list back with that row changed, having read it with `get`.
//
// The separator has NO ESCAPE, deliberately: an escape needs a second grammar
// and a second grammar is a second thing to get wrong. A command that must
// contain a ';' is written into the config file directly, where the
// accumulator's line-per-key form has no separator to collide with.
//
// `menu-entries` is deliberately NOT one of the keys `configKeySpecs()` names.
// That list is a view of the single settings the option table declares and is
// asserted equal to the config file writer's managed key list, and the writer
// emits the three accumulator keys rather than a `menu-entries=` line.
//
// DISC-01d -- the window-manager version travels in the status-reply field
// list (D-14 names it as status), not as a member of hello-ack. D-15 asks the
// handshake for a program name and a protocol version and nothing else, and
// one fewer member is one fewer thing frozen forever.
//
// ---------------------------------------------------------------------------
//
// The grammar this file implements is deliberately ONE LEVEL DEEP: an object
// whose members are strings, one bare unsigned integer (protocol), and one
// flat array of strings (fields). There is no nesting, so there is no
// recursion in the parser and no depth for an adversary to exhaust (T-9-06).
// Every decode outcome is one of four named enumerators; no path returns Ok
// with an unset type, and there is no else-less fallthrough anywhere in the
// file -- the house rule include/MenuPaint.h's `Foreign` enumerator documents.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>


// The version this build speaks. Sent in the hello, echoed in the hello-ack.
inline constexpr int kConfigProtocolVersion = 1;

// The longest line the decoder will look at, INCLUDING its terminating
// newline. 4096 is not an arbitrary round number: it is the same per-line
// guard Config::applyFile() already enforces on the config file
// (src/Config.cpp, "Skip lines > 4096 chars"), so a value that fits in the
// file fits on the wire and vice versa. Keeping the two equal means a user can
// never author a config line the protocol cannot carry.
inline constexpr std::size_t kConfigProtocolMaxLine = 4096;


// -----------------------------------------------------------------------------
// X11 INTEROPERABILITY GUARD -- not part of the contract, required by it
// -----------------------------------------------------------------------------
//
// Xlib.h contains `#define Status int` (X11/Xlib.h:83). It is a PREPROCESSOR
// MACRO, not a typedef, so it rewrites the token `Status` everywhere it appears
// after Xlib is included -- including inside the enumerator list below and
// inside `ConfigMessageType::Status` at every use site.
//
// `status` is one of the eleven message types D-14 froze at the 09-02 decision
// checkpoint, and D-8.5-01 makes that spelling permanent. So the collision
// cannot be resolved by renaming the enumerator; it is resolved here, once, on
// behalf of every consumer.
//
// The macro is REPLACED BY A TYPEDEF rather than merely removed, and that is
// load-bearing: X11/extensions/shape.h and X11/extensions/Xrandr.h both declare
// functions RETURNING `Status`, so a bare undef makes every one of them fail to
// compile. `Status` is a bare `int` in Xlib, so the typedef is exactly what the
// macro meant -- and a global typedef does not reach into a scoped enum's own
// scope, which is what lets ConfigMessageType::Status and Xlib's Status coexist
// in one translation unit.
//
// Deliberately narrow. `Bool`, `True` and `False` -- the other Xlib macros of
// this kind -- are left completely alone, because the window manager uses all
// three constantly and none of them collides with anything here.
#ifdef Status
#undef Status
typedef int Status;
#endif


// Every message this version speaks, plus the named landing place for one it
// does not. `Unknown` is not a wire spelling and is never transmitted; it is
// the value a decode leaves behind when the type field held a string this
// version has no enumerator for.
enum class ConfigMessageType {
    Hello,        // client -> WM   : program, protocol
    HelloAck,     // WM -> client   : program, protocol
    Get,          // client -> WM   : key
    Value,        // WM -> client   : key, value
    Set,          // client -> WM   : key, value
    Ack,          // WM -> client   : key
    Error,        // WM -> client   : key, reason
    Reload,       // client -> WM   : (no members)
    Reloaded,     // WM -> client   : (no members)
    Status,       // client -> WM   : (no members)
    StatusReply,  // WM -> client   : fields
    Unknown       // a type string this version does not speak
};


// One decoded message. Which members carry meaning depends on the type -- see
// the comments on the enumerators above. Members a type does not carry are
// left empty by the decoder and are not emitted by the encoder, so a round
// trip is exact rather than merely lossless.
struct ConfigMessage {
    ConfigMessageType type = ConfigMessageType::Unknown;

    std::string program;   // hello, hello-ack
    int         protocol = 0;  // hello, hello-ack
    std::string key;       // get, value, set, ack, error
    std::string value;     // value, set
    std::string reason;    // error

    // status-reply only. An ORDERED list of name/value pairs rather than a
    // nested object: order is part of what a status display wants, and JSON
    // object member order is not guaranteed. On the wire it is one flat array
    // of strings, name and value alternating -- which keeps the grammar one
    // level deep.
    std::vector<std::pair<std::string, std::string>> fields;
};


// Why a decode did not produce a message, or that it did. Four outcomes, and
// every path through the decoder returns exactly one of them.
enum class ConfigDecodeResult {
    Ok,           // a message this version speaks, fully parsed
    TooLong,      // over kConfigProtocolMaxLine; rejected before any scan
    Malformed,    // not a one-level object, or a member the grammar rejects
    UnknownType   // a well-formed object whose type this version does not speak
};


// Is this arriving message an UNSOLICITED NOTICE rather than the reply the
// request asked for?
//
// D-08 reuses `reloaded` as both the reply to a client's own `reload` and a
// broadcast to every hello-completed connection, which is what let this phase
// avoid a twelfth message type. A client waiting for some other reply
// therefore has to be able to tell the two apart, and the only thing that
// distinguishes them is what the client asked for.
//
// NOT PART OF THE WIRE GRAMMAR. This adds no type, changes no member and
// touches neither the encoder nor the decoder; it is a classification over the
// eleven types the version-1 contract froze, spelled once so wm2-ctl and
// wm2-config cannot disagree about it (WR-12, CR-02).
inline bool configProtocolIsUnsolicitedNotice(ConfigMessageType arrived,
                                              ConfigMessageType expected) {
    return arrived == ConfigMessageType::Reloaded &&
           expected != ConfigMessageType::Reloaded;
}


// The wire spelling of a message type. Returns "" for Unknown, which is not a
// wire spelling.
inline const char* configMessageTypeName(ConfigMessageType type) {
    switch (type) {
        case ConfigMessageType::Hello:       return "hello";
        case ConfigMessageType::HelloAck:    return "hello-ack";
        case ConfigMessageType::Get:         return "get";
        case ConfigMessageType::Value:       return "value";
        case ConfigMessageType::Set:         return "set";
        case ConfigMessageType::Ack:         return "ack";
        case ConfigMessageType::Error:       return "error";
        case ConfigMessageType::Reload:      return "reload";
        case ConfigMessageType::Reloaded:    return "reloaded";
        case ConfigMessageType::Status:      return "status";
        case ConfigMessageType::StatusReply: return "status-reply";
        case ConfigMessageType::Unknown:     return "";
    }
    return "";  // unreachable; present because the enum could gain a value
}


// The inverse. Any string that is not one of the eleven frozen spellings maps
// to Unknown -- including the empty string, which is what configMessageTypeName
// returns for Unknown, so the two functions agree on that boundary.
inline ConfigMessageType configMessageTypeFromName(const std::string& name) {
    if (name == "hello")        return ConfigMessageType::Hello;
    if (name == "hello-ack")    return ConfigMessageType::HelloAck;
    if (name == "get")          return ConfigMessageType::Get;
    if (name == "value")        return ConfigMessageType::Value;
    if (name == "set")          return ConfigMessageType::Set;
    if (name == "ack")          return ConfigMessageType::Ack;
    if (name == "error")        return ConfigMessageType::Error;
    if (name == "reload")       return ConfigMessageType::Reload;
    if (name == "reloaded")     return ConfigMessageType::Reloaded;
    if (name == "status")       return ConfigMessageType::Status;
    if (name == "status-reply") return ConfigMessageType::StatusReply;
    return ConfigMessageType::Unknown;
}


// -----------------------------------------------------------------------------
// Encoding
// -----------------------------------------------------------------------------

// Appends `s` to `out` as a JSON string literal, quotes included.
//
// Escaped: the two escapes JSON requires ('"' and '\\'), the four named
// control escapes ('\b', '\f', '\n', '\r', '\t' -- five, with the two above
// that is the seven-way switch below), and every other byte below 0x20 as
// \u00XX. '/' is deliberately NOT escaped: JSON permits \/ but does not
// require it, and emitting it would make the encoder's output differ from the
// obvious hand-written form for no gain.
//
// Bytes at or above 0x20, including every byte of a UTF-8 sequence, are copied
// through unchanged. The wire is therefore 8-bit clean for anything the config
// file itself can hold.
inline void configProtocolAppendJsonString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static const char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0x0F];
                    out += kHex[c & 0x0F];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}


// Appends `"name":` to `out`, with a leading comma. Every member but the type
// is preceded by one, and the type is always first, so the comma is never
// conditional.
inline void configProtocolAppendMemberName(const char* name, std::string& out) {
    out += ",\"";
    out += name;
    out += "\":";
}


// Encodes one message as a single line INCLUDING its terminating newline.
//
// Member order is fixed (type, program, protocol, key, value, reason, fields)
// and only the members the type carries are emitted, so the same message
// always produces the same bytes. That determinism is what the round-trip
// cases and any future byte-level comparison rest on.
//
// An Unknown message is NOT encodable -- it is a decode verdict, not something
// a sender may transmit -- and yields the empty string. The empty string is
// never a valid line, because a valid line always ends in '\n', so a caller
// that fails to check cannot accidentally put a half-message on the wire.
inline std::string configProtocolEncode(const ConfigMessage& message) {
    const char* name = configMessageTypeName(message.type);
    if (name[0] == '\0') return std::string();

    std::string out;
    out.reserve(64);
    out += "{\"type\":";
    configProtocolAppendJsonString(name, out);

    switch (message.type) {
        case ConfigMessageType::Hello:
        case ConfigMessageType::HelloAck:
            configProtocolAppendMemberName("program", out);
            configProtocolAppendJsonString(message.program, out);
            configProtocolAppendMemberName("protocol", out);
            out += std::to_string(message.protocol);
            break;

        case ConfigMessageType::Get:
        case ConfigMessageType::Ack:
            configProtocolAppendMemberName("key", out);
            configProtocolAppendJsonString(message.key, out);
            break;

        case ConfigMessageType::Value:
        case ConfigMessageType::Set:
            configProtocolAppendMemberName("key", out);
            configProtocolAppendJsonString(message.key, out);
            configProtocolAppendMemberName("value", out);
            configProtocolAppendJsonString(message.value, out);
            break;

        case ConfigMessageType::Error:
            configProtocolAppendMemberName("key", out);
            configProtocolAppendJsonString(message.key, out);
            configProtocolAppendMemberName("reason", out);
            configProtocolAppendJsonString(message.reason, out);
            break;

        case ConfigMessageType::StatusReply:
            configProtocolAppendMemberName("fields", out);
            out += '[';
            for (std::size_t i = 0; i < message.fields.size(); ++i) {
                if (i != 0) out += ',';
                configProtocolAppendJsonString(message.fields[i].first, out);
                out += ',';
                configProtocolAppendJsonString(message.fields[i].second, out);
            }
            out += ']';
            break;

        case ConfigMessageType::Reload:
        case ConfigMessageType::Reloaded:
        case ConfigMessageType::Status:
            // These three carry no members at all. Named rather than left to
            // a default arm, so adding a twelfth type is a compile error here
            // instead of a message that silently loses its payload.
            break;

        case ConfigMessageType::Unknown:
            // Unreachable: the empty-name early return above already handled
            // it. Named anyway, for the same reason as the arm above.
            return std::string();
    }

    out += "}\n";
    return out;
}


// -----------------------------------------------------------------------------
// Decoding
// -----------------------------------------------------------------------------

namespace config_protocol_detail {

// Appends the UTF-8 encoding of one Unicode code point.
inline void appendUtf8(unsigned int cp, std::string& out) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Reads four hex digits at `p` into `outValue`. Returns false on anything
// that is not four hex digits, including a short line.
inline bool readHex4(const std::string& s, std::size_t end, std::size_t p,
                     unsigned int& outValue) {
    if (p + 4 > end) return false;
    unsigned int v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = s[p + i];
        unsigned int digit;
        if (c >= '0' && c <= '9')      digit = static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned int>(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned int>(c - 'A') + 10;
        else return false;
        v = (v << 4) | digit;
    }
    outValue = v;
    return true;
}

inline void skipSpace(const std::string& s, std::size_t end, std::size_t& p) {
    while (p < end && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r')) ++p;
}

// Parses a JSON string literal starting at `p` (which must hold the opening
// quote) into `out`, leaving `p` one past the closing quote. Returns false on
// anything the grammar rejects, including an unterminated literal, a raw
// control character inside the literal, an escape this codec does not accept,
// and a lone surrogate.
inline bool parseString(const std::string& s, std::size_t end, std::size_t& p,
                        std::string& out) {
    out.clear();
    if (p >= end || s[p] != '"') return false;
    ++p;

    while (p < end) {
        const unsigned char c = static_cast<unsigned char>(s[p]);

        if (c == '"') {
            ++p;
            return true;
        }

        if (c < 0x20) {
            // A raw control character inside a string literal is invalid JSON,
            // and a raw newline would additionally break the framing. Rejected
            // rather than passed through.
            return false;
        }

        if (c != '\\') {
            out += static_cast<char>(c);
            ++p;
            continue;
        }

        ++p;
        if (p >= end) return false;
        const char esc = s[p];
        ++p;
        switch (esc) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;  // accepted on input, never emitted
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                unsigned int cp = 0;
                if (!readHex4(s, end, p, cp)) return false;
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // High surrogate: a low surrogate must follow, as its own
                    // \u escape, or this is not a code point at all.
                    if (p + 1 >= end || s[p] != '\\' || s[p + 1] != 'u') return false;
                    unsigned int low = 0;
                    if (!readHex4(s, end, p + 2, low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF) return false;
                    p += 6;
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;  // a lone low surrogate
                }
                appendUtf8(cp, out);
                break;
            }
            default:
                return false;  // an escape this codec does not accept
        }
    }

    return false;  // ran off the end with the literal still open
}

}  // namespace config_protocol_detail


// Decodes ONE line into `out`.
//
// `line` may carry its terminating newline or not, and may carry further bytes
// after it: decoding stops at the first '\n' and never looks past it, so a
// caller that read two frames in one recv() cannot have the second one
// silently influence the first.
//
// `out` is reset before anything else is done, so a failed decode never leaves
// members from a previous message behind for a caller to act on.
//
// Decoding is a pure function of `line`: it holds no state between calls, so
// two decoders running concurrently over different lines cannot affect each
// other, and an interrupted decode leaves no state to corrupt (CGUI-02).
inline ConfigDecodeResult configProtocolDecode(const std::string& line, ConfigMessage& out) {
    using namespace config_protocol_detail;

    // THE LENGTH BOUND IS CHECKED FIRST, before the line is scanned at all --
    // including before the search for the newline (T-9-05). The decoder never
    // grows a buffer on the caller's behalf; rejecting is O(1) regardless of
    // how much the peer sent.
    if (line.size() > kConfigProtocolMaxLine) return ConfigDecodeResult::TooLong;

    out = ConfigMessage();

    // Everything past the first newline belongs to the next frame.
    std::size_t end = line.find('\n');
    if (end == std::string::npos) end = line.size();

    std::size_t p = 0;
    skipSpace(line, end, p);
    if (p >= end || line[p] != '{') return ConfigDecodeResult::Malformed;
    ++p;

    bool sawType = false;
    std::string typeName;
    bool first = true;

    skipSpace(line, end, p);
    if (p < end && line[p] == '}') {
        ++p;  // the empty object: well-formed JSON, but it carries no type
    } else {
        for (;;) {
            if (!first) {
                skipSpace(line, end, p);
                if (p >= end || line[p] != ',') return ConfigDecodeResult::Malformed;
                ++p;
            }
            first = false;

            skipSpace(line, end, p);
            std::string name;
            if (!parseString(line, end, p, name)) return ConfigDecodeResult::Malformed;

            skipSpace(line, end, p);
            if (p >= end || line[p] != ':') return ConfigDecodeResult::Malformed;
            ++p;
            skipSpace(line, end, p);

            if (name == "type") {
                if (sawType) return ConfigDecodeResult::Malformed;  // repeated member
                if (!parseString(line, end, p, typeName)) return ConfigDecodeResult::Malformed;
                sawType = true;
            } else if (name == "program") {
                if (!parseString(line, end, p, out.program)) return ConfigDecodeResult::Malformed;
            } else if (name == "key") {
                if (!parseString(line, end, p, out.key)) return ConfigDecodeResult::Malformed;
            } else if (name == "value") {
                if (!parseString(line, end, p, out.value)) return ConfigDecodeResult::Malformed;
            } else if (name == "reason") {
                if (!parseString(line, end, p, out.reason)) return ConfigDecodeResult::Malformed;
            } else if (name == "protocol") {
                // A bare unsigned integer and nothing else: no sign, no
                // fraction, no exponent, no quotes. Bounded to nine digits,
                // which cannot overflow an int and is four orders of magnitude
                // more version numbers than this project will ever have.
                const std::size_t digitsBegin = p;
                while (p < end && line[p] >= '0' && line[p] <= '9') ++p;
                const std::size_t digitCount = p - digitsBegin;
                if (digitCount == 0 || digitCount > 9) return ConfigDecodeResult::Malformed;
                int v = 0;
                for (std::size_t i = digitsBegin; i < p; ++i) v = v * 10 + (line[i] - '0');
                out.protocol = v;
            } else if (name == "fields") {
                // One FLAT array of strings, name and value alternating. Flat
                // is the point: an array of pairs or an array of objects would
                // put a second level in the grammar, and the parser has no
                // recursion to descend one with.
                if (p >= end || line[p] != '[') return ConfigDecodeResult::Malformed;
                ++p;
                std::vector<std::string> flat;
                skipSpace(line, end, p);
                if (p < end && line[p] == ']') {
                    ++p;
                } else {
                    for (;;) {
                        skipSpace(line, end, p);
                        std::string item;
                        if (!parseString(line, end, p, item)) return ConfigDecodeResult::Malformed;
                        flat.push_back(item);
                        skipSpace(line, end, p);
                        if (p >= end) return ConfigDecodeResult::Malformed;
                        if (line[p] == ',') { ++p; continue; }
                        if (line[p] == ']') { ++p; break; }
                        return ConfigDecodeResult::Malformed;
                    }
                }
                if (flat.size() % 2 != 0) return ConfigDecodeResult::Malformed;
                out.fields.clear();
                for (std::size_t i = 0; i + 1 < flat.size(); i += 2) {
                    out.fields.push_back({flat[i], flat[i + 1]});
                }
            } else {
                // DISC-01c: an unknown member is malformed, not ignored.
                return ConfigDecodeResult::Malformed;
            }

            skipSpace(line, end, p);
            if (p >= end) return ConfigDecodeResult::Malformed;
            if (line[p] == '}') { ++p; break; }
            if (line[p] == ',') continue;
            return ConfigDecodeResult::Malformed;
        }
    }

    // Nothing but whitespace may follow the closing brace on this line.
    skipSpace(line, end, p);
    if (p != end) return ConfigDecodeResult::Malformed;

    if (!sawType) return ConfigDecodeResult::Malformed;

    const ConfigMessageType type = configMessageTypeFromName(typeName);
    if (type == ConfigMessageType::Unknown) {
        // The object parsed; this version simply does not speak its type. The
        // members that WERE understood are left in `out` -- a caller logging
        // the rejection may want them -- but `out.type` stays Unknown, so
        // there is no path that returns a usable message here.
        return ConfigDecodeResult::UnknownType;
    }

    out.type = type;
    return ConfigDecodeResult::Ok;
}
