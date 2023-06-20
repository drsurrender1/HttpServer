#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define MAXSIZE 100
#define DATE_FORMAT "%a, %d %b %Y %X %Z"

// GET /test.html HTTP/1.0
// If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT
// If-Modified-Since: Wed, 21 Oct 2025 07:28:00 EST

/*
httperf --server localhost --port 11000 --uri /test.html --max-connections=1500000000 --num-conns=15000 --http-version=1.0 --burst-length=5 --method=GET
httperf --server localhost --port 11116 --uri /test.html --max-connections=1 --num-conns=1 --http-version=1.0 --burst-length=5 --method=GET
httperf --server localhost --port 11106 --uri /test.html --max-connections=1500 --num-conns=105 --http-version=1.0 --burst-length=1 --method=GET
*/

char* ext_abbrev[5] = {".txt", ".css", ".html", ".jpg", ".js"};
char* ext_name[5] = {"text/plain", "text/css", "text/html", "image/jpeg", "text/javascript"};

int flag_write;

/*
 * Generic usage print.
 */
void print_usage(char **argv) {
    fprintf(stderr, "Usage: %s -p PORT -d ROOT_PATH\n", argv[0]);
    exit(EXIT_FAILURE);
}

/*
 * return file name indicated in the path
 */
char* parse_path(const char* path){
    //printf("filepath=%s\n", path);
    char *name;
    char *temp = malloc(100*sizeof(char));
    const char s[2] = "/";
    char path_copy[strlen(path)+1];
    path_copy[strlen(path)] = '\0';
    strcpy(path_copy, path);
    name = strtok(path_copy, s);
    while (name!= NULL){
        //printf("name=%s\n", name);
        strcpy(temp, name);
        //printf("temp=%s\n", temp);
        name = strtok(NULL, s);
    }
    //printf("now temp=%s\n", temp);
    return temp;
}

/*
 * get file extension name
 */
char* get_file_extension(char* filename){
    char* extension = strrchr(filename, '.');
    printf("extension = %s\n", extension);
    if (extension == filename || !extension) {
        fprintf(stderr, "filename invalid\n");
        return ""; // file name invalid
    }
    printf("extension2 = %s\n", extension);
    printf("extension3 = %s\n", extension);
    for (int i = 0; i < 5; i++) {
        printf("[%s][%s]\n", extension, ext_abbrev[i]);
        if (strcasecmp(extension, ext_abbrev[i]) == 0) {
            printf("return\n");
            return ext_name[i];
        }
    }
    fprintf(stderr, "requested file has no extension\n");
    return NULL;
}

/*
 * give null pointer to time when already handled
 */
void handle_if_modified_since2(struct tm* time){
    char time_str[30];
    char* temp;
    time_t target_time;
    const char s[2] = " ";
    memset(time_str, '\0', 30);
    time_str[29] = '\0';
    for(int j = 0;j < 5;j++){
        temp = strtok(NULL, s);
        printf("temp  = %s\n", temp);
        strcat(time_str, temp);
        strcat(time_str, s);
        printf("now time_str = %s\n", time_str);
    }   
    strcat(time_str, "EST");
    printf("last time_str = %s\n", time_str);
    printf("length = %ld\n", strlen(time_str));
    strptime(time_str, DATE_FORMAT, time); //reads string time into struct tm time
    strtok(NULL, s);
    return;
}

/*
 * From struct tm to string time.
 */
void handle_if_modified_since(struct tm* time, char* time_str){
    strptime(time_str, DATE_FORMAT, time);
}

/*
 * wrapper function for write() with error checking.
 */
ssize_t write_string(int client_fd, char* string) {
    ssize_t bytes = write(client_fd, string, strlen(string));
    if (bytes < 1) {
        perror("write");
        flag_write = 1;
    }
    return bytes;
}

