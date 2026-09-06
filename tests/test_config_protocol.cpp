// The version-1 wire contract, exercised with no display, no X server, no GTK
// and no window manager running (D-20's protocol half).
//
// This binary links Catch2 and nothing else. There is no PkgConfig::X11 here
// and no display fixture, because include/ConfigProtocol.h is the one file the
// window manager, wm2-ctl, wm2-config and this test all compile identically --
// any dependency it acquires becomes a dependency of all four. A case that
// needed a display would be evidence the header had grown one.

#include <catch2/catch_test_macros.hpp>

#include "ConfigProtocol.h"

#include <string>
#include <vector>

namespace {

// One representative message per type, carrying exactly the members that type
// is defined to carry. The round-trip table below walks this list, so adding a
// message type to the protocol without adding a row here leaves the new type
// untested -- which the eleven-name spelling guard turns into a failure.
std::vector<ConfigMessage> representativeMessages() {
    std::vector<ConfigMessage> out;

    ConfigMessage hello;
    hello.type = ConfigMessageType::Hello;
    hello.program = "wm2-config";
    hello.protocol = kConfigProtocolVersion;
    out.push_back(hello);

    ConfigMessage helloAck;
    helloAck.type = ConfigMessageType::HelloAck;
    helloAck.program = "wm2-born-again";
    helloAck.protocol = kConfigProtocolVersion;
    out.push_back(helloAck);

    ConfigMessage get;
    get.type = ConfigMessageType::Get;
    get.key = "frame-thickness";
    out.push_back(get);

    ConfigMessage value;
    value.type = ConfigMessageType::Value;
    value.key = "frame-thickness";
    value.value = "7";
    out.push_back(value);

    ConfigMessage set;
    set.type = ConfigMessageType::Set;
    set.key = "tab-background";
    set.value = "#C8CACC";
    out.push_back(set);

    ConfigMessage ack;
    ack.type = ConfigMessageType::Ack;
    ack.key = "tab-background";
    out.push_back(ack);

    ConfigMessage error;
    error.type = ConfigMessageType::Error;
    error.key = "frame-thickness";
    error.reason = "out of range";
    out.push_back(error);

    ConfigMessage reload;
    reload.type = ConfigMessageType::Reload;
    out.push_back(reload);

    ConfigMessage reloaded;
    reloaded.type = ConfigMessageType::Reloaded;
    out.push_back(reloaded);

    ConfigMessage status;
    status.type = ConfigMessageType::Status;
    out.push_back(status);

    ConfigMessage statusReply;
    statusReply.type = ConfigMessageType::StatusReply;
    statusReply.fields.push_back({"wm-version", "0.1.0"});
    statusReply.fields.push_back({"protocol", "1"});
    statusReply.fields.push_back({"uptime-seconds", "412"});
    statusReply.fields.push_back({"screen-geometry", "1280x800"});
    statusReply.fields.push_back({"managed-windows", "3"});
    statusReply.fields.push_back({"hidden-windows", "1"});
    out.push_back(statusReply);

    return out;
}

}  // namespace

// =============================================================================
// The frozen contract itself
// =============================================================================

TEST_CASE("The protocol version is 1 and the line bound is 4096", "[config_protocol]") {
    REQUIRE(kConfigProtocolVersion == 1);
    REQUIRE(kConfigProtocolMaxLine == 4096u);
}

// DISC-01, taken by a human before the first byte of the codec was written:
// exactly these eleven spellings, and no twelfth. From v1.0 a user's shell
// script may depend on every one of them, so a rename is a documented breaking
// change (D-8.5-01), not a refactor. This case is the thing that makes such a
// rename visible in a diff.
TEST_CASE("The eleven frozen message-type spellings are exactly these", "[config_protocol]") {
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Hello)) == "hello");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::HelloAck)) == "hello-ack");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Get)) == "get");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Value)) == "value");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Set)) == "set");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Ack)) == "ack");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Error)) == "error");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Reload)) == "reload");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Reloaded)) == "reloaded");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Status)) == "status");
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::StatusReply)) == "status-reply");

    // Unknown is not a wire spelling. It is the named landing place for a type
    // this version does not speak -- MenuPaint.h's `Foreign` rule applied to
    // the wire.
    REQUIRE(std::string(configMessageTypeName(ConfigMessageType::Unknown)).empty());

    // And the table has exactly eleven rows plus Unknown.
    REQUIRE(representativeMessages().size() == 11u);
}

