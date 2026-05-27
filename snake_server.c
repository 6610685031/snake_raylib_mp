#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define MAX_CLIENTS 32
#define TICK_RATE 15
#define TICK_MS (1000 / TICK_RATE)

/* ── game constants ──────────────────────────────────────────────── */
#define GAME_WIDTH 64
#define GAME_HEIGHT 64

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

/* ── per-client input (last direction received) ──────────────────── */
static int client_direction = 0; /* protected by clients_mutex */

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

//
// ARRAY HELPERS (ported from snake.c)
//
void insert(int array[], int *size, int value) {
  // array shift
  for (int i = *size; i > 0; i--) {
    array[i] = array[i - 1];
  }
  // insert value into first index
  array[0] = value;

  // increment the active element counter
  (*size)++;
}

void append(int array[], int *size, int value) {
  // insert the value at the current end index
  array[*size] = value;
  (*size)++;
}

void array_pop_at(int array[], int *size, int index) {
  if (index < 0 || index >= *size) {
    printf("error: index out of bounds\n");
    return; /* don't exit the server */
  }

  // shift elements left to fill the gap
  for (int i = index; i < *size - 1; i++) {
    array[i] = array[i + 1];
  }

  // decrease size
  (*size)--;
}

//
// GAME INIT HELPERS (ported from snake.c)
//
void initApplePosition(int (*appleX)[], int (*appleY)[], int width, int height,
                       int amount) {
  srand(time(NULL));
  for (int i = 0; i < amount; i++) {
    (*appleX)[i] = (rand() % width) + 1;
    (*appleY)[i] = (rand() % height) + 1;
  }
}

void initSnakePosition(int *curPosX, int *curPosY, int (*tailX)[],
                       int (*tailY)[], int length) {
  // first index = head, last index = last tail
  for (int i = 0; i < length; i++) {
    (*tailX)[i] = *curPosX + i;
    (*tailY)[i] = *curPosY;
  }
}

void moveSnake(int *curPosX, int *curPosY, int (*tailX)[], int (*tailY)[],
               int speed, int curDir, int length) {
  if (curDir == 0)
    *curPosX -= speed; // LEFT
  if (curDir == 1)
    *curPosX += speed; // RIGHT
  if (curDir == 2)
    *curPosY -= speed; // UP
  if (curDir == 3)
    *curPosY += speed; // DOWN

  // insert new position at front index
  insert(*tailX, &length, *curPosX);
  insert(*tailY, &length, *curPosY);
}

