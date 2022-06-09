#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    if (_input_ended) {
        return 0;
    }
    size_t write_len = min(data.size(), remaining_capacity());
    _buf.insert(_buf.end(), data.begin(), data.begin() + write_len);
    _bytes_written += write_len;
    return write_len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t peeked_len = min(len, _buf.size());
    return string(_buf.begin(), _buf.begin() + peeked_len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t popped_len = min(len, _buf.size());
    _buf.erase(_buf.begin(), _buf.begin() + popped_len);
    _bytes_read += popped_len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
string ByteStream::read(const size_t len) {
    if (len == 0) {
        return "";
    }
    string res = peek_output(len);
    pop_output(res.size());
    return res;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _buf.size(); }

bool ByteStream::buffer_empty() const { return _buf.empty(); }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buf.size(); }
