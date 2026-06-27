#include "ConfigStore.h"
#include "Logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ═══════════════════════════════════════════════════════════════════════════
// Minimal JSON parser (recursive descent)
// ═══════════════════════════════════════════════════════════════════════════

namespace rjson {

enum Type { T_NULL, T_BOOL, T_NUM, T_STR, T_ARR, T_OBJ };

struct Value {
    Type type;
    bool bval;
    double num;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value> > obj;

    Value() : type(T_NULL), bval(false), num(0) {}

    const Value* find(const char *key) const {
        for (size_t i = 0; i < obj.size(); i++)
            if (obj[i].first == key) return &obj[i].second;
        return NULL;
    }
    const char* asStr(const char *def = "") const {
        return type == T_STR ? str.c_str() : def;
    }
    double asNum(double def = 0) const {
        return type == T_NUM ? num : def;
    }
    bool asBool(bool def = false) const {
        return type == T_BOOL ? bval : def;
    }
    // Safe accessors: find key, return default if missing
    const char* getCstr(const char *key, const char *def = "") const {
        const Value *v = find(key);
        return v ? v->asStr(def) : def;
    }
    double getNum(const char *key, double def = 0) const {
        const Value *v = find(key);
        return v ? v->asNum(def) : def;
    }
    bool getBool(const char *key, bool def = false) const {
        const Value *v = find(key);
        return v ? v->asBool(def) : def;
    }
    const Value* get(const char *key) const { return find(key); }
};

struct Parser {
    const char *p;
    const char *end;
    const char *err;

    Parser(const char *s, int len) : p(s), end(s + len), err(NULL) {}

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    }

    bool parse(Value &out) {
        skipWs();
        if (p >= end) { err = "unexpected end"; return false; }
        char c = *p;
        if (c == '{') return parseObj(out);
        if (c == '[') return parseArr(out);
        if (c == '"') return parseStr(out);
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        return parseNum(out);
    }

    bool parseStr(Value &out) {
        p++; // skip opening quote
        out.type = T_STR;
        out.str.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                char esc = *p++;
                switch (esc) {
                case '"': out.str += '"'; break;
                case '\\': out.str += '\\'; break;
                case '/': out.str += '/'; break;
                case 'n': out.str += '\n'; break;
                case 'r': out.str += '\r'; break;
                case 't': out.str += '\t'; break;
                case 'u': {
                    if (p + 4 <= end) {
                        unsigned long cp = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = p[i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= h - '0';
                            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        }
                        p += 4;
                        if (cp < 0x80) out.str += (char)cp;
                        else if (cp < 0x800) {
                            out.str += (char)(0xC0 | (cp >> 6));
                            out.str += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out.str += (char)(0xE0 | (cp >> 12));
                            out.str += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out.str += (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default: out.str += esc; break;
                }
            } else {
                out.str += *p++;
            }
        }
        if (p >= end) { err = "unterminated string"; return false; }
        p++; // skip closing quote
        return true;
    }

    bool parseNum(Value &out) {
        out.type = T_NUM;
        char *endp;
        out.num = strtod(p, &endp);
        if (endp == p) { err = "invalid number"; return false; }
        p = endp;
        return true;
    }

    bool parseBool(Value &out) {
        if (end - p >= 4 && memcmp(p, "true", 4) == 0) {
            out.type = T_BOOL; out.bval = true; p += 4; return true;
        }
        if (end - p >= 5 && memcmp(p, "false", 5) == 0) {
            out.type = T_BOOL; out.bval = false; p += 5; return true;
        }
        err = "invalid literal"; return false;
    }

    bool parseNull(Value &out) {
        if (end - p >= 4 && memcmp(p, "null", 4) == 0) {
            out.type = T_NULL; p += 4; return true;
        }
        err = "invalid literal"; return false;
    }

    bool parseArr(Value &out) {
        p++; // skip [
        out.type = T_ARR;
        skipWs();
        if (p < end && *p == ']') { p++; return true; }
        for (;;) {
            skipWs();
            Value elem;
            if (!parse(elem)) return false;
            out.arr.push_back(elem);
            skipWs();
            if (p >= end) { err = "unterminated array"; return false; }
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return true; }
            err = "expected , or ]"; return false;
        }
    }

    bool parseObj(Value &out) {
        p++; // skip {
        out.type = T_OBJ;
        skipWs();
        if (p < end && *p == '}') { p++; return true; }
        for (;;) {
            skipWs();
            if (p >= end || *p != '"') { err = "expected key string"; return false; }
            Value key;
            if (!parseStr(key)) return false;
            skipWs();
            if (p >= end || *p != ':') { err = "expected :"; return false; }
            p++;
            Value val;
            if (!parse(val)) return false;
            out.obj.push_back(std::make_pair(key.str, val));
            skipWs();
            if (p >= end) { err = "unterminated object"; return false; }
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; return true; }
            err = "expected , or }"; return false;
        }
    }
};

} // namespace rjson

