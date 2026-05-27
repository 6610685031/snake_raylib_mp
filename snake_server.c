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
#define NUM_PLAYERS 2

/* ── wire protocol ───────────────────────────────────────────────── */
struct DataPacket {
  int playerCount;
  int score[NUM_PLAYERS];
  int appleAmount;
  int appleXarr[100];
  int appleYarr[100];
  int snakeLength[NUM_PLAYERS];
  int snakeTailXarr[NUM_PLAYERS][100];
  int snakeTailYarr[NUM_PLAYERS][100];
};

struct sendPacket {
  int snakeDirection;
};

/* ── per-player game state ───────────────────────────────────────── */
typedef struct {
  int score;
  int snakeLength;
  int snakeTailXarr[100];
  int snakeTailYarr[100];
  int posX, posY;
  int direction;
  int inputDirection; /* written by reader_thread */
  int speed;
  int alive; /* 1 = player slot occupied by a connected client */
} PlayerState;

static PlayerState players[NUM_PLAYERS];
static pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── per-client state ─────────────────────────────────────────────── */
typedef struct {
  int fd;
  int active;
  int player_id; /* 0 or 1 for players, -1 for spectators */
  pthread_mutex_t msg_mutex;
} ClientState;

static ClientState clients[MAX_CLIENTS];
static int client_count = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── shared apple state ──────────────────────────────────────────── */
static int appleAmount = 10;
static int appleXarr[100];
static int appleYarr[100];

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

/* ── initialize a single player's state ──────────────────────────── */
static void initPlayer(int id) {
  PlayerState *p = &players[id];
  p->score = 0;
  p->snakeLength = 5;
  p->speed = 1;

  if (id == 0) {
    p->posX = GAME_WIDTH / 3;
    p->posY = GAME_HEIGHT / 2;
    p->direction = 1;      /* facing right */
    p->inputDirection = 1;
  } else {
    p->posX = 2 * GAME_WIDTH / 3;
    p->posY = GAME_HEIGHT / 2;
    p->direction = 0;      /* facing left */
    p->inputDirection = 0;
  }

  initSnakePosition(&p->posX, &p->posY, &p->snakeTailXarr, &p->snakeTailYarr,
                    p->snakeLength);
}

