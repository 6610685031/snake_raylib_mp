#define _POSIX_C_SOURCE 199309L
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT 5050
#define MAX_CLIENTS 32
#define TICK_RATE 20 /* ticks per second */
#define TICK_MS (1000 / TICK_RATE)

/* ---------- shared client list ---------- */
static int clients[MAX_CLIENTS];
static int client_count = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------- helpers ---------- */
static void sleep_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ---------- accept thread ---------- */
static void *accept_thread(void *arg) {
  int server_fd = *(int *)arg;
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);

  while (1) {
    int client_fd = accept(server_fd, (struct sockaddr *)&addr, &len);
    if (client_fd < 0) {
      perror("accept");
      continue;
    }

    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
      clients[client_count++] = client_fd;
      printf("[server] client connected: %s (fd=%d, total=%d)\n",
             inet_ntoa(addr.sin_addr), client_fd, client_count);
    } else {
      close(client_fd); /* full */
    }
    pthread_mutex_unlock(&clients_mutex);
  }
  return NULL;
}

/* ---------- tick loop (main thread) ---------- */
int main(void) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = INADDR_ANY,
      .sin_port = htons(PORT),
  };
  bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
  listen(server_fd, 8);
  printf("[server] listening on :%d  tick_rate=%d Hz\n", PORT, TICK_RATE);

  pthread_t tid;
  pthread_create(&tid, NULL, accept_thread, &server_fd);
  pthread_detach(tid);

  long tick = 0;
  while (1) {
    long start = now_ms();

    /* --- build state message --- */
    char msg[64];
    int len = snprintf(msg, sizeof(msg), "tick=%ld\n", tick++);

    /* --- broadcast to all clients --- */
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count;) {
      if (send(clients[i], msg, len, MSG_NOSIGNAL) < 0) {
        /* dead client — swap-remove */
        printf("[server] client fd=%d disconnected\n", clients[i]);
        close(clients[i]);
        clients[i] = clients[--client_count];
      } else {
        i++;
      }
    }
    pthread_mutex_unlock(&clients_mutex);

    /* --- sleep for remainder of tick --- */
    long elapsed = now_ms() - start;
    long remaining = TICK_MS - elapsed;
    if (remaining > 0)
      sleep_ms(remaining);
  }
}