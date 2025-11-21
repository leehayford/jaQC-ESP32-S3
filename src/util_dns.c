#include "util_dns.h"
#include "util_err.h"

#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/inet.h"
#include "esp_log.h"


static const char *TAG = "UTIL_DNS";

static void dns_recv(
    void *arg, 
    struct udp_pcb *pcb, 
    struct pbuf *p, 
    const ip_addr_t *addr, 
    u16_t port
) {

    if (!p || p->len < sizeof(dns_header_t)) return;

    // Log raw DNS query /// LOG_INFO(TAG, "Received DNS query from %s:%d", ipaddr_ntoa(addr), port);

    // Cast payload to dns_header_t
    uint8_t *payload = (uint8_t *)p->payload;
    // dns_header_t *hdr = (dns_header_t *)payload;

    // TODO: REMOVE - extract domain name (very simplified)
    char domain[256] = {0};
    int i = 12, j = 0;
    while (i < p->len && payload[i] != 0 && j < sizeof(domain) - 1) {
        int len = payload[i++];
        for (int k = 0; k < len && i < p->len; k++) {
            domain[j++] = payload[i++];
        }
        domain[j++] = '.';
    }
    domain[j - 1] = '\0'; // Remove trailing dot // LOG_INFO(TAG, "Requested domain: %s", domain);

    // Copy query name and type/class
    // int query_len = p->len - sizeof(dns_header_t);
    // uint8_t *query = payload + sizeof(dns_header_t);

    // Build response
    struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, p->len + 16, PBUF_RAM); // +16 for answer
    if (!resp) {
        pbuf_free(p);
        return;
    }

    uint8_t *r = (uint8_t *)resp->payload;
    memcpy(r, payload, p->len); // Copy original query

    dns_header_t *resp_hdr = (dns_header_t *)r;
    resp_hdr->flags = htons(0x8180); // Standard response, no error
    resp_hdr->ancount = htons(1);    // One answer

    // Answer section starts after query
    int answer_offset = p->len;
    r[answer_offset++] = 0xC0; // Pointer to domain name
    r[answer_offset++] = 0x0C;

    r[answer_offset++] = 0x00; // Type A
    r[answer_offset++] = 0x01;

    r[answer_offset++] = 0x00; // Class IN
    r[answer_offset++] = 0x01;

    r[answer_offset++] = 0x00; // TTL
    r[answer_offset++] = 0x00;
    r[answer_offset++] = 0x00;
    r[answer_offset++] = 0x3C; // 60 seconds

    r[answer_offset++] = 0x00;
    r[answer_offset++] = 0x04; // IPv4 address length

    // IP address: 192.168.4.1
    r[answer_offset++] = 192;
    r[answer_offset++] = 168;
    r[answer_offset++] = 4;
    r[answer_offset++] = 1;

    udp_sendto(pcb, resp, addr, port);
    pbuf_free(resp);
    pbuf_free(p);

}

void start_dns_server() {
    struct udp_pcb *pcb = udp_new();
    
    if (!pcb) {
        LOG_ERR(TAG, ESP_FAIL, "failed to create udp pcb");
        return;
    }

    if (udp_bind(pcb, IP_ADDR_ANY, DNS_PORT) != ERR_OK) {
        LOG_ERR(TAG, ESP_FAIL, "failed to bind udp pcb to port %d", DNS_PORT);
        return;
    }

    udp_recv(pcb, dns_recv, NULL);
    LOG_INFO(TAG, "dns server started on port %d", DNS_PORT);
}


/* OLD STUFF ******************************************/

// #include "freertos/task.h"
// #include "lwip/sockets.h"

// // Very small DNS reply: copy ID/flags, one question/answer, wildcard A record to s_ip_be
// static int build_dns_response(const uint8_t *q, int qlen, uint8_t *a, int alen) {
//     if (qlen < 12) return 0; // header
//     // Find end of QNAME
//     int i = 12;
//     while (i < qlen && q[i] != 0) i += q[i] + 1;
//     if (i+5 >= qlen) return 0;
//     int qname_end = i + 1;
//     uint16_t qtype = (q[qname_end] << 8) | q[qname_end+1];
//     uint16_t qclass = (q[qname_end+2] << 8) | q[qname_end+3];

//     // Header
//     if (alen < qlen + 16) return 0;
//     memcpy(a, q, qlen);              // copy query
//     a[2] = 0x81; a[3] = 0x80;        // flags: standard response, no error
//     a[7] = 1;                        // ANCOUNT = 1
//     // Answer section
//     int p = qlen;
//     a[p++] = 0xC0; a[p++] = 0x0C;    // name pointer to QNAME
//     a[p++] = 0x00; a[p++] = 0x01;    // TYPE A
//     a[p++] = 0x00; a[p++] = 0x01;    // CLASS IN
//     a[p++] = 0x00; a[p++] = 0x00; a[p++] = 0x00; a[p++] = 0x1E; // TTL 30s
//     a[p++] = 0x00; a[p++] = 0x04;    // RDLENGTH = 4
//     memcpy(&a[p], &s_ip_be, 4); p += 4;
//     return p;
// }

// static void dns_task(void *arg) {
//     int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//     if (sock < 0) { vTaskDelete(NULL); return; }

//     struct sockaddr_in addr = {0};
//     addr.sin_family = AF_INET;
//     addr.sin_addr.s_addr = htonl(INADDR_ANY);
//     addr.sin_port = htons(53);
//     bind(sock, (struct sockaddr*)&addr, sizeof(addr));

//     uint8_t buf[512], out[512];
//     for (;;) {
//         struct sockaddr_in from; socklen_t flen = sizeof(from);
//         int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);
//         if (n <= 0) continue;
//         int m = build_dns_response(buf, n, out, sizeof(out));
//         if (m > 0) sendto(sock, out, m, 0, (struct sockaddr*)&from, flen);
//     }
// }