// ═══════════════════════════════════════════════════════════════════════════
// Lookup tables (reused from LogParser — proven mappings)
// ═══════════════════════════════════════════════════════════════════════════

long ConfigStore::lookupReturnCode(const char *name) {
    struct { const char *n; long v; } t[] = {
        {"STATUS_NOERROR", STATUS_NOERROR},
        {"ERR_NOT_SUPPORTED", ERR_NOT_SUPPORTED},
        {"ERR_INVALID_CHANNEL_ID", ERR_INVALID_CHANNEL_ID},
        {"ERR_INVALID_PROTOCOL_ID", ERR_INVALID_PROTOCOL_ID},
        {"ERR_NULL_PARAMETER", ERR_NULL_PARAMETER},
        {"ERR_INVALID_IOCTL_VALUE", ERR_INVALID_IOCTL_VALUE},
        {"ERR_INVALID_FLAGS", ERR_INVALID_FLAGS},
        {"ERR_FAILED", ERR_FAILED},
        {"ERR_DEVICE_NOT_CONNECTED", ERR_DEVICE_NOT_CONNECTED},
        {"ERR_TIMEOUT", ERR_TIMEOUT},
        {"ERR_INVALID_MSG", ERR_INVALID_MSG},
        {"ERR_INVALID_TIME_INTERVAL", ERR_INVALID_TIME_INTERVAL},
        {"ERR_EXCEEDED_LIMIT", ERR_EXCEEDED_LIMIT},
        {"ERR_INVALID_MSG_ID", ERR_INVALID_MSG_ID},
        {"ERR_DEVICE_IN_USE", ERR_DEVICE_IN_USE},
        {"ERR_INVALID_IOCTL_ID", ERR_INVALID_IOCTL_ID},
        {"ERR_BUFFER_EMPTY", ERR_BUFFER_EMPTY},
        {"ERR_BUFFER_FULL", ERR_BUFFER_FULL},
        {"ERR_BUFFER_OVERFLOW", ERR_BUFFER_OVERFLOW},
        {"ERR_PIN_INVALID", ERR_PIN_INVALID},
        {"ERR_CHANNEL_IN_USE", ERR_CHANNEL_IN_USE},
        {"ERR_MSG_PROTOCOL_ID", ERR_MSG_PROTOCOL_ID},
        {"ERR_INVALID_FILTER_ID", ERR_INVALID_FILTER_ID},
        {"ERR_NO_FLOW_CONTROL", ERR_NO_FLOW_CONTROL},
        {"ERR_NOT_UNIQUE", ERR_NOT_UNIQUE},
        {"ERR_INVALID_BAUDRATE", ERR_INVALID_BAUDRATE},
        {"ERR_INVALID_DEVICE_ID", ERR_INVALID_DEVICE_ID},
    };
    if (!name) return -1;
    for (int i = 0; i < (int)(sizeof(t)/sizeof(t[0])); i++)
        if (strcmp(t[i].n, name) == 0) return t[i].v;
    return -1;
}

