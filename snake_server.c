#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define MAX_CLIENTS 2
#define TICK_RATE 15
#define TICK_MS (1000 / TICK_RATE)

#define GAME_WIDTH 64
#define GAME_HEIGHT 64

/*

  data shared between threads

*/

struct gamePacket {
  // lock player count to 2
  // don't want it to be hard to implement
  int playerCount;
  int score[2];
  int appleAmount;
  int appleXarr[100];
  int appleYarr[100];
  int snakeLength[2];
  int snakeTailXarr[2][100];
  int snakeTailYarr[2][100];
};

struct sendPacket {
  int snakeDirection;
};

/* client state: plain arrays, index = player id (0 or 1) */
static int client_fd[2] = {-1, -1};
static int client_active[2] = {0, 0};
static int client_count = 0;

static pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* static initializer data */
static int appleAmount = 10;
static int appleXarr[100];
static int appleYarr[100];

// game data storage
static int snakePosX[2];
static int snakePosY[2];
static int snakeDirection[2];
static int inputDirection[2];
static int snakeSpeed[2];
static int snakeLength[2];
static int snakeTailXarr[2][100];
static int snakeTailYarr[2][100];
static int score[2];

/* flag: both players connected, game is running */
static volatile int game_running = 0;
/* flag: server should exit */
static volatile int server_exit = 0;

/*

  precise timing modules

*/

// sleep (precise)
static void sleep_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}
// get current time (precise)
static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/*

  array helpers

*/

// array shift
void insert(int array[], int *size, int value) {
  for (int i = *size; i > 0; i--) {
    array[i] = array[i - 1];
  }
  array[0] = value;
  (*size)++;
}

// append at index end
void append(int array[], int *size, int value) {
  array[*size] = value;
  (*size)++;
}

// pop array at certain index
void array_pop_at(int array[], int *size, int index) {
  if (index < 0 || index >= *size) {
    printf("error: index out of bounds\n");
    exit(1);
  }
  for (int i = index; i < *size - 1; i++) {
    array[i] = array[i + 1];
  }
  (*size)--;
}

/*

  position init

*/
void initApplePosition(int (*appleX)[], int (*appleY)[], int width, int height,
                       int amount) {
  for (int i = 0; i < amount; i++) {
    (*appleX)[i] = (rand() % width) + 1;
    (*appleY)[i] = (rand() % height) + 1;
  }
}

