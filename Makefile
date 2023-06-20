all: simple persistent pipelined

simple:
	gcc -Wextra -O3 -o simple SimpleServer.c
	gcc -o http_client http_client.c
persistent:
	gcc -Wextra -g -o persistent PersistentServer.c
	gcc -o http_client http_client.c
pipelined:
	gcc -Wextra -g -o pipelined PipelinedServer.c
	gcc -o http_client http_client.c
clean:
	rm simple persistent pipelined http_client