unsigned long ConfigStore::lookupSymbolic(const char *name) {
    struct { const char *n; unsigned long v; } t[] = {
        {"J1850VPW", J2534_J1850VPW}, {"J1850PWM", J2534_J1850PWM},
        {"ISO9141", J2534_ISO9141}, {"ISO14230", J2534_ISO14230},
        {"CAN", J2534_CAN}, {"ISO15765", J2534_ISO15765},
        {"SCI_A_ENGINE", J2534_SCI_A_ENGINE}, {"SCI_A_TRANS", J2534_SCI_A_TRANS},
        {"SCI_B_ENGINE", J2534_SCI_B_ENGINE}, {"SCI_B_TRANS", J2534_SCI_B_TRANS},
        {"CAN_29BIT_ID", CAN_29BIT_ID}, {"ISO9141_NO_CHECKSUM", ISO9141_NO_CHECKSUM},
        {"CAN_ID_BOTH", CAN_ID_BOTH}, {"ISO9141_K_LINE_ONLY", ISO9141_K_LINE_ONLY},
        {"PASS_FILTER", PASS_FILTER}, {"BLOCK_FILTER", BLOCK_FILTER},
        {"FLOW_CONTROL_FILTER", FLOW_CONTROL_FILTER},
        {"ISO15765_FRAMEPAD", ISO15765_FRAME_PAD},
        {"ISO15765_FRAME_PAD", ISO15765_FRAME_PAD},
        {"GET_CONFIG", GET_CONFIG}, {"SET_CONFIG", SET_CONFIG},
        {"READ_VBATT", READ_VBATT}, {"FIVE_BAUD_INIT", FIVE_BAUD_INIT},
        {"FAST_INIT", FAST_INIT}, {"CLEAR_TX_BUFFER", CLEAR_TX_BUFFER},
        {"CLEAR_RX_BUFFER", CLEAR_RX_BUFFER},
        {"CLEAR_PERIODIC_MSGS", CLEAR_PERIODIC_MSGS},
        {"CLEAR_MSG_FILTERS", CLEAR_MSG_FILTERS},
        {"CLEAR_FUNCT_MSG_LOOKUP_TABLE", CLEAR_FUNCT_MSG_LOOKUP_TABLE},
        {"ADD_TO_FUNCT_MSG_LOOKUP_TABLE", ADD_TO_FUNCT_MSG_LOOKUP_TABLE},
        {"DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE", DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE},
        {"READ_PROG_VOLTAGE", READ_PROG_VOLTAGE},
    };
    if (!name) return 0xFFFFFFFF;
    for (int i = 0; i < (int)(sizeof(t)/sizeof(t[0])); i++)
        if (strcmp(t[i].n, name) == 0) return t[i].v;
    return 0xFFFFFFFF;
}

unsigned long ConfigStore::parseIoctlKey(const char *key) {
    if (!key) return 0xFFFFFFFF;
    // Try symbolic first
    unsigned long sym = lookupSymbolic(key);
    if (sym != 0xFFFFFFFF) return sym;
    // Try hex (0x...) or decimal
    if (key[0] == '0' && (key[1] == 'x' || key[1] == 'X')) {
        return strtoul(key + 2, NULL, 16);
    }
    // Try decimal
    if (key[0] >= '0' && key[0] <= '9') {
        return strtoul(key, NULL, 10);
    }
    return 0xFFFFFFFF;
}

// ═══════════════════════════════════════════════════════════════════════════
// Hex byte parsing — ignores non-hex separators (-, space, :, etc.)
// ═══════════════════════════════════════════════════════════════════════════

unsigned long ConfigStore::parseHexBytes(const char *str, unsigned char *buf, int bufSize) {
    if (!str || !buf || bufSize <= 0) return 0;
    int len = 0;
    int nibble = -1;
    for (int i = 0; str[i] && len < bufSize; i++) {
        char c = str[i];
        int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else { nibble = -1; continue; } // separator
        if (nibble < 0) {
            nibble = val;
        } else {
            buf[len++] = (unsigned char)((nibble << 4) | val);
            nibble = -1;
        }
    }
    if (nibble >= 0 && len < bufSize) {
        buf[len++] = (unsigned char)nibble; // dangling nibble
    }
    return len;
}

