#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/sendfile.h>

#define PORT 54321
#define MAXSIZE 100

// static char get_request[] =
//     "GET /index.html HTTP/1.0\r\n"
//     "Host: www-net.cs.umass.edu\r\n"
//     "User-Agent: Firefox/3.6.10\r\n"
//     "Accept: text/html,application/xhtml+xml\r\n"
//     "Accept-Language: en-us,en;q=0.5\r\n"
//     "Accept-Encoding: gzip,deflate\r\n"
//     "Accept-Charset: ISO-8859-1,utf-8;q=0.7\r\n"
//     "Keep-Alive: 115\r\n"
//     "\r\n"

static char get_request[] =
    /*"GET /examplepages/Example_Domain.html HTTP/1.1\r\n"
    "If-Modified-Since: Wed, 09 Feb 2005 15:17:28 GMT\r\n"
    "\r\n";
    */
    /*"GET /examplepages/Larry_Yueli_Zhang_-_Homepage.html HTTP/1.1\r\n"
    "If-Modified-Since: Wed, 09 Feb 2005 15:17:28 GMT\r\n"
    "\r\n";
    */
   /* file not exist
   "GET /doesnotexists.js HTTP/1.1\r\n"
    "If-Modified-Since: Wed, 09 Feb 2005 15:17:28 GMT\r\n"
    "\r\n";
    */
    /* precondition failed
    "GET /test.html HTTP/1.1\r\n"
    "If-Modified-Since: Wed, 09 Feb 2005 15:17:28 GMT\r\n"
    "If-Match: jkaljkldfslajlka\r\n"
    "\r\n";
    */
    /* precondition failed
    "GET /examplepages/test.html HTTP/1.1\r\n"
    "If-Modified-Since: Wed, 09 Feb 2005 15:17:28 GMT\r\n"
    "If-None-Match: jkaljkldfslajlka\r\n"
    "\r\n";
    */
   // test.html etag is a1af548052d42cca0a9cc19688d1c2a3, so we can check
   /* this should return 200 ok and the requested resource
    "GET /test.html HTTP/1.1\r\n"
    "If-Modified-Since: Wed, 09 Feb 2005 15:17:28 GMT\r\n"
    "If-None-Match: \"tagthatdoesnotexist\"\r\n"
    "\r\n";
    */
    /* this should return 304 not modified
    "GET /test.html HTTP/1.1\r\n"
    "If-None-Match: \"a1af548052d42cca0a9cc19688d1c2a3\"\r\n"
    "\r\n";
    */
    /* this should return 206 partial content with correct body
    "GET /test.html HTTP/1.1\r\n"
    "If-Range: \"a1af548052d42cca0a9cc19688d1c2a3\"\r\n"
    "\r\n";
    */
    /* this should return 200 ok with correct body
    "GET /test.html HTTP/1.1\r\n"
    "If-Range: \"fasdafafssdfaasdf\"\r\n"
    "\r\n";
    */
    ///* this should return 206 partial content with correct body
    "GET /test.html HTTP/1.1\r\n"
    "If-Range: Wed, 09 Feb 2005 15:17:28 EST\r\n"
    "\r\n";
    //*/
    /* this should return 200 ok with correct body
    "GET /test.html HTTP/1.1\r\n"
    "If-Range: Wed, 09 Feb 2025 15:17:28 EST\r\n"
    "\r\n";
    */

/*
GET /index.html HTTP/1.1
Connection: keep-alive
Keep-Alive: 10
*/

int main(int argc, char **argv){
    if (argc != 2){
        fprintf(stderr, "Usage: %s PORT\n", argv[0]);
        exit(1);
    }
    int client_soc = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    
    server.sin_family = AF_INET;
    memset(&server.sin_zero, 0, 8);
    server.sin_port = htons(atoi(argv[1]));

    struct addrinfo *result;
    getaddrinfo("127.0.0.1", NULL, NULL, &result);
    server.sin_addr = ((struct sockaddr_in *) result->ai_addr)->sin_addr;

    connect(client_soc, (struct sockaddr *)&server, sizeof(struct sockaddr_in));
    

    write(client_soc, get_request, 100);
    while (1){
        char buf[2049];
        int i;
        if ((i = read(client_soc, buf, 2048)) == 0){
            break;
        }
        printf("%s", buf);
        sleep(2);
    }

    return 0;
}