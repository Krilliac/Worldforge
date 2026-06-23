#include "srp6.hpp"
#include <algorithm>
#include <cctype>

namespace wf {
namespace {

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

Bytes sha1v(const Bytes& v) { auto h = sha1(v); return Bytes(h.begin(), h.end()); }

} // namespace

const BigUInt& Srp6::N() {
    static const BigUInt n = BigUInt::fromHexBE(
        "894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7");
    return n;
}
BigUInt Srp6::g() { return BigUInt(7); }
BigUInt Srp6::k() { return BigUInt(3); }

BigUInt Srp6::calcX(const std::string& user, const std::string& pass, const Bytes& salt32) {
    std::string up = upper(user) + ":" + upper(pass);
    Bytes interim = sha1v(Bytes(up.begin(), up.end()));
    Bytes xInput = concat({ salt32, interim });
    return BigUInt::fromBytesLE(sha1v(xInput));
}

Bytes Srp6::passwordVerifier(const std::string& user, const std::string& pass,
                             const Bytes& salt32) {
    BigUInt x = calcX(user, pass, salt32);
    return BigUInt::modpow(g(), x, N()).toBytesLE(32);
}

// Interleaved SHA-1: split S's 32 little-endian bytes into even/odd halves,
// hash each, then interleave the two 20-byte digests into a 40-byte key.
Bytes Srp6::sessionKey(const BigUInt& S) {
    Bytes s = S.toBytesLE(32);
    Bytes even(16), odd(16);
    for (int i = 0; i < 16; ++i) { even[i] = s[i*2]; odd[i] = s[i*2+1]; }
    Bytes h1 = sha1v(even), h2 = sha1v(odd);
    Bytes K(40);
    for (int i = 0; i < 20; ++i) { K[i*2] = h1[i]; K[i*2+1] = h2[i]; }
    return K;
}

namespace {
// u = SHA1(A | B) as a little-endian integer.
BigUInt calcU(const Bytes& A32, const Bytes& B32) {
    return BigUInt::fromBytesLE(sha1v(concat({ A32, B32 })));
}

// M1 = SHA1( (SHA1(N) xor SHA1(g)) | SHA1(USER) | salt | A | B | K ).
Bytes calcM1(const std::string& user, const Bytes& salt,
             const Bytes& A32, const Bytes& B32, const Bytes& K) {
    Bytes hN = sha1v(Srp6::N().toBytesLE(32));
    Bytes hG = sha1v(Srp6::g().toBytesLE());     // single byte 0x07
    Bytes xorH(20);
    for (int i = 0; i < 20; ++i) xorH[i] = hN[i] ^ hG[i];
    std::string U = upper(user);
    Bytes hU = sha1v(Bytes(U.begin(), U.end()));
    return sha1v(concat({ xorH, hU, salt, A32, B32, K }));
}
} // namespace

// ---------------- server ----------------
Srp6Server::Srp6Server(const std::string& user, const Bytes& verifier32, const Bytes& salt32,
                       const Bytes& b32)
    : v(BigUInt::fromBytesLE(verifier32)), b(BigUInt::fromBytesLE(b32)),
      salt(salt32), user(user) {
    // B = (k*v + g^b) mod N
    BigUInt kv = (Srp6::k() * v) % Srp6::N();
    BigUInt gb = BigUInt::modpow(Srp6::g(), b, Srp6::N());
    B = (kv + gb) % Srp6::N();
}

bool Srp6Server::process(const Bytes& A32, Bytes& outK, Bytes& outM1) const {
    BigUInt A = BigUInt::fromBytesLE(A32);
    if ((A % Srp6::N()).isZero()) return false;        // invalid client key

    BigUInt u = calcU(A32, B.toBytesLE(32));
    // S = (A * v^u)^b mod N
    BigUInt vu = BigUInt::modpow(v, u, Srp6::N());
    BigUInt base = (A * vu) % Srp6::N();
    BigUInt S = BigUInt::modpow(base, b, Srp6::N());

    outK  = Srp6::sessionKey(S);
    outM1 = calcM1(user, salt, A32, B.toBytesLE(32), outK);
    return true;
}

Bytes Srp6Server::serverProof(const Bytes& A32, const Bytes& M1, const Bytes& K) {
    return sha1v(concat({ A32, M1, K }));
}

// ---------------- client ----------------
Srp6Client::Srp6Client(const std::string& user, const std::string& pass, const Bytes& salt32,
                       const Bytes& a32)
    : user(user), pass(pass), salt(salt32), a(BigUInt::fromBytesLE(a32)) {
    A = BigUInt::modpow(Srp6::g(), a, Srp6::N());      // A = g^a mod N
}

void Srp6Client::process(const Bytes& B32, Bytes& outK, Bytes& outM1) const {
    BigUInt B = BigUInt::fromBytesLE(B32);
    BigUInt x = Srp6::calcX(user, pass, salt);
    BigUInt u = calcU(A.toBytesLE(32), B32);

    // S = (B - k * g^x) ^ (a + u*x) mod N, kept positive in the modular field.
    BigUInt gx = BigUInt::modpow(Srp6::g(), x, Srp6::N());
    BigUInt kgx = (Srp6::k() * gx) % Srp6::N();
    BigUInt Bm = B % Srp6::N();
    BigUInt base = (Bm >= kgx) ? (Bm - kgx) : ((Bm + Srp6::N()) - kgx);
    BigUInt exp = a + (u * x);
    BigUInt S = BigUInt::modpow(base, exp, Srp6::N());

    outK  = Srp6::sessionKey(S);
    outM1 = calcM1(user, salt, A.toBytesLE(32), B32, outK);
}

} // namespace wf