// ═══════════════════════════════════════════════════════════════════════════
// ConfigStore
// ═══════════════════════════════════════════════════════════════════════════

ConfigStore::ConfigStore() : loaded_(false) {
    memset(&device_, 0, sizeof(device_));
    lastError_[0] = '\0';
}

ConfigStore::~ConfigStore() { clear(); }

void ConfigStore::clear() {
    loaded_ = false;
    ioctls_.clear();
    targets_.clear();
    stateMachine_ = StateMachineConfig();
    memset(&device_, 0, sizeof(device_));
}

bool ConfigStore::load(const char *filePath) {
    clear();
    if (!filePath || !filePath[0]) {
        snprintf(lastError_, sizeof(lastError_), "empty config path");
        return false;
    }
    FILE *fp = fopen(filePath, "rb");
    if (!fp) {
        snprintf(lastError_, sizeof(lastError_), "cannot open: %s", filePath);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) {
        fclose(fp);
        snprintf(lastError_, sizeof(lastError_), "invalid size %ld", sz);
        return false;
    }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return false; }
    long rd = (long)fread(buf, 1, sz, fp);
    fclose(fp);
    buf[rd] = '\0';

    rjson::Parser parser(buf, (int)rd);
    rjson::Value root;
    if (!parser.parse(root)) {
        snprintf(lastError_, sizeof(lastError_), "JSON parse error: %s", parser.err ? parser.err : "?");
        free(buf);
        return false;
    }
    free(buf);

    if (!parseScenario(root)) {
        return false;
    }

    loaded_ = true;
    g_logger.verbose("ConfigStore: loaded %d ioctls, %d targets from '%s'",
                     (int)ioctls_.size(), (int)targets_.size(), filePath);
    return true;
}

bool ConfigStore::parseScenario(const rjson::Value &root) {
    if (root.type != rjson::T_OBJ) {
        snprintf(lastError_, sizeof(lastError_), "root is not object");
        return false;
    }
    if (const rjson::Value *v = root.find("device"))
        if (!parseDevice(*v)) return false;
    if (const rjson::Value *v = root.find("ioctls"))
        if (!parseIoctls(*v)) return false;
    if (const rjson::Value *v = root.find("targets"))
        if (!parseTargets(*v)) return false;
    if (const rjson::Value *v = root.find("states"))
        if (!parseStates(*v)) return false;
    return true;
}

bool ConfigStore::parseDevice(const rjson::Value &v) {
    if (v.type != rjson::T_OBJ) return true;
    strncpy(device_.firmwareVersion, v.getCstr("firmwareVersion", "3.37.0"), 79);
    strncpy(device_.dllVersion, v.getCstr("dllVersion", "1.0.0"), 79);
    strncpy(device_.apiVersion, v.getCstr("apiVersion", "04.04"), 79);
    device_.firmwareVersion[79] = device_.dllVersion[79] = device_.apiVersion[79] = '\0';
    const rjson::Value *vb = v.find("vbatt_mV");
    device_.vbatt_mV = vb ? (unsigned long)vb->asNum(12000) : 12000;
    return true;
}

