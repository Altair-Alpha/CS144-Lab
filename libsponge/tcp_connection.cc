
#include "tcp_connection.hh"

#include <iostream>
#include <limits>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _last_recv_et; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _last_recv_et = 0;

    const TCPHeader &header = seg.header();

    if (header.rst) {
        _shutdown(false);
        return;
    }

    _receiver.segment_received(seg);

    if (header.ack) {
        _sender.ack_received(header.ackno, header.win);
    }

    // if the incoming segment occupys seqno and nothing has been sent,
    // send at least one segment
    if (seg.length_in_sequence_space() > 0 && _sender.segments_out().size() == 0) {
        // in the case of listen SYN/ACK, fill_window() will send a segment
        // with SYN=1 instead of 0
        _sender.fill_window();

        // if it's not the above case, send a plain empty segment
        if (_sender.segments_out().size() == 0)
            _sender.send_empty_segment();
    }

    // keep-alive response
    if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0) &&
        (header.seqno == _receiver.ackno().value() - 1)) {
        _sender.send_empty_segment();
    }
    _clear_sendbuf();

    if (_receiver.stream_out().input_ended() && !_sender.stream_in().input_ended()) {
        _linger_after_streams_finish = false;
    }
}

void TCPConnection::_clear_sendbuf() {
    auto &sender_queue = _sender.segments_out();
    while (!sender_queue.empty()) {
        TCPSegment &seg = sender_queue.front();
        TCPHeader &header = seg.header();
        // header.win is of type uint16_t, while receiver's window size is size_t
        // so we need to clamp it
        uint16_t max_winsize = numeric_limits<uint16_t>::max();
        if (_receiver.window_size() > max_winsize)
            header.win = max_winsize;
        else
            header.win = _receiver.window_size();

        if (_receiver.ackno().has_value()) {
            header.ack = true;
            header.ackno = _receiver.ackno().value();
        }

        _segments_out.push(seg);
        sender_queue.pop();
    }
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t data_size = _sender.stream_in().write(data);
    _sender.fill_window();
    _clear_sendbuf();
    return data_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _last_recv_et += ms_since_last_tick;

    if (_sender.consecutive_retransmissions() >= _cfg.MAX_RETX_ATTEMPTS) {
        _send_rst_segment();
        _shutdown(false);
        return;
    }

    _sender.tick(ms_since_last_tick);

    if (_should_shutdown()) {
        if (_linger_after_streams_finish) {
            if (_last_recv_et >= 10 * _cfg.rt_timeout) {
                _shutdown(true);
            }
        } else
            _shutdown(true);
    }
    _clear_sendbuf();
}

bool TCPConnection::_should_shutdown() const {
    return _receiver.stream_out().input_ended() && (_receiver.unassembled_bytes() == 0) &&
           _sender.stream_in().input_ended() && (_sender.bytes_in_flight() == 0) &&
           (_sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2);
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    _clear_sendbuf();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _clear_sendbuf();
}

void TCPConnection::_send_rst_segment() {
    _sender.send_empty_segment(false, false, true);
    _clear_sendbuf();
}

void TCPConnection::_shutdown(bool clean) {
    if (!clean) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
    }
    _active = false;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _send_rst_segment();
            _shutdown(false);
            _clear_sendbuf();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
