#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <exception>
#include <functional>
#include <queue>

// Helper class to determine whether a given timeout has reached (i.e., expired) since started.
// It won't emit any signal but provide accessor for caller to check its state
// Also the class won't call any system time function but rely on its update() called to
// know time elapsed and whether timeout is reached.
class Timer {
  public:
    // start the timer with given timeout.
    // call start() on a started timer acts like reset() called first
    void start(unsigned int timeout);
    // update the timer with info of time elapsed since start/last update
    void update(unsigned int time_elapsed);
    // reset(stop) the timer
    void reset();
    // check whether the timer is active
    bool active() const { return _active; }
    // check whether the timer has timed out (if the timer is inactive, return value is false)
    bool expired() const { return _active && _expired; }

  private:
    bool _active{false};
    bool _expired{false};
    unsigned int _current_time{0};
    unsigned int _timeout{0};
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};
    // temporarily store outstanding segment for possible retransmission
    std::deque<TCPSegment> _retrans_buf{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;
    // current retransimission timeout
    unsigned int _retrans_timeout;
    // consecutive retransmissions occured
    unsigned int _consec_retrans_count{0};

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    // current window size, updated when ack_received() called.
    // the initial and minimum value is 1 so that the sender won't wait endlessly.
    uint16_t _window_size{1};

    // flags for whether SYN and FIN has been sent
    bool _syn_sent{false};
    bool _fin_sent{false};

    // unique timer
    Timer _timer{};

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment(bool syn = false, bool fin = false, bool rst = false);

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
//*/