bool ConfigStore::parseIoctls(const rjson::Value &v) {
    if (v.type != rjson::T_OBJ) return true;
    for (size_t i = 0; i < v.obj.size(); i++) {
        const std::string &key = v.obj[i].first;
        const rjson::Value &rule = v.obj[i].second;
        if (rule.type != rjson::T_OBJ) continue;

        IoctlRule r;
        memset(&r, 0, sizeof(r));
        r.ioctlId = parseIoctlKey(key.c_str());
        if (r.ioctlId == 0xFFFFFFFF) {
            snprintf(lastError_, sizeof(lastError_), "unknown ioctl key: %s", key.c_str());
            return false;
        }
        const char *retName = rule.getCstr("return", "STATUS_NOERROR");
        r.returnCode = lookupReturnCode(retName);
        if (r.returnCode < 0) {
            snprintf(lastError_, sizeof(lastError_), "unknown return code: %s", retName);
            return false;
        }
        r.scope = parseScope(rule.getCstr("scope", "any"));
        r.consumeInput = rule.getBool("consumeInput", false);

        if (const rjson::Value *out = rule.find("output")) {
            if (out->type == rjson::T_STR && strcmp(out->asStr(), "auto") == 0) {
                r.hasOutput = true;
                r.outputAuto = true;
            } else if (out->type == rjson::T_STR) {
                r.hasOutput = true;
                r.outputAuto = false;
                r.outputBytes.isNull = false;
                r.outputBytes.len = (int)parseHexBytes(out->asStr(),
                    r.outputBytes.data, sizeof(r.outputBytes.data));
            } else if (out->type == rjson::T_NUM) {
                r.hasOutput = true;
                r.outputAuto = false;
                unsigned long val = (unsigned long)out->num;
                r.outputBytes.data[0] = (unsigned char)(val & 0xFF);
                r.outputBytes.data[1] = (unsigned char)((val >> 8) & 0xFF);
                r.outputBytes.data[2] = (unsigned char)((val >> 16) & 0xFF);
                r.outputBytes.data[3] = (unsigned char)((val >> 24) & 0xFF);
                r.outputBytes.len = 4;
                r.outputBytes.isNull = false;
            } else {
                r.hasOutput = false;
                r.outputBytes.isNull = true;
            }
        } else {
            r.hasOutput = false;
            r.outputBytes.isNull = true;
        }
        ioctls_.push_back(r);
    }
    return true;
}

IoctlScope ConfigStore::parseScope(const char *s) {
    if (!s) return SCOPE_ANY;
    if (strcmp(s, "device") == 0) return SCOPE_DEVICE;
    if (strcmp(s, "channel") == 0) return SCOPE_CHANNEL;
    return SCOPE_ANY;
}

MatchMode ConfigStore::parseMatchMode(const char *s) {
    if (s && strcmp(s, "exact") == 0) return MATCH_EXACT;
    return MATCH_PREFIX;
}

bool ConfigStore::parseHexBytes(const rjson::Value &v, HexBytes &hb) {
    memset(&hb, 0, sizeof(hb));
    if (v.type == rjson::T_NULL || v.type == rjson::T_OBJ) {
        hb.isNull = true;
        return true;
    }
    if (v.type == rjson::T_STR) {
        hb.isNull = false;
        hb.len = (int)parseHexBytes(v.asStr(), hb.data, sizeof(hb.data));
        return true;
    }
    hb.isNull = true;
    return true;
}

bool ConfigStore::parseMsgSpec(const rjson::Value &v, MsgSpec &msg) {
    memset(&msg, 0, sizeof(msg));
    msg.protocolId = J2534_ISO15765;
    const rjson::Value *p = v.find("protocolId");
    if (p) {
        if (p->type == rjson::T_STR)
            msg.protocolId = lookupSymbolic(p->asStr());
        else
            msg.protocolId = (unsigned long)p->asNum(J2534_ISO15765);
    }
    msg.rxStatus = (unsigned long)v.getNum("rxStatus", 0);
    msg.txFlags = (unsigned long)v.getNum("txFlags", 0);
    if (const rjson::Value *tf = v.find("txFlags"))
        if (tf->type == rjson::T_STR)
            msg.txFlags = lookupSymbolic(tf->asStr(""));

    const rjson::Value *d = v.find("data");
    if (d) parseHexBytes(*d, msg.data);
    else { msg.data.isNull = true; }
    return true;
}

bool ConfigStore::parseReply(const rjson::Value &v, ReplyRule &rule) {
    memset(&rule, 0, sizeof(rule));
    if (const rjson::Value *m = v.find("match")) {
        const char *md = m->getCstr("mode", "prefix");
        rule.mode = parseMatchMode(md);
        const rjson::Value *d = m->find("data");
        if (d) parseHexBytes(*d, rule.matchData);
    }
    if (const rjson::Value *r = v.find("response")) {
        parseMsgSpec(*r, rule.response);
        rule.delayMs = (unsigned long)r->getNum("delayMs", 0);
    }
    return true;
}

