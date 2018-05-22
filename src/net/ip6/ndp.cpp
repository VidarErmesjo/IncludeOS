// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//#define NDP_DEBUG 1
#ifdef NDP_DEBUG
#define PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define PRINT(fmt, ...) /* fmt */
#endif
#include <vector>
#include <net/inet>
#include <net/ip6/ndp.hpp>
#include <net/ip6/icmp6.hpp>
#include <statman>

namespace net
{
  Ndp::Ndp(Stack& inet) noexcept:
  requests_rx_    {Statman::get().create(Stat::UINT32, inet.ifname() + ".ndp.requests_rx").get_uint32()},
  requests_tx_    {Statman::get().create(Stat::UINT32, inet.ifname() + ".ndp.requests_tx").get_uint32()},
  replies_rx_     {Statman::get().create(Stat::UINT32, inet.ifname() + ".ndp.replies_rx").get_uint32()},
  replies_tx_     {Statman::get().create(Stat::UINT32, inet.ifname() + ".ndp.replies_tx").get_uint32()},
  inet_           {inet},
  mac_            (inet.link_addr())
  {}

  void Ndp::send_neighbor_advertisement(icmp6::Packet& req)
  {
    icmp6::Packet res(inet_.ip6_packet_factory());
    bool any_src = req.ip().ip_src() == IP6::ADDR_ANY;

    // drop if the packet is too small
    if (res.ip().capacity() < IP6_HEADER_LEN
     + (int) res.header_size() + req.payload().size())
    {
      PRINT("WARNING: Network MTU too small for ICMP response, dropping\n");
      return;
    }

    // Populate response IP header
    res.ip().set_ip_src(inet_.ip6_addr());
    if (any_src) {
        res.ip().set_ip_dst(ip6::Addr::node_all_nodes);
    } else {
        res.ip().set_ip_dst(req.ip().ip_src());
    }
    res.ip().set_ip_hop_limit(255);

    // Populate response ICMP header
    res.set_type(ICMP_type::ND_NEIGHBOR_ADV);
    res.set_code(0);
    res.ndp().set_neighbor_adv_flag(NEIGH_ADV_SOL | NEIGH_ADV_OVERRIDE);
    PRINT("<NDP> Transmitting Neighbor adv to %s\n",
          res.ip().ip_dst().str().c_str());

    // Insert target link address, ICMP6 option header and our mac address
    // TODO: This is hacky. Fix it
    MAC::Addr dest_mac("c0:01:0a:00:00:2a");
    // Target link address
    res.set_payload({req.payload().data(), 16 });
    res.ndp().set_ndp_options_header(0x02, 0x01);
    res.set_payload({reinterpret_cast<uint8_t*> (&dest_mac), 6});

    // Add checksum
    res.set_checksum();

    PRINT("<NDP> Neighbor Adv Response size: %i payload size: %i, checksum: 0x%x\n",
          res.ip().size(), res.payload().size(), res.compute_checksum());

    network_layer_out_(res.release());
  }

  void Ndp::receive_neighbor_advertisement(icmp6::Packet& req)
  {
  }

  void Ndp::send_neighbor_solicitation()
  {
  }

  void Ndp::receive_neighbor_solicitation(icmp6::Packet& req)
  {
    bool any_src = req.ip().ip_src() == IP6::ADDR_ANY;
    IP6::addr target = req.ndp().neighbor_sol().get_target();
    uint8_t *lladdr, *nonce_opt;
    bool found;
    uint64_t nonce = 0;

    PRINT("ICMPv6 NDP Neighbor solicitation request\n");
    PRINT("target: %s\n", target.str().c_str());

    if (target.is_multicast()) {
        PRINT("ND: neighbor solictation target address is multicast\n");
        return;
    }

    if (any_src && !req.ip().ip_dst().is_solicit_multicast()) {
        PRINT("ND: neighbor solictation address is any source "
                "but not solicit destination\n");
        return;
    }
    req.ndp().parse(ICMP_type::ND_NEIGHBOR_SOL);
    lladdr = req.ndp().get_option_data(icmp6::ND_OPT_SOURCE_LL_ADDR);

    if (lladdr) {
        if (any_src) {
            PRINT("ND: bad any source packet with link layer option\n");
            return;
        }
    }

    nonce_opt = req.ndp().get_option_data(icmp6::ND_OPT_NONCE);
    if (nonce_opt) {
        //memcpy(&nonce, nonce_opt, 6);
    }

    bool is_dest_multicast = req.ip().ip_dst().is_multicast();

    if (target != inet_.ip6_addr()) {
        /* Not for us. Should we forward? */
        return;
    }

    if (any_src) {
        send_neighbor_advertisement(req);
        return;
    }

    /* Update/Create cache entry for the source address */
    found = lookup(!is_dest_multicast || lladdr,
            req.ip().ip_src(), lladdr, 0);

    if (found) {
        send_neighbor_advertisement(req);
    }

  }

