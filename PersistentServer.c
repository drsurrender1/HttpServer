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
#include "md5.c"

#define MAXSIZE 100
#define DATE_FORMAT "%a, %d %b %Y %X %Z"

// GET /test.html HTTP/1.1
// GET /UTMMN.jpg HTTP/1.1
// GET /examplepages/Larry_Yueli_Zhang_-_Homepage.html HTTP/1.1
// If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT
// If-Modified-Since: Wed, 21 Oct 2025 07:28:00 EST

/*
httperf --server localhost --port 6666 --uri /test.html --max-connections=1500000000 --num-conns=15000 --http-version=1.1 --burst-length=5 --method=GET

httperf --server localhost --port 11116 --uri /test.html --max-connections=5 --num-conns=5 --http-version=1.1 --burst-length=1 --method=GET
httperf --server localhost --port 11116 --uri /test.html --max-connections=1 --num-conns=1 --http-version=1.1 --burst-length=5 --method=GET
httperf --server localhost --port 6666 --uri /test.html --max-connections=1500 --num-conns=105 --http-version=1.1 --burst-length=1 --method=GET
*/

char* ext_abbrev[5] = {".txt", ".css", ".html", ".jpg", ".js"};
char* ext_name[5] = {"text/plain", "text/css", "text/html", "image/jpeg", "text/javascript"};

void print_usage(char **argv) {
    fprintf(stderr, "Usage: %s -p PORT -d ROOT_PATH\n", argv[0]);
    exit(EXIT_FAILURE);
}

// return etag without "" 
char* parse_etag(char* etag){
    char* temp = malloc(100*sizeof(char));
    strcpy(temp, etag);
    temp[strlen(etag)-1] = '\0';
    temp++;
    return temp;
}

// return file name indicated in the path
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