TEST_CASE("Every frozen spelling maps back to its enumerator", "[config_protocol]") {
    for (const ConfigMessage& m : representativeMessages()) {
        const std::string name = configMessageTypeName(m.type);
        REQUIRE(configMessageTypeFromName(name) == m.type);
    }
    REQUIRE(configMessageTypeFromName("no-such-type") == ConfigMessageType::Unknown);
    REQUIRE(configMessageTypeFromName("") == ConfigMessageType::Unknown);
}

// =============================================================================
// Round trips
// =============================================================================

TEST_CASE("A hello round-trips its program name and protocol number", "[config_protocol]") {
    ConfigMessage sent;
    sent.type = ConfigMessageType::Hello;
    sent.program = "wm2-config";
    sent.protocol = kConfigProtocolVersion;

    const std::string line = configProtocolEncode(sent);
    REQUIRE(line == "{\"type\":\"hello\",\"program\":\"wm2-config\",\"protocol\":1}\n");

    ConfigMessage got;
    REQUIRE(configProtocolDecode(line, got) == ConfigDecodeResult::Ok);
    REQUIRE(got.type == ConfigMessageType::Hello);
    REQUIRE(got.program == "wm2-config");
    REQUIRE(got.protocol == kConfigProtocolVersion);
}

TEST_CASE("Every message type round-trips the fields it carries", "[config_protocol]") {
    for (const ConfigMessage& sent : representativeMessages()) {
        const std::string line = configProtocolEncode(sent);
        INFO("encoded line: " << line);

        ConfigMessage got;
        REQUIRE(configProtocolDecode(line, got) == ConfigDecodeResult::Ok);
        CHECK(got.type == sent.type);
        CHECK(got.program == sent.program);
        CHECK(got.protocol == sent.protocol);
        CHECK(got.key == sent.key);
        CHECK(got.value == sent.value);
        CHECK(got.reason == sent.reason);
        REQUIRE(got.fields.size() == sent.fields.size());
        for (std::size_t i = 0; i < sent.fields.size(); ++i) {
            CHECK(got.fields[i].first == sent.fields[i].first);
            CHECK(got.fields[i].second == sent.fields[i].second);
        }
    }
}

TEST_CASE("Every encoded line ends with exactly one newline and carries no other",
          "[config_protocol]") {
    for (const ConfigMessage& sent : representativeMessages()) {
        const std::string line = configProtocolEncode(sent);
        REQUIRE(line.size() >= 2u);
        REQUIRE(line.back() == '\n');
        REQUIRE(line.find('\n') == line.size() - 1);
    }
}

TEST_CASE("A value containing a quote, a backslash, a newline and a tab survives a round trip",
          "[config_protocol]") {
    ConfigMessage sent;
    sent.type = ConfigMessageType::Set;
    sent.key = "new-window-command";
    sent.value = std::string("say \"hi\" \\ then\nnewline\tand tab");

    const std::string line = configProtocolEncode(sent);
    // The encoder must not have emitted a raw control character: the framing is
    // one message per line, so a literal newline in the payload would split the
    // message in two on the wire.
    REQUIRE(line.find('\n') == line.size() - 1);
    REQUIRE(line.find('\t') == std::string::npos);

    ConfigMessage got;
    REQUIRE(configProtocolDecode(line, got) == ConfigDecodeResult::Ok);
    REQUIRE(got.value == sent.value);
    REQUIRE(got.key == sent.key);
}

