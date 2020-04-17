#include "cxxopts/include/cxxopts.hpp"

#include <iostream>
#include <string>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#define	MAXIPLEN 60
#define	MAXICMPLEN 76

void error(std::string msg) {
  std::cerr << strerror(errno) << std::endl;
  std::cerr << msg << std::endl;
  exit(1);
}

class Pinger {
 public:
  Pinger(const std::string& hostname, int port);
  ~Pinger() {
    close(sockfd);
  }

  void ping();
  uint16_t check_sum(uint16_t* addr, unsigned len);

 private:
  int datalen = 56;
  int cc = 64;

  std::string hostname;
  int port;

  int sockfd;
  int sequence_num = 1337;
  struct hostent* host;
  struct sockaddr_in ping_addr;
  struct icmp* icp;
};

Pinger::Pinger(const std::string& hostname, int port)
    : hostname(hostname), port(port) {

  struct icmp icp_val = {
    .icmp_type = ICMP_ECHO,
    .icmp_code = 0,
    .icmp_cksum = 0,
    .icmp_seq = static_cast<n_short>(sequence_num),
    .icmp_id = static_cast<n_short>(getpid()),
  };
  icp = &icp_val;

  sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sockfd < 0) {
    error("ERROR creating socket");
  }

  host = gethostbyname(hostname.c_str());
  if (host == NULL) {
    error("ERROR resolving hostname");
  }

  ping_addr = {
    .sin_family = static_cast<sa_family_t>(host->h_addrtype),
    .sin_port = htons(port),
  };
	bcopy(host->h_addr, (caddr_t)&ping_addr.sin_addr, host->h_length);

  std::cout
    << "PING " << hostname
    << "(" << inet_ntoa(ping_addr.sin_addr) << "): "
    << datalen << " data bytes"
    << std::endl;
}

void Pinger::ping() {
  struct timeval start;
  struct timeval end;

  gettimeofday(&start, NULL);

  icp->icmp_cksum = check_sum((uint16_t*)icp, cc);

  int send_result = sendto(
    sockfd,
    (char*)icp,
    cc,
    0,
    (struct sockaddr*)&ping_addr,
    (socklen_t)sizeof(struct sockaddr_in)
  );
  if (send_result < 0) {
    error("ERROR, couldn't sendto()");
  }
  if (send_result != cc) {
    std::cout << "wrote " << hostname << " " << cc << " chars, ret= " << std::endl;
  }

  struct timeval timeout = {
    .tv_sec = 1,
    .tv_usec = 0,
  };

  fd_set fd_mask;
  FD_ZERO(&fd_mask);
  FD_SET(sockfd, &fd_mask);

  if(select(sockfd + 1, &fd_mask, NULL, NULL, &timeout) == 0) {
    return;
  }

  struct sockaddr_in from_addr = {};
  int from_len = sizeof(from_addr);
  int packlen = datalen + MAXIPLEN + MAXICMPLEN;
  char packet[512];
  int ret = recvfrom(sockfd, packet, packlen, 0, (struct sockaddr*)&from_addr, (socklen_t*)&from_len);
  if (ret < 0) {
    error("ERROR, couldn't recvfrom()");
  }

  struct ip* ip = (struct ip*)packet;
  if (ret < (sizeof(struct ip) + ICMP_MINLEN)) { 
    error("ERROR, packet too short (" + std::to_string(ret) + " bytes) from " + hostname);
  } 

	int hlen = sizeof(struct ip);
  icp = (struct icmp*)(packet + hlen); 
  if (icp->icmp_type == ICMP_ECHOREPLY) {
    if (icp->icmp_seq != sequence_num) {
      std::cout << "received sequence # " << icp->icmp_seq << std::endl;
      return;
    }
    if (icp->icmp_id != getpid()) {
      std::cout << "received id " << icp->icmp_id << std::endl;
      return;
    }

    std::cout
      << cc << " bytes from "
      << inet_ntoa(from_addr.sin_addr)
      << ": icmp_seq=" << icp->icmp_seq;
  } else {
    std::cout << "Recv: not an echo reply" << std::endl;
    return;
  }

  gettimeofday(&end, NULL);
  int time_diff = 1000000 * (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec);

  std::cout << " time=" << time_diff << " us" << std::endl;
}

uint16_t Pinger::check_sum(uint16_t *addr, unsigned len) {
  uint16_t answer = 0;
  uint32_t sum = 0;

  while (len > 1) {
    sum += *addr++;
    len -= 2;
  }

  if (len == 1) {
    *(unsigned char*)&answer = *(unsigned char*)addr;
    sum += answer;
  }

  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  answer = ~sum;

  return answer;
}

int main(int argc, char** argv) {
  cxxopts::Options options("test", "A brief description");

  options.parse_positional({"hostname"});

  options.add_options()
    ("hostname", "Host to ping", cxxopts::value<std::string>())
    ("p,port", "Ping specific port", cxxopts::value<int>()->default_value("7"))
    ("h,help", "Print usage")
  ;

  auto result = options.parse(argc, argv);  
  std::string hostname = result["hostname"].as<std::string>();
  int port = result["port"].as<int>();

  Pinger instance(hostname, port);
  instance.ping();

  std::cout << "OK pinging host" << std::endl;
  return 0;
}