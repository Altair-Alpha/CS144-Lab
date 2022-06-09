#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <map>
#include <string>

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;                         //!< The reassembled in-order byte stream
    std::map<size_t, std::string> _wait_map{};  // byte streams to be assembled.
                                                // Key is the first byte's index and value is the stream itself.

    size_t _capacity;  //!< The maximum number of bytes

    size_t _wait_index;  // waiting byte's index (i.e. the index of the last byte in _output + 1)
    size_t _eof_index;   // last byte's index (this is needed because the last byte of input with eof flag
                         // maybe discarded due to insufficient capacity though we still need to
                         // remember that byte is the real sign of eof)

    // Truncate the data to make sure it doesn't overflow the capacity.
    // Should be called before actual write to either _output or _wait_map.
    std::string truncate_data(const std::string &data, uint64_t index);

    // Update data in the _wait_map according to new _wait_index. Bytes with index smaller than _wait_index would be
    // truncated. Should be called after an assemble, i.e. _output.write() happens
    void update_waitmap();

    // Try to insert a pair of index and data into the _wait_map.
    // This function make sure that after insertion, no repeating data will be stored.
    // e.g., _wait_map: {1:'b', 3:'def'}: an insertion of 3:'d'/3:'de' will do nothing,
    // an insertion of 0:'abc' will succeed and erase existing 1:'b',
    // an insertion of 4:'defg' will be merged with 3:'def' to be 3:'defg',
    // an insertion of 6:'gh' will succeed
    void checked_insert(const std::string &data, uint64_t index);

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    // Returns index of the first absent byte.
    // This is needed for lab2 to provide information for TCPReciver.
    size_t wait_index() const { return _wait_index; }

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
