/*
  Weather REST API (tiny C server) — Beginner-friendly walkthrough

  What this file does (for frontend developers):
  - Listens on http://localhost:8080
  - Exposes two GET endpoints under /api/v1
      1) /api/v1/geo?city=NAME
         → returns a small JSON object with city, country, lat, lon
      2) /api/v1/weather?lat=...&lon=...
         → returns { tempC, description, updatedAt }
  - Answers CORS preflight (OPTIONS) and sets CORS headers on responses
  - Uses only in-memory demo data (no external API calls)

  How to read this:
  - We use simple C and add comments for each step so you can follow the flow
  - If you just want to change behavior, look at the DEMO_CITIES array and
    the handle_geo / handle_weather functions.
  - To add a new endpoint, add a new handler and route it inside handle_request.

  Platform notes:
  - Uses POSIX sockets (Linux/macOS/WSL). For native Windows without WSL,
    you’d port these headers and calls to Winsock2.
*/

// Socket/network headers (Linux/WSL/macOS). Provide IPv4 types and functions.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
// Standard C headers for I/O, memory, strings, etc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <math.h>

// Configuration: network port, listen queue size, and max request buffer
#define PORT 8080
#define BACKLOG 16
#define BUF_SIZE 8192

// A very small in-memory "database" of cities we support for the demo.
typedef struct {
    const char *city;    // City display name, e.g., "Malmo"
    const char *country; // Two-letter country code, e.g., "SE"
    double lat;          // Latitude (decimal degrees)
    double lon;          // Longitude (decimal degrees)
} City;

// Our fixed test data. Feel free to add more entries here.
static City DEMO_CITIES[] = {
    {"Stockholm", "SE", 59.3293, 18.0686},
    {"Orebro", "SE", 59.2741, 15.2066},
    {"Malmo", "SE", 55.6050, 13.0038},
    {"Gothenburg", "SE", 57.7089, 11.9746},
    {"Uppsala", "SE", 59.8586, 17.6389}
};

// Build a simple JSON error message as a string.
// Example: json_error(404, "not found") → "{\"error\":{\"code\":404,\"message\":\"not found\"}}"
static const char *json_error(int code, const char *message) {
    static char buf[1024];             // static buffer reused per call (single-threaded server)
    snprintf(buf, sizeof(buf),
             "{\"error\":{\"code\":%d,\"message\":\"%s\"}}",
             code, message);
    return buf;                         // caller uses it immediately to send the response
}

// Send a basic HTTP response with CORS headers.
// - status_code / status_text: e.g., 200 "OK"
// - content_type: e.g., "application/json"
// - body: the response payload as a C string
static void write_response(int client_fd, int status_code, const char *status_text, const char *content_type, const char *body) {
    char header[1024];                         // buffer for response header lines
    int content_length = body ? (int)strlen(body) : 0; // byte length of body
    // Build the HTTP response header with common CORS headers for browser access
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, content_length);
    send(client_fd, header, n, 0);             // send header first
    if (body && content_length > 0) {
        send(client_fd, body, content_length, 0); // then send body if present
    }
}

// Respond to OPTIONS preflight (no body, 204 No Content)
static void write_options_ok(int client_fd) {
    write_response(client_fd, 204, "No Content", "text/plain", "");
}

// Very small URL-decoder (handles %xx and '+'). Modifies the string in-place.
// Example: "Malmo%20City" → "Malmo City"; "+" becomes space as well.
static void url_decode(char *s) {
    char *o = s;                           // output pointer writes back onto the same string
    for (; *s; s++, o++) {                  // walk the input string, writing to output
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 }; // two hex digits after '%'
            *o = (char) strtol(hex, NULL, 16); // convert hex to a single byte
            s += 2;                           // skip the two hex digits we just consumed
        } else if (*s == '+') {               // '+' represents space in URL encoding
            *o = ' ';
        } else {
            *o = *s;                          // normal character, copy as-is
        }
    }
    *o = '\0';                              // null-terminate the decoded string
}