  void Ndp::send_router_solicitation()
  {
    icmp6::Packet req(inet_.ip6_packet_factory());
    req.ip().set_ip_src(inet_.ip6_addr());
    req.ip().set_ip_dst(ip6::Addr::node_all_nodes);
    req.ip().set_ip_hop_limit(255);
    req.set_type(ICMP_type::ND_ROUTER_SOLICATION);
    req.set_code(0);
    req.set_reserved(0);
    // Set multicast addreqs
    // IPv6mcast_02: 33:33:00:00:00:02

    // Add checksum
    req.set_checksum();

    PRINT("<NDP> Router solicit size: %i payload size: %i, checksum: 0x%x\n",
          req.ip().size(), req.payload().size(), req.compute_checksum());

    network_layer_out_(req.release());
  }

  void Ndp::receive(icmp6::Packet& pckt) {
    switch(pckt.type()) {
    case (ICMP_type::ND_ROUTER_SOLICATION):
      PRINT("<NDP> ICMP Router solictation message from %s\n", pckt.ip().ip_src().str().c_str());
      break;
    case (ICMP_type::ND_ROUTER_ADV):
      PRINT("<NDP> ICMP Router advertisement message from %s\n", pckt.ip().ip_src().str().c_str());
      break;
    case (ICMP_type::ND_NEIGHBOR_SOL):
      PRINT("<NDP> ICMP Neigbor solictation message from %s\n", pckt.ip().ip_src().str().c_str());
      receive_neighbor_solicitation(pckt);
      break;
    case (ICMP_type::ND_NEIGHBOR_ADV):
      PRINT("<NDP> ICMP Neigbor advertisement message from %s\n", pckt.ip().ip_src().str().c_str());
      break;
    case (ICMP_type::ND_REDIRECT):
      PRINT("<NDP> ICMP Neigbor redirect message from %s\n", pckt.ip().ip_src().str().c_str());
      break;
    default:
      return;
    }
  }

  bool Ndp::lookup(bool create, IP6::addr ip, uint8_t *ll_addr, uint8_t flags)
  {
      auto entry = cache_.find(ip);
      if (entry != cache_.end()) {
          return true;
      } else if (create == true && ll_addr) {
          MAC::Addr mac(ll_addr);
          cache(ip, mac, flags);
          return true;
      }
      return false;
  }

  void Ndp::cache(IP6::addr ip, MAC::Addr mac, uint8_t flags)
  {
      PRINT("Ndp Caching IP %s for %s\n", ip.str().c_str(), mac.str().c_str());
      auto entry = cache_.find(ip);
      if (entry != cache_.end()) {
          PRINT("Cached entry found: %s recorded @ %zu. Updating timestamp\n",
             entry->second.mac().str().c_str(), entry->second.timestamp());
          if (entry->second.mac() != mac) {
            cache_.erase(entry);
            cache_.emplace(ip, mac);
          } else {
            entry->second.update();
          }
      } else {
          cache_.emplace(ip, mac); // Insert
          if (UNLIKELY(not flush_timer_.is_running())) {
            flush_timer_.start(flush_interval_);
          }
      }
  }

  void Ndp::resolve_waiting()
  {
    PRINT("<ndp> resolve timer doing sweep\n");

    for (auto it =waiting_packets_.begin(); it != waiting_packets_.end();){
      if (it->second.tries_remaining--) {
        ndp_resolver_(it->first);
        it++;
      } else {
        it = waiting_packets_.erase(it);
      }
    }

    if (not waiting_packets_.empty())
      resolve_timer_.start(1s);

  }

  void Ndp::await_resolution(Packet_ptr pckt, IP6::addr next_hop) {
    auto queue =  waiting_packets_.find(next_hop);
    PRINT("<NDP await> Waiting for resolution of %s\n", next_hop.str().c_str());
    if (queue != waiting_packets_.end()) {
      PRINT("\t * Packets already queueing for this IP\n");
      queue->second.pckt->chain(std::move(pckt));
    } else {
      PRINT("\t *This is the first packet going to that IP\n");
      waiting_packets_.emplace(std::make_pair(next_hop, Queue_entry{std::move(pckt)}));

      // Try resolution immediately
      ndp_resolver_(next_hop);

      // Retry later
      resolve_timer_.start(1s);
    }
  }