/* ── per-client reader thread ─────────────────────────────────────── */
static void *reader_thread(void *arg) {
  ClientState *c = (ClientState *)arg;
  struct sendPacket sP;
  ssize_t n;

  while ((n = recv(c->fd, (char *)&sP, sizeof(sP), 0)) > 0) {
    pthread_mutex_lock(&clients_mutex);
    client_direction = sP.snakeDirection;
    pthread_mutex_unlock(&clients_mutex);

    printf("[server] fd=%d direction: %d\n", c->fd, sP.snakeDirection);
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

  /* ── game state init (ported from snake.c) ─────────────────────── */
  int score = 0;

  int appleAmount = 10;
  int appleXarr[100];
  int appleYarr[100];

  int snakeLength = 5;
  int snakeTailXarr[100];
  int snakeTailYarr[100];

  int snakePositionX = GAME_WIDTH / 2;
  int snakePositionY = GAME_HEIGHT / 2;

  // current direction of snake: 0 = left, 1 = right, 2 = up, 3 = down
  int snakeDirection = 0;

  int snakeSpeed = 1;

  initSnakePosition(&snakePositionX, &snakePositionY, &snakeTailXarr,
                    &snakeTailYarr, snakeLength);
  initApplePosition(&appleXarr, &appleYarr, GAME_WIDTH, GAME_HEIGHT,
                    appleAmount);

  printf("[server] game initialized: snake at (%d,%d), %d apples\n",
         snakePositionX, snakePositionY, appleAmount);

  /* ── main game loop ────────────────────────────────────────────── */
  while (1) {
    long start = now_ms();

    /* read client input */
    pthread_mutex_lock(&clients_mutex);
    int inputDir = client_direction;
    pthread_mutex_unlock(&clients_mutex);

    /* apply direction with movement limiter (same logic as snake.c) */
    if (snakeDirection == 0 || snakeDirection == 1) {
      // currently horizontal — only allow vertical change
      if (inputDir == 2)
        snakeDirection = 2;
      if (inputDir == 3)
        snakeDirection = 3;
    } else {
      // currently vertical — only allow horizontal change
      if (inputDir == 0)
        snakeDirection = 0;
      if (inputDir == 1)
        snakeDirection = 1;
    }

    /* move the snake */
    moveSnake(&snakePositionX, &snakePositionY, &snakeTailXarr, &snakeTailYarr,
              snakeSpeed, snakeDirection, snakeLength);

    /* apple collision detection */
    for (int i = 0; i < appleAmount; i++) {
      if (appleXarr[i] == snakePositionX && appleYarr[i] == snakePositionY) {
        // pop that apple from array and deduct appleAmount
        array_pop_at(appleXarr, &appleAmount, i);

        // pop workaround (same as snake.c)
        appleAmount += 1;
        array_pop_at(appleYarr, &appleAmount, i);

        // increase snake length and score
        snakeLength += 1;
        score += 1;
      }
    }

    /* tail collision check */
    // START FROM INDEX 1 because skip the current POSITION
    // SO IT WON'T KILL US INSTANTLY
    for (int i = 1; i < snakeLength; i++) {
      if (snakeTailXarr[i] == snakePositionX &&
          snakeTailYarr[i] == snakePositionY) {
        // game over — reset
        printf("[server] game over! self-collision. score=%d\n", score);
        score = 0;
        appleAmount = 10;
        snakeLength = 5;
        snakeDirection = 0;
        snakePositionX = GAME_WIDTH / 2;
        snakePositionY = GAME_HEIGHT / 2;
        initSnakePosition(&snakePositionX, &snakePositionY, &snakeTailXarr,
                          &snakeTailYarr, snakeLength);
        initApplePosition(&appleXarr, &appleYarr, GAME_WIDTH, GAME_HEIGHT,
                          appleAmount);
        break;
      }
    }

    /* boundary collision check */
    if (snakePositionX > (GAME_WIDTH - 1) ||
        snakePositionY > (GAME_HEIGHT - 1) || snakePositionX < 0 ||
        snakePositionY < 0) {
      // game over — reset
      printf("[server] game over! out of bounds. score=%d\n", score);
      score = 0;
      appleAmount = 10;
      snakeLength = 5;
      snakeDirection = 0;
      snakePositionX = GAME_WIDTH / 2;
      snakePositionY = GAME_HEIGHT / 2;
      initSnakePosition(&snakePositionX, &snakePositionY, &snakeTailXarr,
                        &snakeTailYarr, snakeLength);
      initApplePosition(&appleXarr, &appleYarr, GAME_WIDTH, GAME_HEIGHT,
                        appleAmount);
    }

    /* build and broadcast the game state packet */
    struct DataPacket packet;
    memset(&packet, 0, sizeof(packet));

    packet.score = score;
    packet.appleAmount = appleAmount;
    packet.snakeLength = snakeLength;

    memcpy(packet.appleXarr, appleXarr, sizeof(int) * appleAmount);
    memcpy(packet.appleYarr, appleYarr, sizeof(int) * appleAmount);
    memcpy(packet.snakeTailXarr, snakeTailXarr, sizeof(int) * snakeLength);
    memcpy(packet.snakeTailYarr, snakeTailYarr, sizeof(int) * snakeLength);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count;) {
      ClientState *c = &clients[i];

      if (!c->active) {
        /* swap-remove dead client */
        pthread_mutex_destroy(&c->msg_mutex);
        clients[i] = clients[--client_count];
        continue;
      }

      if (send(c->fd, &packet, sizeof(packet), MSG_NOSIGNAL) < 0) {
        c->active = 0; /* reader thread will close fd */
      }
      i++;
    }
    pthread_mutex_unlock(&clients_mutex);

    long remaining = TICK_MS - (now_ms() - start);
    if (remaining > 0)
      sleep_ms(remaining);
  }
}