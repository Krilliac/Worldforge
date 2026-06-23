#pragma once
// ---------------------------------------------------------------------------
// WoW 1.12.1 flavour of SRP6 (logon / realmd authentication). Constants and
// algorithm verified against GTKer's implementation guide:
//   N = 894B645E...2A3E9BB7 (big endian), g = 7, k = 3, SHA-1, little-endian.
//
// Both client and server roles are implemented so the full handshake can be
// exercised offline: with the same password both sides derive an identical
// session key K and the M1/M2 proofs verify.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "crypto.hpp"

namespace wf {

using Bytes = std::vector<uint8_t>;

// The salt and public/private keys are fixed-width little-endian byte arrays.
struct Srp6 {
    static const BigUInt& N();         // large safe prime
    static BigUInt g();                // 7
    static BigUInt k();                // 3

    // Server registration: verifier v = g^x mod N for an account.
    static Bytes passwordVerifier(const std::string& user, const std::string& pass,
                                  const Bytes& salt32);

    // x = SHA1(salt | SHA1(USER:PASS)) as a little-endian integer.
    static BigUInt calcX(const std::string& user, const std::string& pass, const Bytes& salt32);

    // Interleaved-SHA1 session key from S (the shared secret).
    static Bytes sessionKey(const BigUInt& S);
};

// ---- server side ----
struct Srp6Server {
    BigUInt v, b, B;
    Bytes   salt;     // 32
    std::string user;

    // Construct from a stored verifier + salt and a server private key b (32 bytes).
    Srp6Server(const std::string& user, const Bytes& verifier32, const Bytes& salt32,
               const Bytes& b32);

    Bytes publicB() const { return B.toBytesLE(32); }

    // Given client public key A, derive K and the expected client proof M1.
    // Returns false if A is invalid (A mod N == 0).
    bool process(const Bytes& A32, Bytes& outK, Bytes& outM1) const;

    // Server proof to send back once M1 matched.
    static Bytes serverProof(const Bytes& A32, const Bytes& M1, const Bytes& K);
};

// ---- client side ----
struct Srp6Client {
    std::string user, pass;
    Bytes salt;       // 32
    BigUInt a, A;

    Srp6Client(const std::string& user, const std::string& pass, const Bytes& salt32,
               const Bytes& a32);

    Bytes publicA() const { return A.toBytesLE(32); }

    // Given server public key B, derive K and client proof M1.
    void process(const Bytes& B32, Bytes& outK, Bytes& outM1) const;
};

} // namespace wf
