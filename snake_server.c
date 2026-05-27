#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
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

static void sleep_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

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
  listen(server_fd, 1);
  printf("[server] listening on :%d  tick=%d Hz\n", PORT, TICK_RATE);

  /* accept one client */
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
  if (client_fd < 0) {
    perror("accept");
    return 1;
  }
  printf("[server] client connected: %s\n", inet_ntoa(client_addr.sin_addr));

  /* send zeroed DataPacket every tick */
  struct DataPacket packet;
  memset(&packet, 0, sizeof(packet));

  while (1) {
    long start = now_ms();

    if (send(client_fd, &packet, sizeof(packet), MSG_NOSIGNAL) < 0) {
      printf("[server] client disconnected\n");
      break;
    }

    long remaining = TICK_MS - (now_ms() - start);
    if (remaining > 0)
      sleep_ms(remaining);
  }

  close(client_fd);
  close(server_fd);
  return 0;
}
