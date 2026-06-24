#pragma once
// ---------------------------------------------------------------------------
// LogonClient: the realmd (port 3724) side of the vanilla login, driven as a
// PURE state machine so it can be unit-tested with no sockets.
//
//   1. start()                 -> bytes for CMD_AUTH_LOGON_CHALLENGE
//   2. feedChallengeReply(buf) -> bytes for CMD_AUTH_LOGON_PROOF (runs SRP6)
//   3. feedProofReply(buf)     -> verifies M2, then bytes for CMD_REALM_LIST
//   4. feedRealmList(buf)      -> fills realms()
//
// After feedProofReply() succeeds, sessionKey() holds the 40-byte SRP6 K -- the
// exact key the world link reuses for both the auth digest and the header cipher.
//
// SRP6 itself is delegated to src/srp6.* (already correct for both roles); this
// class is just the realmd packet plumbing around it. Validated offline against
// the existing Srp6Server (tests/test_net.cpp): both sides derive the same K.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "net/auth_protocol.hpp"
#include "srp6.hpp"

namespace wf {
namespace net {

class LogonClient {
public:
    // `privateA` is the 32-byte SRP6 client ephemeral. In production it is random;
    // tests pass a fixed value for determinism.
    LogonClient(std::string account, std::string password,
                std::vector<uint8_t> privateA32)
        : account_(toUpper(std::move(account))),
          password_(toUpper(std::move(password))),
          privateA_(std::move(privateA32)) {}

    // Step 1: the initial challenge request bytes.
    std::vector<uint8_t> start() const {
        LogonChallengeRequest req;
        req.account = account_;
        return encodeLogonChallenge(req);
    }

    // Step 2: consume the server challenge, run SRP6, emit the proof request.
    // Returns false (and leaves error()) if the server rejected the account.
    bool feedChallengeReply(const std::vector<uint8_t>& buf,
                            std::vector<uint8_t>& outProofRequest) {
        challenge_ = decodeLogonChallengeReply(buf);
        if (!challenge_.ok) { result_ = challenge_.result; return false; }

        // Drive the existing SRP6 client with the server-provided salt + B.
        Srp6Client cli(account_, password_, challenge_.salt, privateA_);
        cli.process(challenge_.B, sessionKey_, m1_);
        publicA_ = cli.publicA();

        LogonProofRequest pr;
        pr.A  = publicA_;
        pr.M1 = m1_;
        pr.crcHash.assign(20, 0);
        outProofRequest = encodeLogonProof(pr);
        return true;
    }

    // Step 3: verify the server proof M2, then emit the realm-list request.
    bool feedProofReply(const std::vector<uint8_t>& buf,
                        std::vector<uint8_t>& outRealmListRequest) {
        LogonProofReply reply = decodeLogonProofReply(buf);
        if (!reply.ok) { result_ = reply.result; return false; }

        // The client recomputes the expected M2 = SHA1(A | M1 | K) and checks it.
        Bytes expect = Srp6Server::serverProof(publicA_, m1_, sessionKey_);
        if (expect != reply.M2) { authenticated_ = false; return false; }

        authenticated_ = true;
        outRealmListRequest = encodeRealmListRequest();
        return true;
    }

    // Step 4: parse the realm list.
    bool feedRealmList(const std::vector<uint8_t>& buf) {
        realms_ = decodeRealmListReply(buf);
        return true;
    }

    const std::vector<uint8_t>& sessionKey()  const { return sessionKey_; }   // 40 bytes
    const std::vector<uint8_t>& publicA()      const { return publicA_; }
    const std::string&          account()      const { return account_; }
    const std::vector<Realm>&   realms()       const { return realms_; }
    bool                        authenticated() const { return authenticated_; }
    AuthResult                  result()       const { return result_; }

private:
    static std::string toUpper(std::string s) {
        for (char& c : s)
            c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        return s;
    }

    std::string account_, password_;
    std::vector<uint8_t> privateA_;
    LogonChallengeReply  challenge_;
    std::vector<uint8_t> sessionKey_, m1_, publicA_;
    std::vector<Realm>   realms_;
    bool                 authenticated_ = false;
    AuthResult           result_ = AuthResult::SUCCESS;
};

} // namespace net
} // namespace wf