TEST_CASE("Every byte below 0x20 survives a round trip", "[config_protocol]") {
    std::string payload;
    for (int c = 1; c < 0x20; ++c) payload += static_cast<char>(c);

    ConfigMessage sent;
    sent.type = ConfigMessageType::Value;
    sent.key = "k";
    sent.value = payload;

    const std::string line = configProtocolEncode(sent);
    REQUIRE(line.find('\n') == line.size() - 1);

    ConfigMessage got;
    REQUIRE(configProtocolDecode(line, got) == ConfigDecodeResult::Ok);
    REQUIRE(got.value == payload);
}

TEST_CASE("A \\uXXXX escape is accepted on input and decodes to UTF-8", "[config_protocol]") {
    ConfigMessage got;
    // U+00E9 LATIN SMALL LETTER E WITH ACUTE, two UTF-8 bytes.
    REQUIRE(configProtocolDecode("{\"type\":\"get\",\"key\":\"caf\\u00e9\"}\n", got) ==
            ConfigDecodeResult::Ok);
    REQUIRE(got.key == std::string("caf\xc3\xa9"));

    // U+1F600, a surrogate pair, four UTF-8 bytes.
    ConfigMessage pair;
    REQUIRE(configProtocolDecode("{\"type\":\"get\",\"key\":\"\\ud83d\\ude00\"}\n", pair) ==
            ConfigDecodeResult::Ok);
    REQUIRE(pair.key == std::string("\xf0\x9f\x98\x80"));

    // A lone high surrogate is not a code point; it is malformed input, not a
    // silently-substituted replacement character.
    ConfigMessage lone;
    REQUIRE(configProtocolDecode("{\"type\":\"get\",\"key\":\"\\ud83d\"}\n", lone) ==
            ConfigDecodeResult::Malformed);
}

TEST_CASE("The encoder never emits a \\uXXXX escape for printable ASCII", "[config_protocol]") {
    ConfigMessage sent;
    sent.type = ConfigMessageType::Value;
    sent.key = "k";
    sent.value = "plain ASCII / with a solidus";

    const std::string line = configProtocolEncode(sent);
    REQUIRE(line.find("\\u") == std::string::npos);
    REQUIRE(line.find("\\/") == std::string::npos);
}

// =============================================================================
// The length bound (T-9-05)
// =============================================================================

TEST_CASE("A line at exactly the bound decodes and one byte over is rejected as too long",
          "[config_protocol]") {
    ConfigMessage sent;
    sent.type = ConfigMessageType::Set;
    sent.key = "k";
    sent.value = "";

    const std::size_t overhead = configProtocolEncode(sent).size();
    REQUIRE(overhead < kConfigProtocolMaxLine);

    sent.value = std::string(kConfigProtocolMaxLine - overhead, 'a');
    const std::string exact = configProtocolEncode(sent);
    REQUIRE(exact.size() == kConfigProtocolMaxLine);

    ConfigMessage got;
    REQUIRE(configProtocolDecode(exact, got) == ConfigDecodeResult::Ok);
    REQUIRE(got.value.size() == kConfigProtocolMaxLine - overhead);

    sent.value += 'a';
    const std::string over = configProtocolEncode(sent);
    REQUIRE(over.size() == kConfigProtocolMaxLine + 1);

    ConfigMessage rejected;
    // The length result SPECIFICALLY, not the malformed one. The two outcomes
    // are different facts about the input and a caller may want to log them
    // differently, so collapsing them would be a real loss.
    REQUIRE(configProtocolDecode(over, rejected) == ConfigDecodeResult::TooLong);
    REQUIRE(configProtocolDecode(over, rejected) != ConfigDecodeResult::Malformed);
}