/* init a single player's snake */
void initSnake(int p) {
  snakeLength[p] = 3;
  snakeSpeed[p] = 1;

  if (p == 0) {
    // player 0 starts top-left area, facing right
    snakePosX[p] = 10;
    snakePosY[p] = 10;
    snakeDirection[p] = 1; // RIGHT
    inputDirection[p] = 1;
  } else {
    // player 1 starts bottom-right area, facing left
    snakePosX[p] = GAME_WIDTH - 10;
    snakePosY[p] = GAME_HEIGHT - 10;
    snakeDirection[p] = 0; // LEFT
    inputDirection[p] = 0;
  }

  // lay out the initial tail behind the snake
  for (int i = 0; i < snakeLength[p]; i++) {
    if (snakeDirection[p] == 1) {
      // facing right, tail extends to the left
      snakeTailXarr[p][i] = snakePosX[p] - i;
      snakeTailYarr[p][i] = snakePosY[p];
    } else {
      // facing left, tail extends to the right
      snakeTailXarr[p][i] = snakePosX[p] + i;
      snakeTailYarr[p][i] = snakePosY[p];
    }
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

/* thread stuffs */
static void *reader_thread(void *arg) {
  int p = *(int *)arg;
  free(arg);
  int fd = client_fd[p];
  struct sendPacket sP;
  ssize_t n;

  while ((n = recv(fd, (char *)&sP, sizeof(sP), 0)) > 0) {
    pthread_mutex_lock(&game_mutex);
    inputDirection[p] = sP.snakeDirection;
    pthread_mutex_unlock(&game_mutex);

    printf("[server] fd=%d player=%d direction: %d\n", fd, p,
           sP.snakeDirection);
  }

  /* player disconnected — mark inactive and signal server to exit */
  printf("[server] player %d disconnected (fd=%d)\n", p, fd);
  close(fd);

  pthread_mutex_lock(&clients_mutex);
  client_active[p] = 0;
  pthread_mutex_unlock(&clients_mutex);

  /* if either player leaves, the server should exit */
  server_exit = 1;

  return NULL;
}

static void *accept_thread(void *arg) {
  int server_fd = *(int *)arg;
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);

  while (1) {
    /* stop accepting once we have 2 players */
    pthread_mutex_lock(&clients_mutex);
    int count = client_count;
    pthread_mutex_unlock(&clients_mutex);
    if (count >= MAX_CLIENTS) {
      sleep_ms(100);
      continue;
    }

    int fd = accept(server_fd, (struct sockaddr *)&addr, &len);
    if (fd < 0) {
      perror("accept");
      continue;
    }

    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
      int assigned_id = client_count; // 0 for first, 1 for second

      // send the player ID back
      if (send(fd, &assigned_id, sizeof(assigned_id), MSG_NOSIGNAL) < 0) {
        close(fd);
        pthread_mutex_unlock(&clients_mutex);
        continue;
      }

      client_fd[assigned_id] = fd;
      client_active[assigned_id] = 1;
      client_count++;

      int *pid = malloc(sizeof(int));
      *pid = assigned_id;
      pthread_t tid;
      pthread_create(&tid, NULL, reader_thread, pid);
      pthread_detach(tid);

      printf("[server] client connected: %s fd=%d player=%d (total=%d)\n",
             inet_ntoa(addr.sin_addr), fd, assigned_id, client_count);

      /* if we now have 2 players, start the game */
      if (client_count == MAX_CLIENTS) {
        game_running = 1;
        printf("[server] both players connected — game starting!\n");
      }
    } else {
      /* already full, reject */
      printf("[server] rejecting connection from %s — game full\n",
             inet_ntoa(addr.sin_addr));
      close(fd);
    }
    pthread_mutex_unlock(&clients_mutex);
  }
  return NULL;
}

