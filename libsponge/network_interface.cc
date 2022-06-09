#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    auto it = _addr_cache.find(next_hop_ip);
    // dst Ethernet address already known
    if (it != _addr_cache.end()) {
        _frames_out.emplace(_make_frame((*it).second.first, EthernetHeader::TYPE_IPv4, dgram.serialize()));
    } else {
        // broadcast ARP request if the IP address hasn't been queried or os queried over 5000ms ago
        auto it1 = _addr_request_time.find(next_hop_ip);
        if (it1 == _addr_request_time.end() or _current_time - (*it1).second > 5000) {
            ARPMessage msg;
            msg.sender_ethernet_address = _ethernet_address;
            msg.sender_ip_address = _ip_address.ipv4_numeric();
            msg.target_ip_address = next_hop_ip;
            msg.opcode = ARPMessage::OPCODE_REQUEST;

            _frames_out.emplace(_make_frame(ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, BufferList(msg.serialize())));

            // emplace or update last query time
            if (it1 == _addr_request_time.end())
                _addr_request_time.emplace(next_hop_ip, _current_time);
            else
                (*it1).second = _current_time;
        }
        _waiting_dgrams.emplace_back(make_pair(next_hop_ip, dgram));
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    optional<InternetDatagram> res = nullopt;

    const auto &header = frame.header();
    if (header.dst == _ethernet_address or header.dst == ETHERNET_BROADCAST) {
        if (header.type == EthernetHeader::TYPE_IPv4) {
            InternetDatagram dgram;
            if (dgram.parse(Buffer(frame.payload())) == ParseResult::NoError) {
                res.emplace(dgram);
            }
        } else if (header.type == EthernetHeader::TYPE_ARP) {
            ARPMessage msg;
            if (msg.parse(Buffer(frame.payload())) == ParseResult::NoError) {
                // record sender's address
                auto it = _addr_cache.find(msg.sender_ip_address);
                if (it != _addr_cache.end()) {
                    (*it).second.first = msg.sender_ethernet_address;
                    (*it).second.second = _current_time;
                } else {
                    _addr_cache.emplace(msg.sender_ip_address, make_pair(msg.sender_ethernet_address, _current_time));
                }
                auto it1 = _addr_request_time.find(msg.sender_ip_address);
                if (it1 != _addr_request_time.end())
                    _addr_request_time.erase(it1);

                _try_send_waiting(msg.sender_ip_address);

                // send a reply if we are the target
                if (msg.opcode == ARPMessage::OPCODE_REQUEST and msg.target_ip_address == _ip_address.ipv4_numeric()) {
                    ARPMessage reply_msg;
                    reply_msg.sender_ethernet_address = _ethernet_address;
                    reply_msg.sender_ip_address = _ip_address.ipv4_numeric();
                    reply_msg.target_ethernet_address = msg.sender_ethernet_address;
                    reply_msg.target_ip_address = msg.sender_ip_address;
                    reply_msg.opcode = ARPMessage::OPCODE_REPLY;

                    _frames_out.emplace(_make_frame(
                        msg.sender_ethernet_address, EthernetHeader::TYPE_ARP, BufferList(reply_msg.serialize())));
                }
            }
        }
    }
    return res;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _current_time += ms_since_last_tick;
    _remove_expired_cache();
}

void NetworkInterface::_remove_expired_cache() {
    for (auto it = _addr_cache.begin(); it != _addr_cache.end();) {
        if (_current_time - (*it).second.second > 30000)
            it = _addr_cache.erase(it);
        else
            ++it;
    }
}

void NetworkInterface::_try_send_waiting(uint32_t new_ip) {
    for (auto it = _waiting_dgrams.begin(); it != _waiting_dgrams.end();) {
        if ((*it).first == new_ip) {
            _frames_out.emplace(
                _make_frame(_addr_cache[new_ip].first, EthernetHeader::TYPE_IPv4, (*it).second.serialize()));
            it = _waiting_dgrams.erase(it);
        } else
            ++it;
    }
}

EthernetFrame NetworkInterface::_make_frame(const EthernetAddress &dst, uint16_t type, const BufferList &payload) {
    EthernetFrame frame;
    frame.header().src = _ethernet_address;
    frame.header().dst = dst;
    frame.header().type = type;
    frame.payload() = payload;
    return frame;
}
