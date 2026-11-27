// SPDX-License-Identifier: MIT
#include "voice/dave.h"

#include <cstdio>
#include <cstring>

#include <dave/dave.h>

#include "util/util.h"

namespace voice {
namespace {

void logSink(DAVELoggingSeverity severity, const char *file, int line, const char *message)
{
    (void)file;
    (void)line;
    if (severity >= DAVE_LOGGING_SEVERITY_WARNING)
        logf("dave: %s", message);
}

void mlsFailure(const char *source, const char *reason, void *user)
{
    (void)user;
    logf("dave: MLS failure in %s: %s", source, reason);
}

}  // namespace

struct Dave::Impl {
    DAVESessionHandle session = nullptr;
    DAVEEncryptorHandle encryptor = nullptr;
    std::map<uint64_t, DAVEDecryptorHandle> decryptors;  // by user
};

Dave::Dave(uint64_t self, uint64_t channel, Sender send)
    : m(new Impl), m_self(self), m_channel(channel), m_send(std::move(send))
{
    daveSetLogSinkCallback(logSink);
    m->session = daveSessionCreate(nullptr, nullptr, mlsFailure, this);
    m->encryptor = daveEncryptorCreate();
    if (!m->session || !m->encryptor) {
        m_failed = true;
        return;
    }
    daveEncryptorSetPassthroughMode(m->encryptor, false);
}

Dave::~Dave()
{
    for (auto &[id, d] : m->decryptors)
        daveDecryptorDestroy(d);
    if (m->encryptor)
        daveEncryptorDestroy(m->encryptor);
    if (m->session)
        daveSessionDestroy(m->session);
    delete m;
}

uint16_t Dave::maxVersion() { return daveMaxSupportedProtocolVersion(); }

void Dave::setSsrc(uint32_t ssrc)
{
    m_ssrc = ssrc;
    if (m->encryptor)
        daveEncryptorAssignSsrcToCodec(m->encryptor, ssrc, DAVE_CODEC_OPUS);
}

void Dave::sendTransition(int op, uint16_t transition)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"op\":%d,\"d\":{\"transition_id\":%u}}", op, (unsigned)transition);
    m_send(false, buf);
}

void Dave::sendBinary(uint8_t op, const uint8_t *p, size_t n)
{
    std::string msg(1, (char)op);
    msg.append((const char *)p, n);
    m_send(true, msg);
}

void Dave::sendKeyPackage()
{
    uint8_t *bytes = nullptr;
    size_t n = 0;
    daveSessionGetMarshalledKeyPackage(m->session, &bytes, &n);
    if (bytes && n)
        sendBinary(26, bytes, n);
    else
        logf("dave: no key package");
    if (bytes)
        daveFree(bytes);
}

void Dave::init(uint16_t version)
{
    if (m_failed)
        return;
    logf("dave: init, protocol %u", (unsigned)version);
    daveSessionInit(m->session, version, m_channel, std::to_string(m_self).c_str());
    sendKeyPackage();
}

void Dave::start(uint16_t version)
{
    m_started = true;
    m_version = version;
    for (auto &[id, d] : m->decryptors)
        daveDecryptorTransitionToPassthroughMode(d, version == 0);
    if (version > 0)
        init(version);
}

void Dave::clientsConnect(const std::vector<uint64_t> &users)
{
    for (uint64_t u : users) {
        if (!u)
            continue;
        daveSessionAddRecognizedUser(m->session, std::to_string(u).c_str());
        if (u == m_self || m->decryptors.count(u))
            continue;
        DAVEDecryptorHandle d = daveDecryptorCreate();
        if (!d)
            continue;
        daveDecryptorTransitionToPassthroughMode(d, m_version == 0);
        m->decryptors[u] = d;
        // Joining an epoch we're already in: their key exists only after
        // the commit that adds them, which updates every receiver.
        if (DAVEKeyRatchetHandle r = daveSessionGetKeyRatchet(m->session, std::to_string(u).c_str())) {
            daveDecryptorTransitionToKeyRatchet(d, r);
            daveKeyRatchetDestroy(r);
        }
    }
}

void Dave::clientDisconnect(uint64_t user)
{
    daveSessionRemoveRecognizedUser(m->session, std::to_string(user).c_str());
    auto it = m->decryptors.find(user);
    if (it != m->decryptors.end()) {
        daveDecryptorDestroy(it->second);
        m->decryptors.erase(it);
    }
}

void Dave::updateReceivers()
{
    for (auto &[id, d] : m->decryptors) {
        if (DAVEKeyRatchetHandle r = daveSessionGetKeyRatchet(m->session, std::to_string(id).c_str())) {
            daveDecryptorTransitionToKeyRatchet(d, r);
            daveKeyRatchetDestroy(r);
        }
    }
}

void Dave::apply(uint16_t version)
{
    m_version = version;
    if (version == 0) {
        daveEncryptorSetPassthroughMode(m->encryptor, true);
        m_sendKey = false;
        for (auto &[id, d] : m->decryptors)
            daveDecryptorTransitionToPassthroughMode(d, true);
        return;
    }
    DAVEKeyRatchetHandle r = daveSessionGetKeyRatchet(m->session, std::to_string(m_self).c_str());
    if (!r) {
        logf("dave: no sending key after the transition");
        m_sendKey = false;
        return;
    }
    logf("dave: protocol %u active, sending encrypted", (unsigned)version);
    daveEncryptorSetKeyRatchet(m->encryptor, r);
    daveKeyRatchetDestroy(r);
    daveEncryptorSetPassthroughMode(m->encryptor, false);
    for (auto &[id, d] : m->decryptors)
        daveDecryptorTransitionToPassthroughMode(d, false);
    m_sendKey = true;
}

