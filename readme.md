## Ping command-line interface

This is c++ application which has linux ping like functionality. Better see ping man page.

### Clone repo

```
# ssh or https
git clone git@github.com:SolomidHero/ping-cli-cpp.git 
git submodule init && git submodule update
```

### Build and run

Use following commands: 
```
g++ -std=c++11 -o  main.cpp ping-cli
sudo ./ping-cli [-p port] [-i seconds] [-c max_packets] host
```

Also you can find usage of this app:
```
sudo ./ping-cli -h
# Send ICMP requests to provided host
# Usage:
#  ./ping-cli [OPTION...] positional parameters
#
#  -p, --port arg      Ping specific port (default: 7)
#  -i, --interval arg  Time interval between pings (default: 1.0)
#  -c, --count arg     Max number of packets to transmit
#  -h, --help          Print usage
```

