#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

// Server IP or the local address bound to tunnel
static const char* server_ip = "127.0.0.1";
static uint16_t port = 5000;

int Client_SocketInit() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    int client_fd = -1;
    struct sockaddr_in server_addr = { 0 };

    if (sock == -1) {
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    inet_pton(
            AF_INET, 
            server_ip, 
            &server_addr.sin_addr);

    if (
        connect(
            sock, 
            (struct sockaddr*)&server_addr, 
            sizeof(server_addr)) != 0
    ) {
        return -1;
    }

    char hs_buf[16] = { 0 };

    recv(sock, hs_buf, 16, 0);

    if (strcmp(hs_buf, "BUSY\n") == 0) {
        fprintf(stderr, "[Error] Server full. Please try again later.\n");
	close(sock);
	return -1;
    }

    printf("Connected to server at %s:%hu\n", server_ip, port);

    return sock;
}

void Client_Communicate(int sock) {
    char out[256] = { 0 };
    char in[256] = { 0 };
    printf("What do you have to say?\n");
    fgets(out, sizeof(out), stdin);
    send(sock, out, strlen(out), 0);
    recv(sock, in, sizeof(in), 0);
    printf("[Server] %s\n", in);
}

void Client_Close(int sock) {
    close(sock);
}

int main() {
    int sock = Client_SocketInit();
    
    if (sock == -1) return -1;

    Client_Communicate(sock);

    Client_Close(sock);

    return 0; 
}
