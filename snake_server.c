#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define MAX_CLIENTS 32
#define TICK_RATE 20
#define TICK_MS (1000 / TICK_RATE)

struct DataPacket {
  int score;
  int appleAmount;
  int appleXarr[100];
  int appleYarr[100];
  int snakeLength;
  int snakeTailXarr[100];
  int snakeTailYarr[100];
};

struct sendPacket {
  int snakeDirection;
};

struct sendPacket sP;

/* ── per-client state ─────────────────────────────────────────────── */
typedef struct {
  int fd;
  int active;
  char last_msg[256];        /* last message received from this client */
  pthread_mutex_t msg_mutex; /* protects last_msg */
} ClientState;

static ClientState clients[MAX_CLIENTS];
static int client_count = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── helpers ──────────────────────────────────────────────────────── */
static void sleep_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ── per-client reader thread ─────────────────────────────────────── */
static void *reader_thread(void *arg) {
  ClientState *c = (ClientState *)arg;
  ssize_t n;

  while ((n = recv(c->fd, (char *)&sP, sizeof(sP), 0)) > 0) {
    pthread_mutex_lock(&c->msg_mutex);
    pthread_mutex_unlock(&c->msg_mutex);

    char score_str[20];
    snprintf(score_str, sizeof(sP.snakeDirection), "%d", sP.snakeDirection);

    printf("[server] fd=%d said: %s\n", c->fd, score_str);
  }

  /* client disconnected — mark inactive */
  printf("[server] fd=%d reader exiting\n", c->fd);
  c->active = 0;
  close(c->fd);
  return NULL;
}

/* ── accept thread ────────────────────────────────────────────────── */
static void *accept_thread(void *arg) {
  int server_fd = *(int *)arg;
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);

  while (1) {
    int fd = accept(server_fd, (struct sockaddr *)&addr, &len);
    if (fd < 0) {
      perror("accept");
      continue;
    }

    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
      ClientState *c = &clients[client_count++];
      c->fd = fd;
      c->active = 1;
      c->last_msg[0] = '\0';
      pthread_mutex_init(&c->msg_mutex, NULL);

      pthread_t tid;
      pthread_create(&tid, NULL, reader_thread, c);
      pthread_detach(tid);

      printf("[server] client connected: %s fd=%d (total=%d)\n",
             inet_ntoa(addr.sin_addr), fd, client_count);
    } else {
      close(fd);
    }
    pthread_mutex_unlock(&clients_mutex);
  }
  return NULL;
}

/* ── tick loop (main thread) ──────────────────────────────────────── */
int main(void) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = INADDR_ANY,
      .sin_port = htons(PORT),
  };

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    return 1;
  listen(server_fd, 8);
  printf("[server] listening on :%d  tick=%d Hz\n", PORT, TICK_RATE);

  pthread_t tid;
  pthread_create(&tid, NULL, accept_thread, &server_fd);
  pthread_detach(tid);

  long tick = 0;
  while (1) {
    long start = now_ms();

    /* broadcast tick + echo back each client's last message */
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count;) {
      ClientState *c = &clients[i];

      if (!c->active) {
        /* swap-remove dead client */
        pthread_mutex_destroy(&c->msg_mutex);
        clients[i] = clients[--client_count];
        continue;
      }

      pthread_mutex_lock(&c->msg_mutex);
      pthread_mutex_unlock(&c->msg_mutex);

      struct DataPacket packet;
      memset(&packet, 0, sizeof(packet));

      packet.appleXarr[0] = sP.snakeDirection;
      packet.appleYarr[0] = sP.snakeDirection;

      packet.appleAmount = 1;

      if (send(c->fd, &packet, sizeof(packet), MSG_NOSIGNAL) < 0) {
        c->active = 0; /* reader thread will close fd */
      }
      i++;
    }
    pthread_mutex_unlock(&clients_mutex);

    tick++;
    long remaining = TICK_MS - (now_ms() - start);
    if (remaining > 0)
      sleep_ms(remaining);
  }
}