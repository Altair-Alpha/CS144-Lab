#include "stream_reassembler.hh"

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _wait_index(0), _eof_index(numeric_limits<size_t>::max()) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        _eof_index = index + data.size();  // one over last byte's index
    }
    // If the incoming data's index is smaller than waiting index, truncate it at front
    // (we can't erase data that's  already been written to the end of output stream,
    // and as the document says both substring in this case should be the same)
    // size_t start_pos = max(0, static_cast<int>(_wait_index) - static_cast<int>(index));
    size_t start_pos = index < _wait_index ? _wait_index - index : 0;
    if (start_pos >= data.size()) {
        if (empty() && _wait_index >= _eof_index) {
            _output.end_input();
        }
        return;
    }
    string write_data = data.substr(start_pos);
    size_t moved_index = index + start_pos;

    if (moved_index > _wait_index) {
        checked_insert(write_data, index);
    } else if (moved_index == _wait_index) {
        write_data = truncate_data(write_data, index);
        size_t write_size = _output.write(write_data);  // write_size should be equal to trucated data size
        _wait_index += write_size;

        // try to reassemble as much data as possible
        while (true) {
            update_waitmap();
            auto it = _wait_map.find(_wait_index);
            if (it == _wait_map.end()) {
                break;
            }
            write_size = _output.write((*it).second);
            _wait_index += write_size;
            _wait_map.erase(it);
        }
    }
    // if all data in wait buffer has been assembled (including eof byte)
    // it's ok to close the output stream
    if (empty() && _wait_index == _eof_index) {
        _output.end_input();
    }
}

string StreamReassembler::truncate_data(const string &data, uint64_t index) {
    // Two conditions for truncating data:
    // first, buffer's size + _wait_map's size + data's size <= capacity
    // second, bytes with index > _capacity + bytes_read() should be discarded, because storing
    // them may cause bytes before it can't be stored, causing a shorter assembled length.
    // (e.g., capacity=8, push 0:'abc' ok, push 6:'ghX' should be truncate to 'gh', otherwise 'f' in
    // incoming 'def' never has a chance to be stored/assembled if no read happens, and storing 'gh'
    // would also be meaningless because they can't be assembled without 'f' in place)
    size_t trunc_size = min(data.size(), _capacity + _output.bytes_read() - index);
    trunc_size = min(trunc_size, _capacity - _output.buffer_size() - unassembled_bytes());
    return data.substr(0, trunc_size);
}

void StreamReassembler::update_waitmap() {
    for (auto it = _wait_map.begin(); it != _wait_map.end();) {
        size_t index = (*it).first;
        if (index < _wait_index) {
            string data = (*it).second;
            it = _wait_map.erase(it);  // erase anyway as we're either discarding it or modifying both key and value
            if (index + data.size() > _wait_index) {
                data = data.substr(_wait_index - index);
                index = _wait_index;
                checked_insert(data, index);
            }
        } else {
            ++it;
        }
    }
}

void StreamReassembler::checked_insert(const string &data, uint64_t index) {
    string ins_data = data;
    // check and truncate data according to _wait_map's content
    size_t ins_start = index, ins_end = index + ins_data.size() - 1;
    for (auto it = _wait_map.begin(); it != _wait_map.end();) {
        const string &elem_data = (*it).second;
        size_t elem_start = (*it).first, elem_end = elem_start + elem_data.size() - 1;
        // insert data overlaps with current element 'e'
        if (ins_start <= elem_end && elem_start <= ins_end) {
            // insert data completely covers 'e', erase 'e'
            if (ins_start <= elem_start && ins_end >= elem_end) {
                it = _wait_map.erase(it);
            }
            // insert data is completely covered by 'e', clear data (do not insert)
            else if (elem_start <= ins_start && elem_end >= ins_end) {
                ins_data.clear();
                ++it;
            }
            // insert data partially overlaps with 'e', merge 'e' into data
            else {
                if (ins_start <= elem_start) {
                    ins_data += elem_data.substr(ins_end + 1 - elem_start, elem_end - ins_end);
                } else {
                    index = elem_start;
                    ins_data.insert(0, elem_data.substr(0, ins_start - elem_start));
                }
                it = _wait_map.erase(it);
            }
        } else {
            ++it;
        }
    }
    // if data isn't empty after checking, perform the insert
    if (!ins_data.empty()) {
        _wait_map.insert(make_pair(index, truncate_data(ins_data, index)));
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t count = 0;
    for (auto &p : _wait_map) {
        count += p.second.size();
    }
    return count;
}

bool StreamReassembler::empty() const { return _wait_map.empty(); }
