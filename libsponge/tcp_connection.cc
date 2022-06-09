/*
#include "tcp_connection.hh"

#include <iostream>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received_counter; }

bool TCPConnection::real_send() {
    bool isSend = false;
    while (!_sender.segments_out().empty()) {
        isSend = true;
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_windowsize(segment);
        _segments_out.push(segment);
    }
    return isSend;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_received_counter = 0;
    // check if the RST has been set
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }

    // give the segment to receiver
    _receiver.segment_received(seg);

    // check if need to linger
    if (check_inbound_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    // check if the ACK has been set
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        real_send();
    }

    // send ack
    if (seg.length_in_sequence_space() > 0) {
        // handle the SYN/ACK case
        _sender.fill_window();
        bool isSend = real_send();
        // send at least one ack message
        if (!isSend) {
            _sender.send_empty_segment();
            TCPSegment ACKSeg = _sender.segments_out().front();
            _sender.segments_out().pop();
            set_ack_and_windowsize(ACKSeg);
            _segments_out.push(ACKSeg);
        }
    }

    return;
}

bool TCPConnection::active() const { return _active; }

void TCPConnection::set_ack_and_windowsize(TCPSegment &segment) {
    // ask receiver for ack and window size
    optional<WrappingInt32> ackno = _receiver.ackno();
    if (ackno.has_value()) {
        segment.header().ack = true;
        segment.header().ackno = ackno.value();
    }
    size_t window_size = _receiver.window_size();
    segment.header().win = static_cast<uint16_t>(window_size);
    return;
}

void TCPConnection::connect() {
    // send SYN
    _sender.fill_window();
    real_send();
}

size_t TCPConnection::write(const string &data) {
    if (!data.size()) return 0;
    size_t actually_write = _sender.stream_in().write(data);
    _sender.fill_window();
    real_send();
    return actually_write;
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // cout<<"!!!!!!!!!! end input !!!!!!!!!!"<<endl;
    // cout<<"stream_in : "<< _sender.stream_in().input_ended()<< " " << _sender.stream_in().buffer_empty() << endl;
    // may send FIN
    _sender.fill_window();
    real_send();
}

void TCPConnection::send_RST() {
    _sender.send_empty_segment();
    TCPSegment RSTSeg = _sender.segments_out().front();
    _sender.segments_out().pop();
    set_ack_and_windowsize(RSTSeg);
    RSTSeg.header().rst = true;
    _segments_out.push(RSTSeg);
}

// prereqs1 : The inbound stream has been fully assembled and has ended.
bool TCPConnection::check_inbound_ended() {
    return _receiver.unassembled_bytes() == 0 && _receiver.stream_out().input_ended();
}
// prereqs2 : The outbound stream has been ended by the local application and fully sent (including
// the fact that it ended, i.e. a segment with fin ) to the remote peer.
// prereqs3 : The outbound stream has been fully acknowledged by the remote peer.
bool TCPConnection::check_outbound_ended() {
    return _sender.stream_in().eof() && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 &&
           _sender.bytes_in_flight() == 0;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received_counter += ms_since_last_tick;
    // tick the sender to do the retransmit
    _sender.tick(ms_since_last_tick);
    // if new retransmit segment generated, send it
    if (_sender.segments_out().size() > 0) {
        TCPSegment retxSeg = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_windowsize(retxSeg);
        // abort the connection
        if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            retxSeg.header().rst = true;
            _active = false;
            //cerr << "TOO MANY RETRANS.";
            //cerr << " Sender remains: " << _sender._retrans_buf.size()
            //        << " packets. First IDX " << _sender._retrans_buf.front().header().seqno.raw_value()
            //        << " size: " << _sender._retrans_buf.front().length_in_sequence_space() << endl;
        }
        _segments_out.push(retxSeg);
    }

    // // check if need to linger
    // if (check_inbound_ended() && !_sender.stream_in().eof()) {
    //     _linger_after_streams_finish = false;
    // }

    // check if done
    if (check_inbound_ended() && check_outbound_ended()) {
        if (!_linger_after_streams_finish) {
            _active = false;
        } else if (_time_since_last_segment_received_counter >= 10 * _cfg.rt_timeout) {
            _active = false;
        }
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            send_RST();
            _active = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
//*/

//*
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
//*/