// Read a key=value from the URL query string. Returns 1 if found.
// Example: query="city=Malmo&x=1", key="city" → writes "Malmo" to out
static int parse_query_param(const char *query, const char *key, char *out, size_t outlen) {
    if (!query) return 0;                   // no query string at all
    size_t keylen = strlen(key);
    const char *p = query;                  // scanning pointer
    while (p && *p) {                       // loop over key=value pairs separated by '&'
        const char *eq = strchr(p, '=');    // find '=' between key and value
        const char *amp = strchr(p, '&');   // find next '&' (end of this pair)
        if (!eq) break;                     // no '=', stop
        size_t klen = (size_t)(eq - p);     // length of the key
        if (klen == keylen && strncmp(p, key, keylen) == 0) {
            size_t vlen = amp ? (size_t)(amp - eq - 1) : strlen(eq + 1); // compute value length
            if (vlen >= outlen) vlen = outlen - 1;                        // clamp to buffer size
            strncpy(out, eq + 1, vlen);                                   // copy value substring
            out[vlen] = '\0';                                            // add terminator
            url_decode(out);                                              // decode %xx and '+'
            return 1;                                                     // success
        }
        p = amp ? amp + 1 : NULL;      // move to next pair or stop if none
    }
    return 0;                            // not found
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Parse the first request line: METHOD PATH HTTP/1.1
// Fills method, path (without query), and sets *query to the part after '?'
static int parse_request_line(char *buf, char *method, size_t mlen, char *path, size_t plen, char **query) {
    char *line_end = strstr(buf, "\r\n");   // request line ends at CRLF
    if (!line_end) return 0;                 // malformed if no CRLF
    *line_end = '\0';                        // temporarily terminate the line for parsing

    char *sp1 = strchr(buf, ' ');            // first space separates METHOD and PATH
    if (!sp1) return 0;
    size_t m = (size_t)(sp1 - buf);          // length of METHOD
    if (m >= mlen) m = mlen - 1;             // clamp to output buffer
    strncpy(method, buf, m);                  // copy METHOD
    method[m] = '\0';                        // terminate METHOD string

    char *sp2 = strchr(sp1 + 1, ' ');        // second space separates PATH and HTTP version
    if (!sp2) return 0;
    size_t pathlen = (size_t)(sp2 - (sp1 + 1)); // length of PATH + optional query
    if (pathlen >= plen) pathlen = plen - 1;    // clamp
    strncpy(path, sp1 + 1, pathlen);            // copy PATH?QUERY
    path[pathlen] = '\0';                      // terminate PATH string

    char *q = strchr(path, '?');             // split on '?': path vs query
    if (q) {
        *q = '\0';                          // end the PATH here
        *query = q + 1;                      // point to query substring
    } else {
        *query = NULL;                       // no query provided
    }
    return 1;
}

// Find a city by exact (case-sensitive) name.
static const City* find_city_by_name(const char *name) {
    size_t n = sizeof(DEMO_CITIES) / sizeof(DEMO_CITIES[0]); // number of entries
    for (size_t i = 0; i < n; i++) {
        if (strcmp(DEMO_CITIES[i].city, name) == 0) return &DEMO_CITIES[i]; // found
    }
    return NULL; // not found
}

// Find a city whose coordinates are "close" to given lat/lon.
static const City* find_city_by_coords(double lat, double lon) {
    size_t n = sizeof(DEMO_CITIES) / sizeof(DEMO_CITIES[0]);
    for (size_t i = 0; i < n; i++) {
        // Treat coordinates as matching if both lat and lon are within ~0.01 degrees
        if (fabs(DEMO_CITIES[i].lat - lat) < 0.01 && fabs(DEMO_CITIES[i].lon - lon) < 0.01) {
            return &DEMO_CITIES[i];
        }
    }
    return NULL; // no nearby city
}

// Get current UTC time as a simple ISO-8601 string.
static void iso8601_utc_now(char *out, size_t outlen) {
    time_t t = time(NULL);                    // seconds since epoch
    struct tm g;                              // broken-out UTC time
    g = *gmtime(&t);                          // convert to UTC components
    strftime(out, outlen, "%Y-%m-%dT%H:%M:%SZ", &g); // format like 2025-11-03T12:34:56Z
}

// Handle /api/v1/geo?city=NAME — City → Coordinates
static void handle_geo(int client_fd, const char *query) {
    char city[128] = {0};                     // buffer for extracted city name
    if (!parse_query_param(query, "city", city, sizeof(city))) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "missing query param: city"));
        return;
    }
    // Limits: max city length 100 characters
    if (strlen(city) > 100) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "city too long (max 100)"));
        return;
    }
    const City *c = find_city_by_name(city);  // lookup in our demo data
    if (!c) {
        write_response(client_fd, 404, "Not Found", "application/json", json_error(404, "city not found"));
        return;
    }
    char body[256];                            // build the JSON response body
    snprintf(body, sizeof(body),
             "{\"city\":\"%s\",\"country\":\"%s\",\"lat\":%.4f,\"lon\":%.4f}",
             c->city, c->country, c->lat, c->lon);
    write_response(client_fd, 200, "OK", "application/json", body); // send 200 OK with JSON
}