void handle_request(char* request, int client_fd, const char* root_path){
    char *bad_request = "HTTP/1.0 400 Bad Request\r\n\r\n";
    char *file_not_found = "HTTP/1.0 404 File Not Found\r\n\r\n";
    char *success_response = "HTTP/1.0 200 OK\r\n";
    char *server_error = "HTTP/1.0 500 Internal Server Error\r\n\r\n";
    char *not_modified = "HTTP/1.0 304 Not Modified\r\n\r\n";
    char *not_supported = "HTTP/1.0 505 HTTP Version Not Supported\r\n\r\n";
    char *path, *http_type;
    char current_time[80];
    const char s[2] = " ";
    struct tm target_time;
    double is_modified_after = 0;
    int flag_is_modified = 0;
    int flag_is_unmodified = 0;
    int flag_if_match = 0;
    int flag_if_nonmatch = 0;
    char last_modified_time[100];

    // if the request is not GET or if modified since, return bad_request to client and return
    const char *req = strtok(request, " ");
    int flag_GET = strcasecmp(req, "GET");
    if (flag_GET != 0) {
        write_string(client_fd, bad_request); //write(client_fd, bad_request, strlen(bad_request));
        return;
    }
    
    // nothing requested, return bad_request
    if ((path = strtok(NULL, " ")) == NULL) {
        write_string(client_fd, bad_request); //write(client_fd, bad_request, strlen(bad_request));
        return;
    }
    printf("path = %s\n", path);

    // if its not HTTP/1.0, return not_supported
    http_type = strtok(NULL, "\r\n");
    printf("http_type = %s\n", http_type);
    if ((strcasecmp(http_type, "HTTP/1.0")) != 0) {
        write_string(client_fd, not_supported);
        //write(client_fd, not_supported, strlen(not_supported));
        fprintf(stderr, "Not HTTP/1.0, Returning\n");
        return;
    }

    int path_len = strlen(path) + strlen(root_path) + 1;
    char* complete_path = (char* )malloc(path_len * sizeof(char));
    
    if(!complete_path){ // if malloc fails
        write_string(client_fd, server_error);
        //write(client_fd, server_error, sizeof(server_error));
        fprintf(stderr, "Not Enough Memory\n");
        return;
    }
    char *key, *value;
    char *modified_date = NULL;
    char *unmodified_date = NULL;
    char *etag_given = NULL;
    char *etag_given_none = NULL;

    while(1) { // for headers
        if ((key = strtok(NULL, " \t")) == NULL || (value = strtok(NULL, "\r\n")) == NULL){
            break;
        }
        else if (strcasecmp(key, "\nIf-Modified-Since:") == 0 || strcasecmp(key, "If-Modified-Since:") == 0) {
            modified_date = value;
            printf("if-Modified value = %s\n", value);
            flag_is_modified = 1;
            handle_if_modified_since(&target_time, modified_date);
        }
    }

    strcpy(complete_path, root_path);
    strncat(complete_path, path, path_len);
    printf("Requested File Path: %s\n", complete_path);

    int file_fd = open(complete_path, O_RDONLY);
    
    if ((file_fd == -1)) { // file not found
        fprintf(stderr, "File Not Found. Return.\n");
        write_string(client_fd, file_not_found); //write(client_fd, file_not_found, strlen(file_not_found));
        return;
    }
    // now file is found, ready to send it back to client
    printf("File Found\n");
    char servertime[100];
    time_t now = time(NULL);
    struct tm tmp;
    memcpy(&tmp, localtime(&now), sizeof(struct tm));
    strftime(servertime, sizeof(servertime) - 1, DATE_FORMAT, &tmp);
    // servertime is the time server start making response.
    struct stat filestat;
    int filesize;
    if (fstat(file_fd, &filestat) != 0) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }
    filesize = filestat.st_size;
    tmp = *localtime(&(filestat.st_mtime)); // tmp is struct tm of file's last modified time
    // recall above we put target time in target_time, so diff time can compare
    if(flag_is_modified == 1){
        is_modified_after = difftime(filestat.st_mtime, mktime(&target_time));
        // note: file is not midified after <-> target time > modified time <-> difftime < 0
        if(is_modified_after < 0){// when the file is not modified after
            // so we write not modified and exit.
            printf("File found but not modified after, returning\n");
            write_string(client_fd, not_modified);
            //write(client_fd, not_modified, sizeof(not_modified));
            close(client_fd);
            close(file_fd);
            return;
        }
    }
    strftime(last_modified_time, 100, DATE_FORMAT, &tmp);
    // now last_modified_time is ready to add to header
    char *content_type;
    char *header = malloc(512);
    printf("start parse_path\n");
    char* filename;
    filename = parse_path(complete_path);

    printf("start get_file_extension, filename = %s\n", filename);

    content_type = get_file_extension(filename);
    write_string(client_fd, success_response); //write(client_fd, success_response, strlen(success_response));
    sprintf(header, "Date: %s\r\nContent-Length: %d\r\nContent-Type: %s\r\nLast-Modified: %s\r\n\r\n",
            servertime, filesize, content_type, last_modified_time);
    write_string(client_fd, header); //write(client_fd, header, strlen(header));
    sendfile(client_fd, file_fd, NULL, filesize);
    close(file_fd);
    return;
}

int main(int argc, char **argv) {
    int port = 0;
    char dir[128] = "\0";

    if (argc != 5) print_usage(argv);
    int opt;
    while ((opt = getopt(argc, argv, "p:d:")) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'd':
            strncpy(dir, optarg, 127);
            break;
        default:
            print_usage(argv);
        }
    }
    if (port == 0 || strlen(dir) < 1) {
        fprintf(stderr, "Invalid port number or root path.");
        exit(EXIT_FAILURE);
    }
    printf("Port: %d\nRoot Path: %s\n", port, dir);
    
    int server_soc = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(addr.sin_zero), 0, 8);
    
    bind(server_soc, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

    if (listen(server_soc, 2) < 0){
        perror("listen");
        close(server_soc);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    memset(&(addr_in.sin_zero), 0, 8);
    unsigned int client_size = sizeof(struct sockaddr_in);

    while(1) {
        int client_fd = accept(server_soc, (struct sockaddr *) &addr_in, &client_size);
        if (client_fd == -1) {
            perror("accept");
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        printf("accepting...");
        char buf[1024];
        int n = read(client_fd, buf, 1024);
        if (n < 0)
            continue;
        if (n > 0) {
            flag_write = 0;
            handle_request(buf, client_fd, dir);
        }
        close(client_fd);
    }
    return 0;
}