/* ── tick loop (main thread) ──────────────────────────────────────── */
int main(void) {
  srand(time(NULL));

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
  printf("[server] listening on :%d  tick=%d Hz  players=2 (waiting...)\n",
         PORT, TICK_RATE);

  pthread_t tid;
  pthread_create(&tid, NULL, accept_thread, &server_fd);
  pthread_detach(tid);

  /* ── game state init ───────────────────────────────────────────── */
  for (int i = 0; i < 2; i++) {
    initSnake(i);
    score[i] = 0;
  }

  initApplePosition(&appleXarr, &appleYarr, GAME_WIDTH, GAME_HEIGHT,
                    appleAmount);

  printf("[server] game initialized: %d apples\n", appleAmount);

  /* ── wait for both players to connect ──────────────────────────── */
  while (!game_running) {
    sleep_ms(50);
  }

  /* ── main game loop ────────────────────────────────────────────── */
  while (!server_exit) {
    long start = now_ms();

    pthread_mutex_lock(&game_mutex);

    /* process each player */
    for (int p = 0; p < 2; p++) {

      /* apply direction with movement limiter (same logic as snake.c) */
      int inputDir = inputDirection[p];
      if (snakeDirection[p] == 0 || snakeDirection[p] == 1) {
        // currently horizontal — only allow vertical change
        if (inputDir == 2)
          snakeDirection[p] = 2;
        if (inputDir == 3)
          snakeDirection[p] = 3;
      } else {
        // currently vertical — only allow horizontal change
        if (inputDir == 0)
          snakeDirection[p] = 0;
        if (inputDir == 1)
          snakeDirection[p] = 1;
      }

      /* move the snake */
      moveSnake(&snakePosX[p], &snakePosY[p], &snakeTailXarr[p],
                &snakeTailYarr[p], snakeSpeed[p], snakeDirection[p],
                snakeLength[p]);

      /* apple collision detection */
      for (int i = 0; i < appleAmount; i++) {
        if (appleXarr[i] == snakePosX[p] && appleYarr[i] == snakePosY[p]) {
          // pop that apple from array and deduct appleAmount
          array_pop_at(appleXarr, &appleAmount, i);

          // pop workaround (same as snake.c)
          appleAmount += 1;
          array_pop_at(appleYarr, &appleAmount, i);

          // increase snake length and score
          snakeLength[p] += 1;
          score[p] += 1;
        }
      }

      // tail collision check
      // START FROM INDEX 1 because skip the current POSITION
      // SO IT WON'T KILL US INSTANTLY
      int self_hit = 0;
      for (int i = 1; i < snakeLength[p]; i++) {
        if (snakeTailXarr[p][i] == snakePosX[p] &&
            snakeTailYarr[p][i] == snakePosY[p]) {
          self_hit = 1;
          break;
        }
      }

      /* boundary collision check */
      int boundary_hit = (snakePosX[p] > (GAME_WIDTH - 1) ||
                          snakePosY[p] > (GAME_HEIGHT - 1) ||
                          snakePosX[p] < 0 || snakePosY[p] < 0);

      /* cross-collision: did this snake hit the OTHER snake's body? */
      int cross_hit = 0;
      int other = (p == 0) ? 1 : 0;
      for (int i = 0; i < snakeLength[other]; i++) {
        if (snakeTailXarr[other][i] == snakePosX[p] &&
            snakeTailYarr[other][i] == snakePosY[p]) {
          cross_hit = 1;
          break;
        }
      }

      if (self_hit || boundary_hit || cross_hit) {
        const char *reason =
            self_hit ? "self-collision"
                     : (boundary_hit ? "out of bounds" : "hit other snake");
        printf("[server] player %d game over! %s. score=%d\n", p, reason,
               score[p]);
        // reset score and reinit position
        score[p] = 0;
        initSnake(p);
      }
    }

    pthread_mutex_unlock(&game_mutex);

    /* build and broadcast the game state packet */
    struct gamePacket packet;
    memset(&packet, 0, sizeof(packet));

    pthread_mutex_lock(&game_mutex);

    packet.playerCount = 2;
    packet.appleAmount = appleAmount;

    memcpy(packet.appleXarr, appleXarr, sizeof(int) * appleAmount);
    memcpy(packet.appleYarr, appleYarr, sizeof(int) * appleAmount);

    for (int p = 0; p < 2; p++) {
      packet.score[p] = score[p];
      packet.snakeLength[p] = snakeLength[p];
      memcpy(packet.snakeTailXarr[p], snakeTailXarr[p],
             sizeof(int) * snakeLength[p]);
      memcpy(packet.snakeTailYarr[p], snakeTailYarr[p],
             sizeof(int) * snakeLength[p]);
    }

    pthread_mutex_unlock(&game_mutex);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < 2; i++) {
      if (!client_active[i])
        continue;

      if (send(client_fd[i], &packet, sizeof(packet), MSG_NOSIGNAL) < 0) {
        client_active[i] = 0; /* reader thread will close fd */
      }
    }
    pthread_mutex_unlock(&clients_mutex);

    long remaining = TICK_MS - (now_ms() - start);
    if (remaining > 0)
      sleep_ms(remaining);
  }

  /* one player left — shut down */
  printf("[server] a player disconnected — shutting down.\n");

  /* close all remaining client fds */
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < 2; i++) {
    if (client_active[i]) {
      close(client_fd[i]);
      client_active[i] = 0;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  close(server_fd);
  return 0;
}