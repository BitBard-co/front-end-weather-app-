#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#define PORT 8080
#define BACKLOG 16
#define BUF_SIZE 8192

// A very small in-memory "database" of cities.
typedef struct {
    const char *city;
    const char *country;
    double lat;
    double lon;
} City;

static City DEMO_CITIES[] = {
    {"Stockholm", "SE", 59.3293, 18.0686},
    {"Orebro", "SE", 59.2741, 15.2066},
    {"Malmo", "SE", 55.6050, 13.0038},
    {"Gothenburg", "SE", 57.7089, 11.9746},
    {"Uppsala", "SE", 59.8586, 17.6389}
};

// Build a simple JSON error message.
static const char *json_error(int code, const char *message) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "{\"error\":{\"code\":%d,\"message\":\"%s\"}}", code, message);
    return buf;
}

// Send a basic HTTP response with CORS headers.
static void write_response(int client_fd, int status_code, const char *status_text, const char *content_type, const char *body) {
    char header[1024];
    int content_length = body ? (int)strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, content_length);
    send(client_fd, header, n, 0);
    if (body && content_length > 0) {
        send(client_fd, body, content_length, 0);
    }
}

// Respond to OPTIONS preflight.
static void write_options_ok(int client_fd) {
    write_response(client_fd, 204, "No Content", "text/plain", "");
}

// Very small URL-decoder (handles %xx and '+'). Modifies the string in-place.
static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++, o++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o = (char) strtol(hex, NULL, 16);
            s += 2;
        } else if (*s == '+') {
            *o = ' ';
        } else {
            *o = *s;
        }
    }
    *o = '\0';
}

// Read a key=value from the URL query string. Returns 1 if found.
static int parse_query_param(const char *query, const char *key, char *out, size_t outlen) {
    if (!query) return 0;
    size_t keylen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        if (!eq) break;
        size_t klen = (size_t)(eq - p);
        if (klen == keylen && strncmp(p, key, keylen) == 0) {
            size_t vlen = amp ? (size_t)(amp - eq - 1) : strlen(eq + 1);
            if (vlen >= outlen) vlen = outlen - 1;
            strncpy(out, eq + 1, vlen);
            out[vlen] = '\0';
            url_decode(out);
            return 1;
        }
        p = amp ? amp + 1 : NULL;
    }
    return 0;
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Parse the first request line: METHOD PATH HTTP/1.1
// Fills method, path (without query), and sets *query to the part after '?'
static int parse_request_line(char *buf, char *method, size_t mlen, char *path, size_t plen, char **query) {
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) return 0;
    *line_end = '\0';

    char *sp1 = strchr(buf, ' ');
    if (!sp1) return 0;
    size_t m = (size_t)(sp1 - buf);
    if (m >= mlen) m = mlen - 1;
    strncpy(method, buf, m);
    method[m] = '\0';

    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return 0;
    size_t pathlen = (size_t)(sp2 - (sp1 + 1));
    if (pathlen >= plen) pathlen = plen - 1;
    strncpy(path, sp1 + 1, pathlen);
    path[pathlen] = '\0';

    char *q = strchr(path, '?');
    if (q) {
        *q = '\0';
        *query = q + 1;
    } else {
        *query = NULL;
    }
    return 1;
}

// Find a city by exact (case-sensitive) name.
static const City* find_city_by_name(const char *name) {
    size_t n = sizeof(DEMO_CITIES) / sizeof(DEMO_CITIES[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(DEMO_CITIES[i].city, name) == 0) return &DEMO_CITIES[i];
    }
    return NULL;
}

// Find a city whose coordinates are "close" to given lat/lon.
static const City* find_city_by_coords(double lat, double lon) {
    size_t n = sizeof(DEMO_CITIES) / sizeof(DEMO_CITIES[0]);
    for (size_t i = 0; i < n; i++) {
        if (fabs(DEMO_CITIES[i].lat - lat) < 0.01 && fabs(DEMO_CITIES[i].lon - lon) < 0.01) {
            return &DEMO_CITIES[i];
        }
    }
    return NULL;
}

