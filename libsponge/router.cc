#include "router.hh"

#include <iostream>

using namespace std;

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    _rules.emplace_back(RouteRule{route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.
    if (dgram.header().ttl <= 1)
        return;

    auto picked_rule = _rules.end();
    for (auto it = _rules.begin(); it != _rules.end(); ++it) {
        if (_match(dgram.header().dst, (*it).route_prefix, (*it).prefix_length)) {
            if (picked_rule == _rules.end() or (*picked_rule).prefix_length < (*it).prefix_length) {
                picked_rule = it;
            }
        }
    }

    if (picked_rule != _rules.end()) {
        --dgram.header().ttl;
        auto next_hop = (*picked_rule).next_hop;
        size_t interface_num = (*picked_rule).interface_num;
        if (next_hop.has_value())
            _interfaces[interface_num].send_datagram(dgram, next_hop.value());
        else
            _interfaces[interface_num].send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}

bool Router::_match(uint32_t ip1, uint32_t ip2, uint8_t prefix_length) {
    uint32_t mask = (prefix_length != 0 ? 0xffffffff << (32 - prefix_length) : 0);
    return (ip1 & mask) == (ip2 & mask);
}
