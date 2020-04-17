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
sudo ./ping-cli [-p port] host
```
