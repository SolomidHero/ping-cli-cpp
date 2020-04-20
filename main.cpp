#include "cxxopts/include/cxxopts.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <limits>
#include <chrono>
#include <thread>

#include <unistd.h>
#include <errno.h>
#include <exception>
#include <signal.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

void error(std::string msg) {
  if (errno != 0) {
    std::cerr << strerror(errno) << std::endl;
  }
  std::cerr << msg << std::endl;
  exit(1);
}

sig_atomic_t volatile stop_criteria = 0;

void sig_to_exception(int signal_num) {
  if (stop_criteria) {
    exit(1);
  }
  stop_criteria = 1;
}


class Pinger {
 public:
  Pinger(const std::string& hostname, int port, float sleep_time);
  ~Pinger() {
    close(sockfd);
  }

  void ping();
  void info(std::ostream& os);

 private:
  uint16_t check_sum(uint16_t* addr, unsigned len);
  void update_stats(double time_diff);

 private:
  static int MAXIPLEN;
  static int MAXICMPLEN;

  int datalen = 56;
  int cc = 64;
  int sequence_num = 0;
  int recieved_num = 0;
  int sleep_interval;

  std::string hostname;
  int port;

  double sum_time = 0;
  double sq_sum_time = 0;
  double min_time = std::numeric_limits<double>::max();
  double max_time = 0;

  int sockfd;
  fd_set fd_mask;
  struct hostent* host;
  struct sockaddr_in ping_addr;
};

int Pinger::MAXIPLEN = 60;
int Pinger::MAXICMPLEN = 76;

Pinger::Pinger(const std::string& hostname, int port, float sleep_time)
    : hostname(hostname), port(port), sleep_interval(sleep_time) {

  int socket_upd_param = 1;
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
  auto t_start = std::chrono::high_resolution_clock::now();

  struct icmp icp = {
    .icmp_type = ICMP_ECHO,
    .icmp_code = 0,
    .icmp_cksum = 0,
    .icmp_seq = static_cast<n_short>(sequence_num++),
    .icmp_id = static_cast<n_short>(getpid()),
  };
  icp.icmp_cksum = check_sum((uint16_t*)&icp, cc);

  int send_result =
    sendto(sockfd, (char*)&icp, cc, 0, (struct sockaddr*)&ping_addr, (socklen_t)sizeof(struct sockaddr_in));
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

  FD_ZERO(&fd_mask);
  FD_SET(sockfd, &fd_mask);

  if(select(sockfd + 1, &fd_mask, NULL, NULL, &timeout) == 0) {
    std::cout << "No socket available" << std::endl;
    return;
  }

  struct sockaddr_in from_addr = {};
  int from_len = sizeof(from_addr);
  int packlen = datalen + MAXIPLEN + MAXICMPLEN;
  char packet[packlen];
  int recv_result = recvfrom(sockfd, packet, packlen, 0, (struct sockaddr*)&from_addr, (socklen_t*)&from_len);
  if (recv_result < 0) {
    error("ERROR, couldn't recvfrom()");
  }

  struct ip* ip = (struct ip*)packet;
  if (recv_result < (sizeof(struct ip) + ICMP_MINLEN)) { 
    error("ERROR, packet too short (" + std::to_string(recv_result) + " bytes) from " + hostname);
  } 

	int hlen = sizeof(struct ip);
  struct icmp* icp_from = (struct icmp*)(packet + hlen);
  if (icp_from->icmp_type == ICMP_ECHOREPLY) {
    if (icp_from->icmp_id != icp.icmp_id) {
      error("ERROR received wrong id, id=" + std::to_string(icp_from->icmp_id));
    }

    std::cout
      << cc << " bytes from "
      << inet_ntoa(from_addr.sin_addr)
      << ": icmp_seq=" << icp_from->icmp_seq;
  } else {
    std::cerr << "recieved not an echo reply" << std::endl;
    return;
  }

  recieved_num++;
  auto t_end = std::chrono::high_resolution_clock::now();
  auto time_diff = std::chrono::duration<double, std::milli>(t_end - t_start).count();
  update_stats (time_diff);

  std::cout
    << std::setprecision(3) << std::fixed
    << " time=" << time_diff
    << "ms" << std::endl;
}

void Pinger::info(std::ostream& os=std::cout) {
	os
    << std::setprecision(3) << std::fixed
    << "----" << hostname << " PING Statistics----" << std::endl
	  << sequence_num << " packets transmitted, "
    << recieved_num << " packets received, "
    << 100 - 100 * recieved_num / sequence_num << "\% packet loss" << std::endl
    << "round-trip min/avg/max/stddev = "
    << min_time << "/" << sum_time / recieved_num << "/"
    << max_time << "/" << sqrt(sq_sum_time / recieved_num - pow(sum_time, 2) / pow(recieved_num, 2)) << " ms" << std::endl;
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

void Pinger::update_stats(double time_diff) {
  if (time_diff < min_time) {
    min_time = time_diff;
  }
  if (time_diff > max_time) {
    max_time = time_diff;
  }
  
  sum_time += time_diff;
  sq_sum_time += time_diff * time_diff;
}

int main(int argc, char** argv) {
  cxxopts::Options options("./ping-cli", "Send ICMP requests to provided host");

  options.add_options()
    ("hostname", "Host to ping", cxxopts::value<std::string>())
    ("p,port", "Ping specific port", cxxopts::value<int>()->default_value("7"))
    ("i,interval", "Time interval between pings", cxxopts::value<float>()->default_value("1.0"))
    ("c,count", "Max number of packets to transmit", cxxopts::value<int>())
    ("h,help", "Print usage")
  ;

  options.parse_positional({"hostname"});

  auto result = options.parse(argc, argv);  

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  std::string hostname = result["hostname"].as<std::string>();
  int port = result["port"].as<int>();
  float interval = result["interval"].as<float>();
  int max_transmitted = std::numeric_limits<int>::max();
  if (result.count("count")) {
    max_transmitted = result["count"].as<int>();
  }
  if (max_transmitted < 0) {
    error("invalid count of packets to transmit: " + std::to_string(max_transmitted));
  }

  Pinger instance(hostname, port, interval);

  struct sigaction sigIntHandler = {
    .sa_handler = sig_to_exception,
    .sa_flags = 0,
  };
  sigaction(SIGINT, &sigIntHandler, NULL);

  int transmitted = 0;
  while (!stop_criteria && transmitted < max_transmitted) {
    instance.ping();
    transmitted++;
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(interval * 1000)));
  }
  instance.info();

  return 0;
}