TEST_CASE("The length bound is checked before any scan of the content", "[config_protocol]") {
    // Content that is unambiguously malformed -- it is not even an object --
    // AND over the bound. If the bound were checked after parsing, this would
    // come back Malformed. TooLong is the observable proof of the ordering.
    const std::string garbage(kConfigProtocolMaxLine + 1, 'z');
    ConfigMessage got;
    REQUIRE(configProtocolDecode(garbage, got) == ConfigDecodeResult::TooLong);

    // Vastly over the bound: still a constant-time rejection, and still not a
    // parse. The decoder never grows a buffer to accommodate the caller.
    const std::string huge(kConfigProtocolMaxLine * 64, 'z');
    REQUIRE(configProtocolDecode(huge, got) == ConfigDecodeResult::TooLong);
}

// =============================================================================
// Every rejection has a name (the MenuPaint.h `Foreign` rule)
// =============================================================================

TEST_CASE("A line that is not an object is rejected as malformed", "[config_protocol]") {
    ConfigMessage got;
    CHECK(configProtocolDecode("", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("hello\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("[\"type\",\"hello\"]\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("\"hello\"\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("42\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"hello\"\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"hello\"} trailing\n", got) ==
          ConfigDecodeResult::Malformed);
}

TEST_CASE("An object with no type member is rejected as malformed", "[config_protocol]") {
    ConfigMessage got;
    REQUIRE(configProtocolDecode("{}\n", got) == ConfigDecodeResult::Malformed);
    REQUIRE(configProtocolDecode("{\"key\":\"frame-thickness\"}\n", got) ==
            ConfigDecodeResult::Malformed);
    // Never a silent success with an unset type.
    REQUIRE(got.type == ConfigMessageType::Unknown);
}

TEST_CASE("An object whose type this version does not speak resolves to UnknownType",
          "[config_protocol]") {
    ConfigMessage got;
    REQUIRE(configProtocolDecode("{\"type\":\"list-windows\",\"key\":\"k\"}\n", got) ==
            ConfigDecodeResult::UnknownType);
    REQUIRE(got.type == ConfigMessageType::Unknown);

    // Distinguishable from malformed: that distinction is what lets a later
    // protocol version add a message type without every v1 peer reporting a
    // parse error it cannot act on.
    REQUIRE(configProtocolDecode("{\"type\":\"list-windows\"}\n", got) !=
            ConfigDecodeResult::Malformed);
    REQUIRE(configProtocolDecode("{\"type\":\"list-windows\"}\n", got) != ConfigDecodeResult::Ok);
}

TEST_CASE("A type member that is not a string is malformed, not unknown", "[config_protocol]") {
    ConfigMessage got;
    CHECK(configProtocolDecode("{\"type\":1}\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":null}\n", got) == ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":[\"hello\"]}\n", got) == ConfigDecodeResult::Malformed);
}

// T-9-06: there is no nesting depth to exhaust, because the grammar is one
// level deep and the parser has no recursion. A nested object is rejected by
// the grammar rather than descended into.
TEST_CASE("Nested objects and arrays are rejected as malformed rather than descended into",
          "[config_protocol]") {
    ConfigMessage got;
    CHECK(configProtocolDecode("{\"type\":\"set\",\"key\":{\"a\":\"b\"}}\n", got) ==
          ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"status-reply\",\"fields\":[[\"a\",\"b\"]]}\n", got) ==
          ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"status-reply\",\"fields\":[{\"a\":\"b\"}]}\n", got) ==
          ConfigDecodeResult::Malformed);

    // A deeply nested payload, well inside the length bound, must come back as
    // a verdict and not as a stack overflow.
    std::string deep = "{\"type\":\"set\",\"key\":";
    const int depth = 500;
    for (int i = 0; i < depth; ++i) deep += "{\"a\":";
    deep += "\"b\"";
    for (int i = 0; i < depth; ++i) deep += "}";
    deep += "}\n";
    REQUIRE(deep.size() <= kConfigProtocolMaxLine);
    CHECK(configProtocolDecode(deep, got) == ConfigDecodeResult::Malformed);
}

