
#include <service>
#include <net/interfaces>
#include <net/dhcp/dhcpd.hpp>
#include <list>

std::unique_ptr<net::dhcp::DHCPD> server;

void Service::start(const std::string&)
{
  using namespace net;
  using namespace dhcp;

  // Server

  auto& inet = Interfaces::get(0);
  inet.network_config(
    { 10,0,0,9 },     // IP
    { 255,255,255,0 },  // Netmask
    { 10,0,0,1 },       // Gateway
    { 8,8,8,8 });       // DNS

  IP4::addr pool_start{10,0,0,10};
  IP4::addr pool_end{10,0,0,20};
  server = std::make_unique<DHCPD>(inet.udp(), pool_start, pool_end);

  // Client 1

  Interfaces::get(1).negotiate_dhcp(10.0, [] (bool timeout) {
    if (timeout) {
      printf("Client 1 timed out\n");
    }
    else {
      INFO("DHCP test", "Client 1 got IP from IncludeOS DHCP server");
      printf("%s\n", Interfaces::get(1).ip_addr().str().c_str());
    }
  });

  // Client 2

  Interfaces::get(2).negotiate_dhcp(10.0, [] (bool timeout) {
    if (timeout) {
      printf("Client 2 timed out\n");
    }
    else {
      INFO("DHCP test", "Client 2 got IP from IncludeOS DHCP server");
      printf("%s\n", Interfaces::get(2).ip_addr().str().c_str());
    }
  });

  // Client 3

  Interfaces::get(3).negotiate_dhcp(10.0, [] (bool timeout) {
    if (timeout) {
      printf("Client 3 timed out\n");
    }
    else {
      INFO("DHCP test", "Client 3 got IP from IncludeOS DHCP server");
      printf("%s\n", Interfaces::get(3).ip_addr().str().c_str());
    }
  });
}
