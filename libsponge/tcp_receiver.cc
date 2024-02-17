#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();
    bool syn = header.syn;
    bool fin = header.fin;
    // before SYN is set in receiver, segments with no SYN flag should be disposed.
    if (!syn && !_syn_set)
        return;
    if (!_syn_set) {
        _syn_set = true;
        _init_seqno = header.seqno;
    }
    string data = seg.payload().copy();
    if (!data.empty()) {
        // there's a special case in t_ack_rst that a segment with data whose seqno belongs to SYN,
        // that data should be ignored
        if (syn || header.seqno != _init_seqno) {
            // we treat _init_seqno as the index of the first valid byte (though it's actually for SYN)
            // so for segments without SYN, the index should be shifted back by 1
            size_t index = unwrap(header.seqno - (!syn), _init_seqno, _reassembler.wait_index());
            _reassembler.push_substring(data, index, fin);
        }
    }
    // set FIN flag if FIN arrives, and from then on keep checking
    // if the reassembler is clear so that we can close the output stream
    if (fin || _fin_set) {
        _fin_set = true;
        if (_reassembler.unassembled_bytes() == 0)
            _reassembler.stream_out().end_input();
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    optional<WrappingInt32> res = nullopt;
    if (_syn_set) {
        uint64_t index = _reassembler.wait_index() + 1;
        // for ackno we should check whether the output stream has really closed
        // instead of whether FIN flag is set (there may still be unarrived bytes)
        if (_reassembler.stream_out().input_ended())
            index++;
        res.emplace(wrap(index, _init_seqno));
    }
    return res;
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