bool ConfigStore::parsePeriodic(const rjson::Value &v, PeriodicRule &rule) {
    memset(&rule, 0, sizeof(rule));
    rule.intervalMs = (unsigned long)v.getNum("intervalMs", 1000);
    parseMsgSpec(*v.find("msg"), rule.msg);
    const char *startOn = v.getCstr("startOn", "connect");
    rule.startOnConnect = (strcmp(startOn, "connect") == 0);
    const char *stopOn = v.getCstr("stopOn", "disconnect");
    rule.stopOnDisconnect = (strcmp(stopOn, "disconnect") == 0);
    return true;
}

bool ConfigStore::parseTargets(const rjson::Value &v) {
    if (v.type != rjson::T_ARR) return true;
    for (size_t i = 0; i < v.arr.size(); i++) {
        const rjson::Value &t = v.arr[i];
        if (t.type != rjson::T_OBJ) continue;
        Target tgt;
        tgt.name = t.getCstr("name", "unnamed");
        tgt.preferredChannelId = (unsigned long)t.getNum("preferredChannelId", 0);

        if (const rjson::Value *m = t.find("match")) {
            const rjson::Value *p = m->find("protocolId");
            if (p) {
                tgt.match.hasProtocolId = true;
                tgt.match.protocolId = (p->type == rjson::T_STR)
                    ? lookupSymbolic(p->asStr()) : (unsigned long)p->asNum(0);
            }
            const rjson::Value *f = m->find("flags");
            if (f) {
                tgt.match.hasFlags = true;
                tgt.match.flags = (f->type == rjson::T_STR)
                    ? lookupSymbolic(f->asStr()) : (unsigned long)f->asNum(0);
            }
            const rjson::Value *b = m->find("baud");
            if (b) {
                tgt.match.hasBaud = true;
                tgt.match.baud = (unsigned long)b->asNum(0);
            }
        }

        if (const rjson::Value *reps = t.find("replies"))
            for (size_t j = 0; j < reps->arr.size(); j++) {
                ReplyRule rule;
                if (parseReply(reps->arr[j], rule))
                    tgt.replies.push_back(rule);
            }
        if (const rjson::Value *pers = t.find("periodic"))
            for (size_t j = 0; j < pers->arr.size(); j++) {
                PeriodicRule rule;
                if (parsePeriodic(pers->arr[j], rule))
                    tgt.periodic.push_back(rule);
            }
        targets_.push_back(tgt);
    }
    return true;
}

bool ConfigStore::parseStates(const rjson::Value &v) {
    if (v.type != rjson::T_OBJ) return true;
    stateMachine_.initial = v.getCstr("initial", "CLOSED");
    if (const rjson::Value *tr = v.find("transitions")) {
        for (size_t i = 0; i < tr->arr.size(); i++) {
            const rjson::Value &t = tr->arr[i];
            if (t.type != rjson::T_OBJ) continue;
            StateTransition st;
            st.event = t.getCstr("event", "");
            st.from = t.getCstr("from", "");
            st.to = t.getCstr("to", "");
            stateMachine_.transitions.push_back(st);
        }
    }
    return true;
}

const IoctlRule* ConfigStore::findIoctl(unsigned long ioctlId) const {
    for (size_t i = 0; i < ioctls_.size(); i++)
        if (ioctls_[i].ioctlId == ioctlId) return &ioctls_[i];
    return NULL;
}

const Target* ConfigStore::findTarget(unsigned long protocolId, unsigned long flags,
                                      unsigned long baud) const {
    for (size_t i = 0; i < targets_.size(); i++) {
        const Target &t = targets_[i];
        if (t.match.hasProtocolId && t.match.protocolId != protocolId) continue;
        if (t.match.hasFlags && t.match.flags != flags) continue;
        if (t.match.hasBaud && t.match.baud != baud) continue;
        return &t;
    }
    return NULL;
}