TEST_CASE("A member name this version does not know is malformed", "[config_protocol]") {
    // Strict by choice (DISC-01c): a v1 peer that quietly ignored an unknown
    // member would accept a v2 `set` while dropping the qualifier that changed
    // its meaning. The protocol number in the hello is how a peer learns what
    // it may send; a new member rides a version bump, not silence.
    ConfigMessage got;
    REQUIRE(configProtocolDecode("{\"type\":\"set\",\"key\":\"k\",\"scope\":\"all\"}\n", got) ==
            ConfigDecodeResult::Malformed);
}

TEST_CASE("A status-reply field list with an odd number of entries is malformed",
          "[config_protocol]") {
    ConfigMessage got;
    REQUIRE(configProtocolDecode("{\"type\":\"status-reply\",\"fields\":[\"a\",\"b\",\"c\"]}\n",
                                 got) == ConfigDecodeResult::Malformed);
    REQUIRE(configProtocolDecode("{\"type\":\"status-reply\",\"fields\":[\"a\",\"b\"]}\n", got) ==
            ConfigDecodeResult::Ok);
    REQUIRE(got.fields.size() == 1u);
    REQUIRE(got.fields[0].first == "a");
    REQUIRE(got.fields[0].second == "b");
}

TEST_CASE("A protocol member that is not a bare unsigned integer is malformed",
          "[config_protocol]") {
    ConfigMessage got;
    CHECK(configProtocolDecode("{\"type\":\"hello\",\"protocol\":\"1\"}\n", got) ==
          ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"hello\",\"protocol\":-1}\n", got) ==
          ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"hello\",\"protocol\":1.5}\n", got) ==
          ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"hello\",\"protocol\":99999999999999}\n", got) ==
          ConfigDecodeResult::Malformed);
    CHECK(configProtocolDecode("{\"type\":\"hello\",\"protocol\":2}\n", got) ==
          ConfigDecodeResult::Ok);
    CHECK(got.protocol == 2);
}

// =============================================================================
// Framing
// =============================================================================

TEST_CASE("Decoding never reads past the terminating newline of the line it was handed",
          "[config_protocol]") {
    // Two whole messages in one buffer. The decoder is defined over ONE line,
    // so the second message must be invisible to it -- otherwise a caller that
    // read two frames in one recv() would silently act on the wrong one.
    const std::string two =
        "{\"type\":\"reload\"}\n{\"type\":\"set\",\"key\":\"frame-thickness\",\"value\":\"9\"}\n";

    ConfigMessage got;
    REQUIRE(configProtocolDecode(two, got) == ConfigDecodeResult::Ok);
    REQUIRE(got.type == ConfigMessageType::Reload);
    REQUIRE(got.key.empty());
    REQUIRE(got.value.empty());

    // Garbage after the newline cannot turn a good line bad either.
    ConfigMessage second;
    REQUIRE(configProtocolDecode("{\"type\":\"status\"}\n}}}not json at all", second) ==
            ConfigDecodeResult::Ok);
    REQUIRE(second.type == ConfigMessageType::Status);
}

TEST_CASE("A line with no terminating newline still decodes", "[config_protocol]") {
    // A caller that stripped the framing byte before handing the line over is
    // not thereby handing over a malformed message.
    ConfigMessage got;
    REQUIRE(configProtocolDecode("{\"type\":\"status\"}", got) == ConfigDecodeResult::Ok);
    REQUIRE(got.type == ConfigMessageType::Status);
}

TEST_CASE("Encoding an Unknown message yields an empty string", "[config_protocol]") {
    // Unknown is a decode verdict, not something a sender may transmit. The
    // empty string is never a valid line (a valid line always ends in '\n'),
    // so a caller that ignores this cannot accidentally put bytes on the wire.
    ConfigMessage m;
    m.type = ConfigMessageType::Unknown;
    m.key = "frame-thickness";
    REQUIRE(configProtocolEncode(m).empty());
}

