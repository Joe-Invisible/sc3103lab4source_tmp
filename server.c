#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

#define MAX_CLIENTS     3U

// Server IP or the local address bound to tunnel
static const char* self = "127.0.0.1";
static uint16_t port = 5000;
static int server_fd = -1;

typedef struct Client {
    int index;
    pthread_t thread;
    int fd;
    int is_active;
} Client;

static Client clients[MAX_CLIENTS] = { 0 };
static int active_count = 0;
static sem_t slots;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void* Server_Communicate(void*);

int Server_SocketInit() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { 0 };

    if (server_fd == -1) {
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (
        bind(
            server_fd,
            (struct sockaddr*)&addr,
            sizeof(addr)) != 0
    ) {
        return -1;
    }

    listen(server_fd, 5);

    printf("Listening on %s:%hu\n", self, port);

    return 0;
}

void Server_Init() {
	sem_init(&slots, 0, MAX_CLIENTS);
	for (int i = 0; i < MAX_CLIENTS; i++) {
		clients[i].index = i;
		clients[i].fd = -1;
		clients[i].is_active = 0;
	}

	Server_SocketInit();
}

void Server_Accept() {
    while (1) {

	int is_assigned = 0;
	int client_id = -1;

        int client_fd = accept(server_fd, NULL, NULL);

	if (client_fd < 0) continue;

	if (sem_trywait(&slots) != 0) {
		// client list full
		const char* res = "BUSY\n";
		printf("[INFO] No empty slots. Rejecting new client...\n");
		send(client_fd, res, strlen(res), 0);
		close(client_fd);
		continue;
	} else {
		const char* res = "READY\n";
		send(client_fd, res, strlen(res), 0);
	}

	// find an empty slot for new client
	pthread_mutex_lock(&lock);
        for (int i = 0; i < MAX_CLIENTS; i++) {
		if (!clients[i].is_active) {
		        active_count += 1;
			clients[i].fd = client_fd;
			clients[i].is_active = 1;
			is_assigned = 1;
			client_id = i;
			break;
		}
	}
	pthread_mutex_unlock(&lock);
	if (!is_assigned) {
		// blocking guarantees empty slot when accepting,
		// this is just a guard.
		perror("Error: Connection accepted but client list was full. Aborting this client...\n");
		const char* err =  "Error: Internal server error (500)"; 
		send(client_fd, err, strlen(err), 0);
		close(client_fd);
		sem_post(&slots);
		continue;
	}

        printf("[INFO] Client connected: slot id=%d\n", client_id);
	printf("[INFO] Concurrent clients (%d/%d)\n", active_count, MAX_CLIENTS);

        int ret = pthread_create(
            &clients[client_id].thread,
            NULL,
            Server_Communicate,
            &clients[client_id]);
	if (ret == 0) {
		pthread_detach(clients[client_id].thread);
	}

    }
}

void* Server_Communicate(void* _client) {
    Client* client = (Client*)(_client);
    int client_fd = client->fd;
    char buffer[256] = { 0 };
    char reply[256] = "Error: Please send a valid number within range.\n";
    size_t used = 0;
    int complete = 0;

    // Read one line, since TCP may split a request across multiple receives.
    while (used < sizeof(buffer) - 1) {
        ssize_t received = recv(client_fd, buffer + used,
                                sizeof(buffer) - 1 - used, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (received == 0) {
            complete = 1;
            break;
        }
        used += (size_t)received;
        char* newline = memchr(buffer, '\n', used);
        if (newline != NULL) {
            used = (size_t)(newline - buffer) + 1;
            complete = 1;
            break;
        }
    }
    buffer[used] = '\0';
    printf("[Client %d] %s\n", client->index, buffer);

    // Reject incomplete/oversized lines and embedded null bytes as well.
    if (complete && memchr(buffer, '\0', used) == NULL) {
        char* end;
        errno = 0;
        double number = strtod(buffer, &end);
        int valid = end != buffer && errno != ERANGE && isfinite(number);
        while (isspace((unsigned char)*end)) end++;

        if (valid && *end == '\0' && isfinite(number * 5.0)) {
            snprintf(reply, sizeof(reply), "%.17g\n", number * 5.0);
        }
    }

    size_t sent = 0;
    size_t reply_length = strlen(reply);
    while (sent < reply_length) {
        ssize_t count = send(client_fd, reply + sent, reply_length - sent,
                             MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        sent += (size_t)count;
    }

    close(client_fd);

    pthread_mutex_lock(&lock);
    client->fd = -1;
    client->is_active = 0;
    active_count -= 1;
    pthread_mutex_unlock(&lock);
    sem_post(&slots);

    return NULL;
}

void Server_Close() {
    close(server_fd);
}


int main() {
    Server_Init();
    Server_Accept();

    return 0;
}
