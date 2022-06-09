#include "wrapping_integers.hh"

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // overflowing n by casting it to uint32_t is equivalent to n % (UINT32_MAX+1)
    return WrappingInt32(isn + static_cast<uint32_t>(n));
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // see wrap
    uint64_t base = static_cast<uint64_t>(UINT32_MAX) + 1;
    uint64_t cp_mod = checkpoint % base;
    uint64_t cp_base = checkpoint - cp_mod;
    if (cp_mod == 0 && cp_base >= base) {
        cp_mod += base;
        cp_base -= base;
    }
    uint64_t n_mod = static_cast<uint64_t>(n.raw_value() - isn.raw_value());
    if (n_mod > cp_mod) {
        if (cp_base >= base && (base - n_mod + cp_mod) <= (n_mod - cp_mod))
            return cp_base - base + n_mod;
        else
            return cp_base + n_mod;
    }
    if ((cp_mod - n_mod) <= (n_mod + base - cp_mod))
        return cp_base + n_mod;
    else
        return cp_base + base + n_mod;
}