// =============================================================================
// CGUI-02, concurrency half (edge probe, authored as a plain truth)
//
// Decoding is a pure function of one line and holds no state between calls, so
// two decoders running concurrently over different lines cannot affect each
// other, and an interrupted decode leaves no state to corrupt. What a test can
// actually observe of that: the same input always yields the same output, and
// interleaving two different inputs changes neither result.
// =============================================================================

TEST_CASE("Decoding is a pure function of the line it was handed", "[config_protocol]") {
    const std::string a = "{\"type\":\"set\",\"key\":\"frame-thickness\",\"value\":\"9\"}\n";
    const std::string b = "{\"type\":\"get\",\"key\":\"tab-background\"}\n";

    ConfigMessage first;
    ConfigMessage interleaved;
    ConfigMessage again;

    REQUIRE(configProtocolDecode(a, first) == ConfigDecodeResult::Ok);
    REQUIRE(configProtocolDecode(b, interleaved) == ConfigDecodeResult::Ok);
    REQUIRE(configProtocolDecode(a, again) == ConfigDecodeResult::Ok);

    REQUIRE(again.type == first.type);
    REQUIRE(again.key == first.key);
    REQUIRE(again.value == first.value);
    REQUIRE(interleaved.type == ConfigMessageType::Get);
    REQUIRE(interleaved.value.empty());
}

TEST_CASE("A failed decode does not leave stale members from a previous decode",
          "[config_protocol]") {
    ConfigMessage reused;
    REQUIRE(configProtocolDecode("{\"type\":\"set\",\"key\":\"k\",\"value\":\"v\"}\n", reused) ==
            ConfigDecodeResult::Ok);
    REQUIRE(reused.value == "v");

    REQUIRE(configProtocolDecode("not an object", reused) == ConfigDecodeResult::Malformed);
    REQUIRE(reused.type == ConfigMessageType::Unknown);
    REQUIRE(reused.key.empty());
    REQUIRE(reused.value.empty());
    REQUIRE(reused.fields.empty());
}


// -----------------------------------------------------------------------------
// Telling D-08's broadcast from the reply a request asked for (WR-12, CR-02)
// -----------------------------------------------------------------------------

TEST_CASE("a reloaded that no request asked for is an unsolicited notice",
          "[config_protocol][notice]")
{
    // Every client that completes the handshake is in the window manager's
    // broadcast set, so a reload triggered by anything else puts a `reloaded`
    // on the connection ahead of whatever reply is outstanding. Reading it as
    // that reply is how `wm2-ctl set` exits 1 -- "the window manager refused
    // the request" -- after a set that succeeded.
    for (ConfigMessageType expected : { ConfigMessageType::Ack,
                                        ConfigMessageType::Value,
                                        ConfigMessageType::StatusReply,
                                        ConfigMessageType::HelloAck }) {
        CHECK(configProtocolIsUnsolicitedNotice(ConfigMessageType::Reloaded,
                                                expected));
    }

    // The reply to a reload THIS client asked for is not a notice. Sharing one
    // type between the two is what let the contract stay at eleven, and what
    // the request is is the only thing that tells them apart.
    CHECK_FALSE(configProtocolIsUnsolicitedNotice(ConfigMessageType::Reloaded,
                                                  ConfigMessageType::Reloaded));

    // And nothing else is ever a notice -- an `error` in particular, which is
    // how a refusal comes back and must always reach the request that caused
    // it.
    for (ConfigMessageType arrived : { ConfigMessageType::Ack,
                                       ConfigMessageType::Value,
                                       ConfigMessageType::Error,
                                       ConfigMessageType::StatusReply,
                                       ConfigMessageType::HelloAck,
                                       ConfigMessageType::Unknown }) {
        CHECK_FALSE(configProtocolIsUnsolicitedNotice(arrived,
                                                      ConfigMessageType::Ack));
        CHECK_FALSE(configProtocolIsUnsolicitedNotice(arrived,
                                                      ConfigMessageType::Reloaded));
    }
}
