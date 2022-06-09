#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

using namespace std;

void Timer::start(unsigned int timeout) {
    _active = true;
    _expired = false;
    _current_time = 0;
    _timeout = timeout;
}

void Timer::update(unsigned int time_elapsed) {
    if (!_active) {
        throw runtime_error("Trying to update an inactive timer. Please start it first.");
    }
    _current_time += time_elapsed;
    if (_current_time >= _timeout)
        _expired = true;
}

void Timer::reset() {
    _active = false;
    _expired = false;
    _timeout = 0;
    _current_time = 0;
}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _retrans_timeout(retx_timeout)
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const {
    uint64_t res = 0;
    for (auto &seg : _retrans_buf) {
        res += seg.length_in_sequence_space();
    }
    return res;
}

void TCPSender::fill_window() {
    size_t remaining_winsize = (_window_size != 0 ? _window_size : 1);
    size_t out_size = bytes_in_flight();
    if (remaining_winsize < out_size)
        return;
    remaining_winsize -= out_size;

    while (true) {
        size_t seg_size = remaining_winsize;
        if (seg_size == 0)
            break;

        TCPSegment seg;
        TCPHeader &header = seg.header();

        // first, put the SYN flag into the seg if nothing has been sent
        if (!_syn_sent) {
            seg_size -= 1;
            header.syn = true;
            _syn_sent = true;
        }

        // then, stuff as much data as possible into the seg
        header.seqno = wrap(_next_seqno, _isn);
        string seg_data = _stream.read(min(seg_size, TCPConfig::MAX_PAYLOAD_SIZE));
        seg_size -= seg_data.size();
        seg.payload() = Buffer(move(seg_data));

        // finally, put the FIN flag if the input stream has ended and there's still space
        if (!_fin_sent && _stream.eof() && seg_size > 0) {
            seg_size -= 1;
            header.fin = true;
            _fin_sent = true;
        }

        seg_size = seg.length_in_sequence_space();
        // if the segment's actual size is 0, it shouldn't been sent
        if (seg_size == 0)
            break;

        _segments_out.emplace(seg);
        _retrans_buf.emplace_back(seg);
        _next_seqno += seg_size;
        remaining_winsize -= seg_size;

        if (!_timer.active())
            _timer.start(_retrans_timeout);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    _window_size = window_size;
    // use next seqno as checkpoint
    uint64_t ack_seqno = unwrap(ackno, _isn, _next_seqno);
    // it's impossible that ackno > _next_seqno, because that byte hasn't been sent yet!
    if (ack_seqno > _next_seqno) {
        return;
    }

    // remove completely ack-ed segments from the retransmission buffer
    // because the segment in retrans buffer is ordered by seqno,
    // it's ok to break once current seg can't be erased (subsequent seg has larger seqno)
    for (auto it = _retrans_buf.begin(); it != _retrans_buf.end();) {
        if (unwrap((*it).header().seqno, _isn, _next_seqno) + (*it).length_in_sequence_space() <= ack_seqno) {
            it = _retrans_buf.erase(it);
            _retrans_timeout = _initial_retransmission_timeout;
            _timer.start(_retrans_timeout);
            _consec_retrans_count = 0;
        } else
            break;
    }
    // stop the timer if retransmission buffer is clear
    if (_retrans_buf.empty())
        _timer.reset();

    // refill the window
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_timer.active())
        _timer.update(ms_since_last_tick);
    if (_timer.expired()) {
        _segments_out.emplace(_retrans_buf.front());
        if (_window_size > 0) {
            _consec_retrans_count++;
            _retrans_timeout *= 2;
        }
        _timer.start(_retrans_timeout);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consec_retrans_count; }

void TCPSender::send_empty_segment(bool syn, bool fin, bool rst) {
    if (syn)
        _syn_sent = true;
    if (fin)
        _fin_sent = true;

    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    seg.header().syn = syn;
    seg.header().fin = fin;
    seg.header().rst = rst;
    _next_seqno += seg.length_in_sequence_space();
    _segments_out.push(seg);
}
