// Flex OS Ultra · hora por red: el transporte UDP (sockets BSD de lwIP; en el PC, los de POSIX para las pruebas). Un socket por consulta, que se
// cierra pase lo que pase: ni un descriptor queda vivo entre intentos. Nunca se llama desde la tarea de la interfaz.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "flex_ntp.h"

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

// Consulta UN servidor. Devuelve la epoca UTC o 0 si no contesta. Solo se aceptan datagramas que vengan de la direccion y el puerto a los que se
// pregunto (Arduino aceptaba el de cualquiera) y que cumplan flex_ntp_reply_sane.
uint32_t flex_ntp_udp_query(const char *host, int port, uint32_t timeout_ms)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char ps[8];
    snprintf(ps, sizeof(ps), "%d", port);
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) {
        return 0;
    }
    uint32_t got = 0;
    int fd = socket(res->ai_family, res->ai_socktype, 0);   // puerto local efimero
    if (fd >= 0) {
        uint8_t pkt[FLEX_NTP_PKT_SIZE];
        flex_ntp_build_packet(pkt);
        if (sendto(fd, pkt, sizeof(pkt), 0, res->ai_addr, res->ai_addrlen) == (ssize_t)sizeof(pkt)) {
            const struct sockaddr_in *dst = (const struct sockaddr_in *)res->ai_addr;
            uint64_t t0 = mono_ms();
            for (;;) {
                uint64_t el = mono_ms() - t0;
                if (el >= timeout_ms) {
                    break;
                }
                uint64_t left = timeout_ms - el;
                struct timeval tv = {.tv_sec = (time_t)(left / 1000u), .tv_usec = (suseconds_t)((left % 1000u) * 1000u)};
                fd_set rd;
                FD_ZERO(&rd);
                FD_SET(fd, &rd);
                int r = select(fd + 1, &rd, NULL, NULL, &tv);
                if (r <= 0) {
                    break;   // tiempo agotado (o error del socket)
                }
                uint8_t buf[128];
                struct sockaddr_in from;
                socklen_t fl = sizeof(from);
                ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
                if (n < 0) {
                    break;
                }
                if (from.sin_addr.s_addr != dst->sin_addr.s_addr || from.sin_port != dst->sin_port) {
                    continue;   // de otro: se sigue esperando al de verdad
                }
                if (n < FLEX_NTP_PKT_SIZE) {
                    continue;   // un datagrama corto no es una respuesta
                }
                if (flex_ntp_reply_sane(buf, (int)n)) {
                    got = flex_ntp_parse_reply(buf, (int)n);
                }
                break;   // la primera respuesta completa cierra la espera, valga o no (como en Arduino)
            }
        }
        close(fd);   // cierre incondicional del socket
    }
    freeaddrinfo(res);
    return got;
}