/* ── per-client reader thread ─────────────────────────────────────── */
static void *reader_thread(void *arg) {
  ClientState *c = (ClientState *)arg;
  struct sendPacket sP;
  ssize_t n;

  while ((n = recv(c->fd, (char *)&sP, sizeof(sP), 0)) > 0) {
    if (c->player_id >= 0 && c->player_id < NUM_PLAYERS) {
      pthread_mutex_lock(&players_mutex);
      players[c->player_id].inputDirection = sP.snakeDirection;
      pthread_mutex_unlock(&players_mutex);
    }

    printf("[server] fd=%d player=%d direction: %d\n", c->fd, c->player_id,
           sP.snakeDirection);
  }

  /* client disconnected — mark inactive */
  printf("[server] fd=%d (player %d) reader exiting\n", c->fd, c->player_id);

  if (c->player_id >= 0 && c->player_id < NUM_PLAYERS) {
    pthread_mutex_lock(&players_mutex);
    players[c->player_id].alive = 0;
    pthread_mutex_unlock(&players_mutex);
  }

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

    /* assign a player slot if one is free */
    int assigned_player = -1; /* -1 = spectator */

    pthread_mutex_lock(&players_mutex);
    for (int i = 0; i < NUM_PLAYERS; i++) {
      if (!players[i].alive) {
        assigned_player = i;
        players[i].alive = 1;
        initPlayer(i);
        break;
      }
    }
    pthread_mutex_unlock(&players_mutex);

    /* send the player ID as a handshake (int, -1 for spectator) */
    if (send(fd, &assigned_player, sizeof(assigned_player), MSG_NOSIGNAL) < 0) {
      close(fd);
      if (assigned_player >= 0) {
        pthread_mutex_lock(&players_mutex);
        players[assigned_player].alive = 0;
        pthread_mutex_unlock(&players_mutex);
      }
      continue;
    }

    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
      ClientState *c = &clients[client_count++];
      c->fd = fd;
      c->active = 1;
      c->player_id = assigned_player;
      pthread_mutex_init(&c->msg_mutex, NULL);

      pthread_t tid;
      pthread_create(&tid, NULL, reader_thread, c);
      pthread_detach(tid);

      printf("[server] client connected: %s fd=%d player=%d (total=%d)\n",
             inet_ntoa(addr.sin_addr), fd, assigned_player, client_count);
    } else {
      close(fd);
      if (assigned_player >= 0) {
        pthread_mutex_lock(&players_mutex);
        players[assigned_player].alive = 0;
        pthread_mutex_unlock(&players_mutex);
      }
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
  printf("[server] listening on :%d  tick=%d Hz  players=%d\n", PORT, TICK_RATE,
         NUM_PLAYERS);

  pthread_t tid;
  pthread_create(&tid, NULL, accept_thread, &server_fd);
  pthread_detach(tid);

  /* ── game state init ───────────────────────────────────────────── */
  for (int i = 0; i < NUM_PLAYERS; i++) {
    initPlayer(i);
    players[i].alive = 0; /* no one connected yet */
  }

  initApplePosition(&appleXarr, &appleYarr, GAME_WIDTH, GAME_HEIGHT,
                    appleAmount);

  printf("[server] game initialized: %d apples\n", appleAmount);

  /* ── main game loop ────────────────────────────────────────────── */
  while (1) {
    long start = now_ms();

    pthread_mutex_lock(&players_mutex);

    /* process each player */
    for (int p = 0; p < NUM_PLAYERS; p++) {
      PlayerState *ps = &players[p];
      if (!ps->alive)
        continue;

      /* apply direction with movement limiter (same logic as snake.c) */
      int inputDir = ps->inputDirection;
      if (ps->direction == 0 || ps->direction == 1) {
        // currently horizontal — only allow vertical change
        if (inputDir == 2)
          ps->direction = 2;
        if (inputDir == 3)
          ps->direction = 3;
      } else {
        // currently vertical — only allow horizontal change
        if (inputDir == 0)
          ps->direction = 0;
        if (inputDir == 1)
          ps->direction = 1;
      }

      /* move the snake */
      moveSnake(&ps->posX, &ps->posY, &ps->snakeTailXarr, &ps->snakeTailYarr,
                ps->speed, ps->direction, ps->snakeLength);

      /* apple collision detection */
      for (int i = 0; i < appleAmount; i++) {
        if (appleXarr[i] == ps->posX && appleYarr[i] == ps->posY) {
          // pop that apple from array and deduct appleAmount
          array_pop_at(appleXarr, &appleAmount, i);

          // pop workaround (same as snake.c)
          appleAmount += 1;
          array_pop_at(appleYarr, &appleAmount, i);

          // increase snake length and score
          ps->snakeLength += 1;
          ps->score += 1;
        }
      }

      /* self-collision check */
      // START FROM INDEX 1 because skip the current POSITION
      // SO IT WON'T KILL US INSTANTLY
      int self_hit = 0;
      for (int i = 1; i < ps->snakeLength; i++) {
        if (ps->snakeTailXarr[i] == ps->posX &&
            ps->snakeTailYarr[i] == ps->posY) {
          self_hit = 1;
          break;
        }
      }

      /* boundary collision check */
      int boundary_hit = (ps->posX > (GAME_WIDTH - 1) ||
                          ps->posY > (GAME_HEIGHT - 1) || ps->posX < 0 ||
                          ps->posY < 0);

      /* cross-collision: did this snake hit the OTHER snake's body? */
      int cross_hit = 0;
      for (int other = 0; other < NUM_PLAYERS; other++) {
        if (other == p || !players[other].alive)
          continue;
        for (int i = 0; i < players[other].snakeLength; i++) {
          if (players[other].snakeTailXarr[i] == ps->posX &&
              players[other].snakeTailYarr[i] == ps->posY) {
            cross_hit = 1;
            break;
          }
        }
        if (cross_hit)
          break;
      }

      if (self_hit || boundary_hit || cross_hit) {
        const char *reason =
            self_hit ? "self-collision"
                     : (boundary_hit ? "out of bounds" : "hit other snake");
        printf("[server] player %d game over! %s. score=%d\n", p, reason,
               ps->score);
        ps->score = 0;
        initPlayer(p);
      }
    }

    pthread_mutex_unlock(&players_mutex);

    /* build and broadcast the game state packet */
    struct DataPacket packet;
    memset(&packet, 0, sizeof(packet));

    pthread_mutex_lock(&players_mutex);

    int activeCount = 0;
    for (int p = 0; p < NUM_PLAYERS; p++) {
      if (players[p].alive)
        activeCount++;
    }
    packet.playerCount = activeCount;
    packet.appleAmount = appleAmount;

    memcpy(packet.appleXarr, appleXarr, sizeof(int) * appleAmount);
    memcpy(packet.appleYarr, appleYarr, sizeof(int) * appleAmount);

    for (int p = 0; p < NUM_PLAYERS; p++) {
      packet.score[p] = players[p].score;
      packet.snakeLength[p] = players[p].alive ? players[p].snakeLength : 0;
      if (players[p].alive) {
        memcpy(packet.snakeTailXarr[p], players[p].snakeTailXarr,
               sizeof(int) * players[p].snakeLength);
        memcpy(packet.snakeTailYarr[p], players[p].snakeTailYarr,
               sizeof(int) * players[p].snakeLength);
      }
    }

    pthread_mutex_unlock(&players_mutex);

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