// Get current UTC time as a simple ISO-8601 string.
static void iso8601_utc_now(char *out, size_t outlen) {
    time_t t = time(NULL);
    struct tm g;
    g = *gmtime(&t);
    strftime(out, outlen, "%Y-%m-%dT%H:%M:%SZ", &g);
}

// Handle /api/v1/geo?city=NAME
static void handle_geo(int client_fd, const char *query) {
    char city[128] = {0};
    if (!parse_query_param(query, "city", city, sizeof(city))) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "missing query param: city"));
        return;
    }
    const City *c = find_city_by_name(city);
    if (!c) {
        write_response(client_fd, 404, "Not Found", "application/json", json_error(404, "city not found"));
        return;
    }
    char body[256];
    snprintf(body, sizeof(body), "{\"city\":\"%s\",\"country\":\"%s\",\"lat\":%.4f,\"lon\":%.4f}", c->city, c->country, c->lat, c->lon);
    write_response(client_fd, 200, "OK", "application/json", body);
}

// Handle /api/v1/weather?lat=X&lon=Y
static void handle_weather(int client_fd, const char *query) {
    char lat_s[64] = {0};
    char lon_s[64] = {0};
    if (!parse_query_param(query, "lat", lat_s, sizeof(lat_s)) || !parse_query_param(query, "lon", lon_s, sizeof(lon_s))) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "missing query params: lat, lon"));
        return;
    }
    double lat = atof(lat_s);
    double lon = atof(lon_s);
    const City *c = find_city_by_coords(lat, lon);
    char updated[64];
    iso8601_utc_now(updated, sizeof(updated));

    // Very simple demo weather: change numbers based on city when we can.
    double tempC = 7.0;
    const char *desc = "Cloudy";
    if (c) {
        if (strcmp(c->city, "Malmo") == 0) { tempC = 10.5; desc = "Sunny"; }
        else if (strcmp(c->city, "Gothenburg") == 0) { tempC = 8.2; desc = "Windy"; }
        else if (strcmp(c->city, "Orebro") == 0) { tempC = 6.3; desc = "Overcast"; }
    }

    char body[256];
    snprintf(body, sizeof(body), "{\"tempC\":%.1f,\"description\":\"%s\",\"updatedAt\":\"%s\"}", tempC, desc, updated);
    write_response(client_fd, 200, "OK", "application/json", body);
}

// Route the request based on path and method.
static void handle_request(int client_fd, char *buf) {
    char method[16];
    char path[256];
    char *query = NULL;

    if (!parse_request_line(buf, method, sizeof(method), path, sizeof(path), &query)) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "invalid request line"));
        return;
    }

    // Allow CORS preflight
    if (strcmp(method, "OPTIONS") == 0) {
        write_options_ok(client_fd);
        return;
    }

    // We only support GET for simplicity
    if (strcmp(method, "GET") != 0) {
        write_response(client_fd, 405, "Method Not Allowed", "application/json", json_error(405, "method not allowed"));
        return;
    }

    if (starts_with(path, "/api/v1/geo")) {
        handle_geo(client_fd, query);
    } else if (starts_with(path, "/api/v1/weather")) {
        handle_weather(client_fd, query);
    } else {
        write_response(client_fd, 404, "Not Found", "application/json", json_error(404, "not found"));
    }
}

int main(void) {
    // 1) Create a TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    // 2) Allow quick restart during development
    int opt = 1;
    (void)setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3) Bind to 0.0.0.0:PORT
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // 4) Start listening
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Weather API server running on http://localhost:%d\n", PORT);

    // 5) Main loop: accept, read one request, respond, close
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char buf[BUF_SIZE];
        ssize_t r = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (r > 0) {
            buf[r] = '\0';
            handle_request(client_fd, buf);
        }
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
