#pragma once
// ReplayJ2534 — Scenario JSON configuration store
// Loads scenario.json defining device metadata, IOCTL rules, targets with
// reply/periodic rules, and state machine transitions.

#include "J2534Defs.h"
#include <vector>
#include <string>

// ── Forward declaration of minimal JSON value ───────────────────────────────
namespace rjson {
struct Value;
}

// ── Configuration data structures ───────────────────────────────────────────

enum IoctlScope { SCOPE_ANY = 0, SCOPE_DEVICE, SCOPE_CHANNEL };

struct HexBytes {
    unsigned char data[512];
    int len;
    bool isNull;
};

struct IoctlRule {
    unsigned long ioctlId;
    long returnCode;
    IoctlScope scope;
    bool hasOutput;
    bool outputAuto;
    HexBytes outputBytes;
    bool consumeInput;
};

struct MsgSpec {
    unsigned long protocolId;
    unsigned long rxStatus;
    unsigned long txFlags;
    HexBytes data;
};

enum MatchMode { MATCH_PREFIX = 0, MATCH_EXACT };

struct ReplyRule {
    HexBytes matchData;
    MatchMode mode;
    MsgSpec response;
    unsigned long delayMs;
};

struct PeriodicRule {
    unsigned long intervalMs;
    MsgSpec msg;
    // startOn/stopOn: currently only "connect"/"disconnect" supported
    bool startOnConnect;
    bool stopOnDisconnect;
};

struct TargetMatch {
    unsigned long protocolId;
    unsigned long flags;
    unsigned long baud;
    bool hasProtocolId;
    bool hasFlags;
    bool hasBaud;
};

struct Target {
    std::string name;
    TargetMatch match;
    unsigned long preferredChannelId;
    std::vector<ReplyRule> replies;
    std::vector<PeriodicRule> periodic;
};

struct DeviceConfig {
    char firmwareVersion[80];
    char dllVersion[80];
    char apiVersion[80];
    unsigned long vbatt_mV;
};

struct StateTransition {
    std::string event;
    std::string from;
    std::string to;
};

struct StateMachineConfig {
    std::string initial;
    std::vector<StateTransition> transitions;
};

// ── ConfigStore ─────────────────────────────────────────────────────────────

class ConfigStore {
public:
    ConfigStore();
    ~ConfigStore();

    bool load(const char *filePath);
    void clear();
    bool isLoaded() const { return loaded_; }

    const DeviceConfig& device() const { return device_; }
    const std::vector<IoctlRule>& ioctls() const { return ioctls_; }
    const std::vector<Target>& targets() const { return targets_; }
    const StateMachineConfig& stateMachine() const { return stateMachine_; }

    const IoctlRule* findIoctl(unsigned long ioctlId) const;
    const Target* findTarget(unsigned long protocolId, unsigned long flags,
                             unsigned long baud) const;

    const char* lastError() const { return lastError_; }

    // Public helpers (used by tests)
    static unsigned long parseHexBytes(const char *str, unsigned char *buf, int bufSize);
    static long lookupReturnCode(const char *name);
    static unsigned long lookupSymbolic(const char *name);
    static unsigned long parseIoctlKey(const char *key);

private:
    bool loaded_;
    char lastError_[512];

    DeviceConfig device_;
    std::vector<IoctlRule> ioctls_;
    std::vector<Target> targets_;
    StateMachineConfig stateMachine_;

    bool parseScenario(const rjson::Value &root);
    bool parseDevice(const rjson::Value &v);
    bool parseIoctls(const rjson::Value &v);
    bool parseTargets(const rjson::Value &v);
    bool parseStates(const rjson::Value &v);
    bool parseMsgSpec(const rjson::Value &v, MsgSpec &msg);
    bool parseReply(const rjson::Value &v, ReplyRule &rule);
    bool parsePeriodic(const rjson::Value &v, PeriodicRule &rule);
    bool parseHexBytes(const rjson::Value &v, HexBytes &hb);
    IoctlScope parseScope(const char *s);
    MatchMode parseMatchMode(const char *s);
};
