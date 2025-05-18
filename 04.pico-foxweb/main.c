#include "httpd.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <openssl/md5.h>
#include <time.h>
#include <hiredis/hiredis.h>

#define CHUNK_SIZE 1024 // read 1024 bytes at a time

#define PUBLIC_DIR "/var/www/picofoxweb/webroot"
#define INDEX_HTML "/index.html"
#define NOT_FOUND_HTML "/404.html"
#define AUTH_HTML "/private/index.html"

#define DIGEST_REALM "FoxWebAuth"

int logRequest(char* ip, char* date, char* method, char* uri, char* prot, int status, size_t bytes);

void md5_hex(const char *str, char *output) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)str, strlen(str), digest);
    for (int i = 0; i < 16; ++i)
        sprintf(&output[i*2], "%02x", (unsigned int)digest[i]);
    output[32] = 0;
}

void gen_nonce(char *out, size_t len) {
    snprintf(out, len, "%lx%lx", (unsigned long)time(NULL), (unsigned long)random());
}


int get_password_from_redis(const char *user, char *password, size_t maxlen) {
    redisContext *c = redisConnect("127.0.0.1", 6379);
    if (c == NULL || c->err) {
        if (c) fprintf(stderr, "Redis error: %s\n", c->errstr);
        return 0;
    }
    redisReply *reply = redisCommand(c, "GET user:%s", user);
    int res = 0;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        strncpy(password, reply->str, maxlen-1);
        password[maxlen-1] = 0;
        res = 1;
    }
    if (reply) freeReplyObject(reply);
    redisFree(c);
    return res;
}

int main(int c, char **v) {
    char *port = c == 1 ? "8000" : v[1];
    serve_forever(port);
    return 0;
}

int file_exists(const char *file_name) {
    struct stat buffer;
    int exists;
    exists = (stat(file_name, &buffer) == 0);
    return exists;
}

int read_file(const char *file_name, int* size) {
    char buf[CHUNK_SIZE];
    FILE *file;
    size_t nread;
    int err = 1;
    int currentSize = 0;

    file = fopen(file_name, "r");

    if (file) {
        while ((nread = fread(buf, 1, sizeof buf, file)) > 0) {
            fwrite(buf, 1, nread, stdout);
            currentSize += nread;
        }
        err = ferror(file);
        fclose(file);
    }
    *size = currentSize;
    return err;
}