// get file extension name
char* get_file_extension(char *filename){
    char* extension = strrchr(filename, '.');
    printf("extension = %s\n", extension);
    if (extension == filename || !extension) {
        fprintf(stderr, "filename invalid\n");
        return ""; // file name invalid
    }
    printf("extension2 = %s\n", extension);
    //printf("file name is valid\n");
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


void handle_if_modified_since(struct tm* time, char* time_str){
    strptime(time_str, DATE_FORMAT, time);
}

void handle_request(char* request, int client_fd, const char *root_path){
    char *bad_request = "HTTP/1.1 400 Bad Request\r\n\r\n";
    char *file_not_found = "HTTP/1.1 404 File Not Found\r\n\r\n";
    char *success_response = "HTTP/1.1 200 OK\r\n";
    char *server_error = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    char *not_modified = "HTTP/1.1 304 Not Modified\r\n\r\n";
    char *not_supported = "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n";
    char *precondition_failed = "HTTP/1.1 412 Precondition Failed\r\n\r\n";
    char *partial_content = "HTTP/1.1 206 Partial Content\r\n\r\n";
    char* path, *http_type;
    char current_time[80];
    const char s[2] = " ";
    struct tm is_modified_target_time;
    struct tm is_unmodified_target_time;
    struct tm range_target_time;
    double is_modified_after = 0;
    int flag_is_modified = 0;
    int flag_is_unmodified = 0;
    int flag_if_match = 0;
    int flag_if_nonmatch = 0;
    int flag_if_range = 0;
    int flag_range_is_time = 0;
    int flag_need_206 = 0;
    char last_modified_time[100];

    // if the request is not GET or if modified since, return bad_request to client and return
    const char* req = strtok(request, " ");
    printf("request is %s\n", req);
    int flag_GET = strcmp(req, "GET");
    printf("flag_GET=%d\n", flag_GET);
    if ((flag_GET != 0)) {
        fprintf(stderr, "Method is not GET, Returning\n");
        fprintf(stderr, "request is %s\n", req);
        write(client_fd, bad_request, strlen(bad_request));
        return;
    }
    
    // nothing requested, return bad_request
    if ((path = strtok(NULL, " ")) == NULL) {
        fprintf(stderr, "Nothing Requested\n");
        write(client_fd, bad_request, strlen(bad_request));
        return;
    }
    printf("path = %s\n", path);

    // if its not HTTP/1.1, return not_supported
    http_type = strtok(NULL, "\r\n");
    printf("http_type = %s\n", http_type);
    if ((strcasecmp(http_type, "HTTP/1.1")) != 0) {
        write(client_fd, not_supported, strlen(not_supported));
        fprintf(stderr, "Not HTTP/1.1, Returning\n");
        return;
    }

    int path_len = strlen(path) + strlen(root_path) + 1;
    char *complete_path = (char *)malloc(path_len * sizeof(char));
    
    if(!complete_path){ // if malloc fails
        write(client_fd, server_error, strlen(server_error));
        fprintf(stderr, "Not Enough Memory\n");
        return;
    }

    char *key, *value;
    char *modified_date = NULL;
    char *unmodified_date = NULL;
    char *etag_given = NULL;
    char *etag_given_none = NULL;
    char *range = NULL;

    while(1){ // parsing header, setting corresponding flags and values
        if ((key = strtok(NULL, " \t")) == NULL || (value = strtok(NULL, "\r\n")) == NULL){
            break;
        }else if (strcasecmp(key, "\nIf-Modified-Since:") == 0 || strcasecmp(key, "If-Modified-Since:") == 0){
            modified_date = value;
            printf("if-Modified value = %s\n", value);
            flag_is_modified = 1;
            handle_if_modified_since(&is_modified_target_time, modified_date);

            //strftime(last_modified_time, 100, DATE_FORMAT, &target_time);
            //printf("time read out from header is: %s\n", last_modified_time);
        }else if (strcasecmp(key, "\nIf-Unmodified-Since:") == 0 || strcasecmp(key, "If-Unmodified-Since:") == 0){
            unmodified_date = value;
            printf("if-UnModified value = %s\n", value);
            flag_is_unmodified = 1;
            handle_if_modified_since(&is_unmodified_target_time, unmodified_date);
        }else if (strcasecmp(key, "\nIf-Match:") == 0 || strcasecmp(key, "If-Match:") == 0){
            etag_given = parse_etag(value);
            printf("if-Match value = %s\n", value);
            flag_if_match = 1;
        }else if (strcasecmp(key, "\nIf-None-Match:") == 0 || strcasecmp(key, "If-None-Match:") == 0){
            etag_given_none = parse_etag(value);
            printf("if-Nonmatch value = %s\n", value);
            flag_if_nonmatch = 1;
        
        }else if (strcasecmp(key, "\nIf-Range:") == 0 || strcasecmp(key, "If-Range:") == 0){
            range = value;
            printf("if-Range value = %s\n", value);
            if((strptime(range, DATE_FORMAT, &range_target_time)) == NULL){ // if value is a etag
                flag_range_is_time = 0;
                printf("range is etag\n");
                range = parse_etag(range);
            }else{ // else it is in time format
                printf("range is time\n");
                flag_range_is_time = 1;
            }
            flag_if_range = 1;
        }
    }

    // memset(complete_path, '\0', path_len); // clean up complete_path mem 
    strcpy(complete_path, root_path);
    strncat(complete_path, path, path_len);
    printf("Requested File Path: %s\n", complete_path);

    // get E-Tag (aka. md5 hash)
    FILE *fp = fopen(complete_path, "r");
    if (fp == NULL) {
        perror("fopen");
        write(client_fd, file_not_found, strlen(file_not_found));
        return;
    }
    printf("File Found\n");
    uint8_t *result = md5File(fp);
    fclose(fp);
    char hash[129];
    char *cur = hash;
    for (unsigned int i = 0; i < 16; ++i) {
		sprintf(cur, "%02x", result[i]);
        cur += 2;
	}
    cur = '\0';
	printf("File MD5 Hash: %s\n", hash);
    
    int file_fd = open(complete_path, O_RDONLY);
    //printf("here\n");

    if (file_fd == -1) { // file not found
        fprintf(stderr, "File Not Found, Returned %d\n", file_fd);
        write(client_fd, file_not_found, strlen(file_not_found));
        return;
    }
    char servertime[100];
    time_t now = time(NULL);
    struct tm tmp;
    memcpy(&tmp, localtime(&now), sizeof(struct tm));
    strftime(servertime, sizeof(servertime) - 1, DATE_FORMAT, &tmp);
    // servertime is the time server start making response.
    struct stat filestat;
    int filesize;
    if (fstat(file_fd, &filestat) != 0) { //if fstat fetch fails
        perror("fstat");
        exit(EXIT_FAILURE);
    }
    filesize = filestat.st_size;
    tmp = *localtime(&(filestat.st_mtime)); // tmp is struct tm of file's last modified time
    // recall above we put target time in target_time, so diff time can compare
    if (flag_is_modified == 1){
        is_modified_after = difftime(filestat.st_mtime, mktime(&is_modified_target_time));
        // note: file is not midified after <-> target time > modified time <-> difftime < 0
        if (is_modified_after < 0) { // when the file is not modified after
            // so we write not modified and exit.
            printf("File found but not modified after, returning\n");
            write(client_fd, not_modified, strlen(not_modified));
            //close(client_fd);
            close(file_fd);
            return;
        }
    }
    if (flag_is_unmodified == 1) { //header has unmodified since
        is_modified_after = difftime(filestat.st_mtime, mktime(&is_unmodified_target_time));
        // note: file is not midified after <-> target time > modified time <-> difftime < 0
        if (is_modified_after > 0) { // when the file is modified after
            // so we write 412 modified and exit.
            printf("File found and modified after, returning\n");
            write(client_fd, precondition_failed, strlen(precondition_failed));
            //close(client_fd);
            close(file_fd);
            return;
        }
    }
    if(flag_if_match == 1){
        //printf("here1111\n");
        int is_match;
        //printf("etag_given = %s\n", etag_given);
        //printf("hash = %s\n", hash);
        is_match = strncmp(hash, etag_given, strlen(hash));
        //printf("here222222222\n");
        if((is_match != 0) && (strcmp("*", etag_given) != 0)){// file is found but etag is not matching
            printf("File found but Etag dont match, returning\n");
            write(client_fd, precondition_failed, strlen(precondition_failed));
            //close(client_fd);
            close(file_fd);
            return;
        }
    }
    if(flag_if_nonmatch == 1){
        int is_match;
        is_match = strcmp(hash, etag_given_none);
        if(is_match == 0){// file is found but etag is matching, then return 304
            printf("File found but Etag match and if_non_match given, returning\n");
            write(client_fd, not_modified, strlen(not_modified));
            //close(client_fd);
            close(file_fd);
            return;
        }
    }
    if(flag_if_range == 1){
        if(flag_range_is_time){ // it is time, so do time comparison
            printf("going in to process if range time section\n");
            is_modified_after = difftime(filestat.st_mtime, mktime(&range_target_time));
            // note: file is not midified after <-> target time > modified time <-> difftime < 0
            printf("is_modified_after = %f\n", is_modified_after);
            if (is_modified_after > 0) { // when the file is modified after
                // so range condition is fulfilled, need 206 header
                printf("now we flag need_206\n");
                flag_need_206 = 1;
            }
        }else{ // it is etag, so do etag comparison
            //if etag match, then need to return 206 header
            int is_match;
            is_match = strcmp(hash, range);
            if(is_match == 0){// file is found but etag is matching, then precondition failed
                flag_need_206 = 1;
            }
        }
    }
    //printf("here3\n");
    strftime(last_modified_time, 100, DATE_FORMAT, &tmp);
    // now last_modified_time is ready to add to header
    char *content_type;
    char *header = malloc(512);
    printf("start parse_path\n");
    char *filename;
    filename = parse_path(complete_path);
    printf("start get_file_extension, filename = %s\n", filename);
    content_type = get_file_extension(filename);
    if(flag_need_206){
        write(client_fd, partial_content, strlen(partial_content));
    }else{
        write(client_fd, success_response, strlen(success_response));
    }
    sprintf(header, "Date: %s\r\nETag: \"%s\"\r\nContent-Length: %d\r\nContent-Type: %s\r\nLast-Modified: %s\r\n\r\n",
            servertime, hash, filesize, content_type, last_modified_time);
    printf("%s\n", header);
    printf("start write to client\n");
    write(client_fd, header, strlen(header));
    sendfile(client_fd, file_fd, NULL, filesize);
    close(file_fd);
    //close(client_fd);
    free(complete_path);
    free(header);
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
    fcntl(server_soc, F_SETFL, O_NONBLOCK);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(addr.sin_zero), 0, 8);
    
    bind(server_soc, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

    if (listen(server_soc, 2) < 0) {
        close(server_soc);
        perror("listen");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    memset(&(addr_in.sin_zero), 0, 8);
    unsigned int client_size = sizeof(struct sockaddr_in);

    fd_set rfds;
    while (1) {
        int client_fd = accept(server_soc, (struct sockaddr *) &addr_in, &client_size);
        if (client_fd == -1) {
            printf("waiting for connection\n");
            sleep(1);
            continue;
        }
        int flag_break = 0;
        while (1) {
            printf("entering while, client fd: %d\n", client_fd);

            FD_ZERO(&rfds);
            FD_SET(client_fd, &rfds);
            /* Wait up to 30 seconds. */
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 30;
            int retval = select(client_fd + 1, &rfds, NULL, NULL, &tv);

            if (retval == -1) {
                close(client_fd);
                perror("select()");
            }
            else if (retval) {
                printf("Data is available now.\n");
                char buf[1024];
                int n = read(client_fd, buf, 1024);
                if (n < 0) {
                    close(client_fd);
                    break;
                }
                handle_request(buf, client_fd, dir);
            }
            else {
                printf("No data within 30 seconds.\n");
                close(client_fd);
                FD_CLR(client_fd, &rfds);
                break;
            }

        }
    }
    return 0;
}