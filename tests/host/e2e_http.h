#pragma once
// #############################################################
//  PUENTE HTTP DE LAS PRUEBAS PUNTA A PUNTA
//  ------------------------------------------------------------
//  Una peticion por conexion (Connection: close) a 127.0.0.1:<puerto>,
//  cabeceras de la respuesta en minusculas y cuerpo "chunked" descodificado.
//  Lo usan cloud_e2e.cpp (servidor Node de Flex Cloud) y phone_e2e.cpp
//  (servidor Kotlin de Flex Cloud en el telefono).
// #############################################################
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <map>

static inline std::string e2eLower(std::string s){ for(auto& c : s) c = (char)tolower((unsigned char)c); return s; }

struct Raw { int status = -1; std::map<std::string, std::string> headers; std::string body; };
static inline Raw e2eHttp(int port, const std::string& method, const std::string& path, const std::map<std::string, std::string>& hdrs, const std::string& body){
  Raw r;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(fd, (sockaddr*)&a, sizeof(a)) != 0){ close(fd); return r; }
  std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nConnection: close\r\n";
  for(auto& h : hdrs){
    std::string k = e2eLower(h.first);
    if(k == "host" || k == "connection" || k == "content-length") continue;
    req += h.first + ": " + h.second + "\r\n";
  }
  if(!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  req += "\r\n";
  req += body;
  for(size_t off = 0; off < req.size();){ ssize_t w = send(fd, req.data() + off, req.size() - off, MSG_NOSIGNAL); if(w <= 0){ close(fd); return r; } off += (size_t)w; }
  std::string in; char buf[65536]; ssize_t n;
  while((n = recv(fd, buf, sizeof(buf), 0)) > 0) in.append(buf, (size_t)n);
  close(fd);
  size_t he = in.find("\r\n\r\n");
  if(he == std::string::npos) return r;
  std::string head = in.substr(0, he);
  std::string rest = in.substr(he + 4);
  size_t le = head.find("\r\n");
  r.status = atoi(head.substr(9, 3).c_str());
  size_t p = le;
  while(p != std::string::npos && p < head.size()){
    size_t s = p + 2, e = head.find("\r\n", s);
    std::string line = head.substr(s, e == std::string::npos ? std::string::npos : e - s);
    size_t c = line.find(':');
    if(c != std::string::npos){ std::string v = line.substr(c + 1); while(!v.empty() && v[0] == ' ') v.erase(0, 1); r.headers[e2eLower(line.substr(0, c))] = v; }
    p = e;
  }
  if(e2eLower(r.headers["transfer-encoding"]) == "chunked"){
    std::string out; size_t q = 0;
    for(;;){
      size_t e = rest.find("\r\n", q); if(e == std::string::npos) break;
      unsigned long len = strtoul(rest.substr(q, e - q).c_str(), nullptr, 16);
      if(!len) break;
      out += rest.substr(e + 2, len); q = e + 2 + len + 2;
    }
    r.body = out;
  } else r.body = rest;
  return r;
}