  void Ndp::flush_expired()
  {
    PRINT("<NDP> Flushing expired entries\n");
    std::vector<IP6::addr> expired;
    for (auto ent : cache_) {
      if (ent.second.expired()) {
        expired.push_back(ent.first);
      }
    }

    for (auto ip : expired) {
      cache_.erase(ip);
    }

    if (not cache_.empty()) {
      flush_timer_.start(flush_interval_);
    }
  }

  void Ndp::ndp_resolve(IP6::addr next_hop)
  {
    PRINT("<NDP RESOLVE> %s\n", next_hop.str().c_str());

    // Stat increment requests sent
    requests_tx_++;

    // Send ndp solicit
  }

  void Ndp::transmit(Packet_ptr pckt, IP6::addr next_hop)
  {

    Expects(pckt->size());

    PRINT("<NDP -> physical> Transmitting %u bytes to %s\n",
          (uint32_t) pckt->size(), next_hop.str().c_str());

    MAC::Addr dest_mac;

    // If we don't have a cached IP, perform address resolution
    auto cache_entry = cache_.find(next_hop);
    if (UNLIKELY(cache_entry == cache_.end())) {
        PRINT("<NDP> No cache entry for IP %s.  Resolving. \n", next_hop.to_string().c_str());
        await_resolution(std::move(pckt), next_hop);
        return;
    }

    // Get MAC from cache
    dest_mac = cache_[next_hop].mac();

    PRINT("<NDP> Found cache entry for IP %s -> %s \n",
        next_hop.to_string().c_str(), dest_mac.to_string().c_str());

    // Move chain to linklayer
    linklayer_out_(std::move(pckt), dest_mac, Ethertype::IP4);
  }

  // NDP packet function definitions
  namespace icmp6 {
      void Packet::NdpPacket::parse(icmp6::Type type)
      {
        switch(type) {
        case (ICMP_type::ND_ROUTER_SOLICATION):
          ndp_opt_.parse(router_sol().options,
                  (icmp6_.payload_len() - router_sol().option_offset()));
          break;
        case (ICMP_type::ND_ROUTER_ADV):
          break;
        case (ICMP_type::ND_NEIGHBOR_SOL):
          ndp_opt_.parse(neighbor_sol().options,
                  (icmp6_.payload_len() - neighbor_sol().option_offset()));
          break;
        case (ICMP_type::ND_NEIGHBOR_ADV):
          ndp_opt_.parse(neighbor_adv().options,
                  (icmp6_.payload_len() - neighbor_adv().option_offset()));
          break;
        case (ICMP_type::ND_REDIRECT):
          ndp_opt_.parse(router_redirect().options,
                  (icmp6_.payload_len() - router_redirect().option_offset()));
          break;
        default:
          break;
        }
      }

      void Packet::NdpPacket::NdpOptions::parse(uint8_t *opt, uint16_t opts_len)
      {
         uint16_t opt_len;
         header_ = reinterpret_cast<struct nd_options_header*>(opt);
         struct nd_options_header *option_hdr = header_;

          if (option_hdr == NULL) {
             return;
          }
          while(opts_len) {
            if (opts_len < sizeof (struct nd_options_header)) {
               return;
            }
            opt_len = option_hdr->len << 3;

            if (opts_len < opt_len || opt_len == 0) {
               return;
            }
            switch (option_hdr->type) {
            case ND_OPT_SOURCE_LL_ADDR:
            case ND_OPT_TARGET_LL_ADDR:
            case ND_OPT_MTU:
            case ND_OPT_NONCE:
            case ND_OPT_REDIRECT_HDR:
                if (opt_array[option_hdr->type]) {
                } else {
                   opt_array[option_hdr->type] = option_hdr;
                }
                option_hdr = opt_array[option_hdr->type];
                break;
            case ND_OPT_PREFIX_INFO:
                opt_array[ND_OPT_PREFIX_INFO_END] = option_hdr;
                if (!opt_array[ND_OPT_PREFIX_INFO]) {
                   opt_array[ND_OPT_PREFIX_INFO] = option_hdr;
                }
                break;
            case ND_OPT_ROUTE_INFO:
                 nd_opts_ri_end = option_hdr;
                 if (!nd_opts_ri) {
                     nd_opts_ri = option_hdr;
                 }
                 break;
            default:
                 if (is_useropt(option_hdr)) {
                    user_opts_end = option_hdr;
                    if (!user_opts) {
                       user_opts = option_hdr;
                    }
                 } else {
                    PRINT("%s: Unsupported option: type=%d, len=%d\n",
                        __FUNCTION__, option_hdr->type, option_hdr->len);
                 }
            }
            opts_len -= opt_len;
            option_hdr = (option_hdr + opt_len);
        }
     }

  } // icmp6
} // net