void Dave::ready(uint16_t transition, uint16_t version)
{
    m_pending = true;
    m_pendingTransition = transition;
    m_pendingVersion = version;
    if (transition == 0) {  // the initial transition: no coordination
        m_pending = false;
        apply(version);
    } else {
        sendTransition(23, transition);
    }
}

void Dave::prepareTransition(uint16_t transition, uint16_t version)
{
    logf("dave: prepare transition %u to protocol %u", (unsigned)transition, (unsigned)version);
    ready(transition, version);
}

void Dave::executeTransition(uint16_t transition)
{
    logf("dave: execute transition %u%s", (unsigned)transition,
         !m_pending || transition != m_pendingTransition ? " (not ours, ignored)" : "");
    if (!m_pending || transition != m_pendingTransition)
        return;
    m_pending = false;
    apply(m_pendingVersion);
}

void Dave::prepareEpoch(uint16_t transition, uint16_t version, uint64_t epoch)
{
    logf("dave: prepare epoch %llu, transition %u, protocol %u", (unsigned long long)epoch, (unsigned)transition,
         (unsigned)version);
    if (epoch == 1) {
        // A new group: start over with a fresh key package.
        m_sendKey = false;
        init(version);
    } else {
        daveSessionSetProtocolVersion(m->session, version);
    }
    m_pendingVersion = version;
}

void Dave::binary(uint8_t op, const uint8_t *p, size_t n)
{
    switch (op) {
    case 25:  // external sender package
        logf("dave: external sender (%zu bytes)", n);
        daveSessionSetExternalSender(m->session, p, n);
        break;
    case 27: {  // proposals: answer with a commit (and welcome)
        uint8_t *out = nullptr;
        size_t size = 0;
        daveSessionProcessProposals(m->session, p, n, &out, &size);
        logf("dave: proposals (%zu bytes), commit %zu bytes", n, size);
        if (out && size)
            sendBinary(28, out, size);
        if (out)
            daveFree(out);
        break;
    }
    case 29:    // the winning commit
    case 30: {  // a welcome into the group
        if (n < 2)
            return;
        uint16_t transition = (uint16_t)(p[0] << 8 | p[1]);
        bool ok = false;
        if (op == 29) {
            DAVECommitResultHandle r = daveSessionProcessCommit(m->session, p + 2, n - 2);
            if (r) {
                bool ignored = daveCommitResultIsIgnored(r);
                ok = !daveCommitResultIsFailed(r);
                daveCommitResultDestroy(r);
                if (ignored)
                    return;
            }
        } else {
            DAVEWelcomeResultHandle r = daveSessionProcessWelcome(m->session, p + 2, n - 2);
            ok = r != nullptr;
            if (r)
                daveWelcomeResultDestroy(r);
        }
        if (!ok) {
            // Ask to be removed and added again, with a new key package.
            logf("dave: unusable %s for transition %u", op == 29 ? "commit" : "welcome", (unsigned)transition);
            sendTransition(31, transition);
            m_sendKey = false;
            init(m_version ? m_version : maxVersion());
            return;
        }
        logf("dave: %s for transition %u ok", op == 29 ? "commit" : "welcome", (unsigned)transition);
        updateReceivers();
        ready(transition, daveSessionGetProtocolVersion(m->session));
        break;
    }
    default:
        break;
    }
}

bool Dave::encrypt(const uint8_t *frame, size_t n, std::vector<uint8_t> &out)
{
    if (!m_started)
        return false;
    if (m_version == 0) {
        out.assign(frame, frame + n);
        return true;
    }
    if (!m_sendKey)
        return false;
    out.resize(daveEncryptorGetMaxCiphertextByteSize(m->encryptor, DAVE_MEDIA_TYPE_AUDIO, n));
    size_t written = 0;
    DAVEEncryptorResultCode rc = daveEncryptorEncrypt(m->encryptor, DAVE_MEDIA_TYPE_AUDIO, m_ssrc, frame, n,
                                                      out.data(), out.size(), &written);
    if (rc != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS)
        return false;
    out.resize(written);
    return true;
}

bool Dave::decrypt(uint64_t user, const uint8_t *frame, size_t n, std::vector<uint8_t> &out)
{
    auto it = m->decryptors.find(user);
    if (it == m->decryptors.end()) {
        if (m_version != 0)
            return false;
        out.assign(frame, frame + n);
        return true;
    }
    out.resize(daveDecryptorGetMaxPlaintextByteSize(it->second, DAVE_MEDIA_TYPE_AUDIO, n));
    size_t written = 0;
    if (daveDecryptorDecrypt(it->second, DAVE_MEDIA_TYPE_AUDIO, frame, n, out.data(), out.size(), &written) !=
        DAVE_DECRYPTOR_RESULT_CODE_SUCCESS)
        return false;
    out.resize(written);
    return true;
}

}  // namespace voice
