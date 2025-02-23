#include "httpd.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define CHUNK_SIZE 1024 // read 1024 bytes at a time

// Public directory settings
#define PUBLIC_DIR "/var/www/picofoxweb/webroot"
#define INDEX_HTML "/index.html"
#define NOT_FOUND_HTML "/404.html"

int logRequest(char* ip, char* date, char* method, char* uri, char* prot, int status, size_t bytes);

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
    char index_html[20];
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


    // Запись в Combined Log Format
    fprintf(myLog, "%s - - [%s] \"%s %s %s\" %d %zu \"\n",
                        ip, date, method, uri, prot, status, bytes);
    fclose(myLog);

    return 0;
}