// Handle /api/v1/weather?lat=X&lon=Y — Coordinates → Weather
static void handle_weather(int client_fd, const char *query) {
    char lat_s[64] = {0};                     // temp buffer for latitude string
    char lon_s[64] = {0};                     // temp buffer for longitude string
    if (!parse_query_param(query, "lat", lat_s, sizeof(lat_s)) || !parse_query_param(query, "lon", lon_s, sizeof(lon_s))) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "missing query params: lat, lon"));
        return;
    }
    double lat = atof(lat_s);                 // convert to floating point
    double lon = atof(lon_s);
    // Basic validation: valid Earth coordinate ranges
    if (!(lat >= -90.0 && lat <= 90.0)) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "lat out of range (-90..90)"));
        return;
    }
    if (!(lon >= -180.0 && lon <= 180.0)) {
        write_response(client_fd, 400, "Bad Request", "application/json", json_error(400, "lon out of range (-180..180)"));
        return;
    }
    const City *c = find_city_by_coords(lat, lon); // try to map to one of our demo cities
    char updated[64];                               // timestamp like 2025-11-03T..Z
    iso8601_utc_now(updated, sizeof(updated));      // fill with current UTC time

    // Very simple demo weather: change numbers based on city when we can.
    double tempC = 7.0;                     // default value
    const char *desc = "Cloudy";            // default description
    if (c) {
        if (strcmp(c->city, "Malmo") == 0) { tempC = 10.5; desc = "Sunny"; }
        else if (strcmp(c->city, "Gothenburg") == 0) { tempC = 8.2; desc = "Windy"; }
        else if (strcmp(c->city, "Orebro") == 0) { tempC = 6.3; desc = "Overcast"; }
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"tempC\":%.1f,\"description\":\"%s\",\"updatedAt\":\"%s\"}",
             tempC, desc, updated);
    write_response(client_fd, 200, "OK", "application/json", body); // send the weather JSON
}

// Route the request based on path and method.
static void handle_request(int client_fd, char *buf) {
    char method[16];     // will hold "GET" or "OPTIONS"
    char path[256];      // path like "/api/v1/geo" or "/api/v1/weather?lat=..."
    char *query = NULL;  // points inside 'path' after '?'

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
    // 1) Create a TCP socket (IPv4, stream oriented)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; } // if it fails, print reason and exit

    // 2) Allow quick restart during development
    int opt = 1;
    (void)setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // ignore return; best-effort

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
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len); // wait for a client
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char buf[BUF_SIZE];
        ssize_t r = recv(client_fd, buf, sizeof(buf) - 1, 0); // read raw HTTP request bytes
        if (r > 0) {
            buf[r] = '\0';               // turn bytes into a C string for simple parsing
            handle_request(client_fd, buf); // parse and send response
        }
        close(client_fd);                  // done with this client (one request per connection)
    }

    close(server_fd);                      // close the listening socket
    return 0;
}