void route(char* dateTime, char* httpRequestType, char* clientIp, char* prot) {
    int code = 0;
    int dataSize = 0;

    ROUTE_START()

    GET("/") {
        char index_html[255];
        sprintf(index_html, "%s%s", PUBLIC_DIR, INDEX_HTML);

        HTTP_200;
        if (file_exists(index_html)) {
            read_file(index_html, &dataSize);
        } else {
            printf("Hello! You are using %s\n\n", request_header("User-Agent"));
        }
        code = 200;
    }

    GET("/test") {
        HTTP_200;
        printf("List of request headers:\n\n");
        header_t *h = request_headers();
        while (h->name) {
            printf("%s: %s\n", h->name, h->value);
            h++;
        }
        code = 200;
    }

    POST("/") {
        HTTP_201;
        printf("Wow, seems that you POSTed %d bytes.\n", payload_size);
        printf("Fetch the data using `payload` variable.\n");
        if (payload_size > 0)
            printf("Request body: %s", payload);
        code = 201;
    }

    GET("/auth") {
        char *auth = request_header("Authorization");
        int must_authenticate = 1;
        char nonce[64] = {0};
        gen_nonce(nonce, sizeof(nonce));
        char user[128] = {0}, realm[256] = {0}, uri[512] = {0}, response[128] = {0};
        char nonce_recv[256] = {0}, qop[64] = {0}, nc[32] = {0}, cnonce[128] = {0};

        if (auth && strncmp(auth, "Digest ", 7) == 0) {
            char auth_copy[1024] = {0};
            strncpy(auth_copy, auth+7, sizeof(auth_copy)-1); // Skip "Digest "
            char *tok = strtok(auth_copy, ",");
            while (tok) {
                char *key = NULL, *val = NULL;
                while (*tok == ' ') tok++;
                key = tok;
                val = strchr(tok, '=');
                if (val) {
                    *val = 0;
                    ++val;
                    if (*val == '"') {
                        ++val;
                        char *endq = strchr(val, '"');
                        if (endq) *endq = 0;
                    }
                }
                if (strcmp(key, "username") == 0) strncpy(user, val, sizeof(user)-1);
                else if (strcmp(key, "realm") == 0) strncpy(realm, val, sizeof(realm)-1);
                else if (strcmp(key, "nonce") == 0) strncpy(nonce_recv, val, sizeof(nonce_recv)-1);
                else if (strcmp(key, "uri") == 0) strncpy(uri, val, sizeof(uri)-1);
                else if (strcmp(key, "response") == 0) strncpy(response, val, sizeof(response)-1);
                else if (strcmp(key, "qop") == 0) strncpy(qop, val, sizeof(qop)-1);
                else if (strcmp(key, "nc") == 0) strncpy(nc, val, sizeof(nc)-1);
                else if (strcmp(key, "cnonce") == 0) strncpy(cnonce, val, sizeof(cnonce)-1);
                tok = strtok(NULL, ",");
            }

            // ищем пользователя в Redis
            char password[128] = {0};
            int user_found = get_password_from_redis(user, password, sizeof(password));

            if (user_found &&
                strcmp(realm, DIGEST_REALM) == 0 &&
                strcmp(uri, "/auth") == 0
            ) {
                char a1[1024], ha1[33];
                snprintf(a1, sizeof(a1), "%s:%s:%s", user, DIGEST_REALM, password);
                md5_hex(a1, ha1);

                char a2[1024], ha2[33];
                snprintf(a2, sizeof(a2), "GET:/auth");
                md5_hex(a2, ha2);

                char resp_calc[2048], valid_response[33];
                snprintf(resp_calc, sizeof(resp_calc), "%s:%s:%s:%s:%s:%s",
                    ha1, nonce_recv, nc, cnonce, qop, ha2);
                md5_hex(resp_calc, valid_response);

                if (strcmp(response, valid_response) == 0) {
                    must_authenticate = 0;
                }
            }
        }

        if (must_authenticate) {
            printf("HTTP/1.1 401 Unauthorized\r\n");
            printf("WWW-Authenticate: Digest realm=\"%s\",nonce=\"%s\",qop=\"auth\",algorithm=MD5\r\n", DIGEST_REALM, nonce);
            printf("Content-Type: text/html\r\n\r\n");
            printf("<html><body><h1>Authentication Required</h1></body></html>");
            fflush(stdout);
            code = 401;
        } else {
            char auth_html[255];
            sprintf(auth_html, "%s%s", PUBLIC_DIR, AUTH_HTML);
            HTTP_200;
            if (file_exists(auth_html)) {
                read_file(auth_html, &dataSize);
            } else {
                printf("auth.html not found\n");
            }
            code = 200;
        }
    }

    GET(uri) {
        char file_name[255];
        sprintf(file_name, "%s%s", PUBLIC_DIR, uri);

        if (file_exists(file_name)) {
            HTTP_200;
            read_file(file_name, &dataSize);
            code = 200;
        } else {
            HTTP_404;
            sprintf(file_name, "%s%s", PUBLIC_DIR, NOT_FOUND_HTML);
            if (file_exists(file_name))
                read_file(file_name, &dataSize);
            code = 404;
        }
    }

    ROUTE_END();

    logRequest(clientIp, dateTime, httpRequestType, uri, prot, code, dataSize);
}

int logRequest(char* ip, char* date, char* method, char* uri, char* prot, int status, size_t bytes) {
    FILE *myLog = fopen("var/log/foxweb.log", "a");
    if (myLog == NULL) {
        perror("Не удалось открыть файл лога");
        return -1; 
    }
    fprintf(myLog, "%s - - [%s] \"%s %s %s\" %d %zu \"\n",
                    ip, date, method, uri, prot, status, bytes);
    fclose(myLog);
    return 0;
}
