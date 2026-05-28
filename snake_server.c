#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TICK_RATE 15
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

// client data

// init them to -1; if connected then
// it should be 0 or 1 respectively
static int client_fd[2] = {-1, -1};
// the game should start if client_count is 2
static int client_count = 0;

// mutex stuffs when accessing shared data
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

// check if 2 players both connected or not
// 0 = no, 1 = yes
static int game_running = 0;

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
    // just quit the game if error
    // don't care lol
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

/*

  thread stuffs

  */
static void *reader_thread(void *arg) {
  int p = *(int *)arg;
  free(arg);
  int fd = client_fd[p];
  struct sendPacket sP;
  ssize_t n;

  while ((n = recv(fd, (char *)&sP, sizeof(sP), 0)) > 0) {
    // editing direction data
    // locking mutex
    pthread_mutex_lock(&game_mutex);
    inputDirection[p] = sP.snakeDirection;
    pthread_mutex_unlock(&game_mutex);

    printf("[server] fd=%d player=%d direction: %d\n", fd, p,
           sP.snakeDirection);
  }

  // if the code reaches this point, it means one of the players quitted
  // so we just close the server
  printf("[server] player %d disconnected (fd=%d)\n", p, fd);
  printf("[server] closing the game");
  close(fd);
  exit(1);

  // code wouldn't be able to reach this point anyways so
  // return NULL;
}

static void *accept_thread(void *arg) {
  int server_fd = *(int *)arg;
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);

  while (1) {
    // editing client count data
    // locking mutex
    pthread_mutex_lock(&clients_mutex);
    int count = client_count;
    pthread_mutex_unlock(&clients_mutex);

    // if both players join then exit the loop
    if (count >= 2) {
      sleep_ms(100);
      continue;
    }

    // if error then exit the loop as well
    int fd = accept(server_fd, (struct sockaddr *)&addr, &len);
    if (fd < 0) {
      perror("accept");
      continue;
    }

    // editing client count data
    // locking mutex
    pthread_mutex_lock(&clients_mutex);
    if (client_count < 2) {
      int assigned_id = client_count; // 0 for first, 1 for second

      // send the player ID back
      if (send(fd, &assigned_id, sizeof(assigned_id), MSG_NOSIGNAL) < 0) {
        close(fd);
        pthread_mutex_unlock(&clients_mutex);
        continue;
      }

      client_fd[assigned_id] = fd;
      client_count++;

      int *pid = malloc(sizeof(int));
      *pid = assigned_id;
      pthread_t tid;
      pthread_create(&tid, NULL, reader_thread, pid);
      pthread_detach(tid);

      printf("[server] client connected: %s fd=%d player=%d (total=%d)\n",
             inet_ntoa(addr.sin_addr), fd, assigned_id, client_count);

      // if we now have 2 players, start the game
      if (client_count == 2) {
        game_running = 1;
        printf("[server] both players connected — game starting!\n");
      }
    }

    // we need no more than 2 players so if others join just kick them out
    else {
      printf("[server] rejecting connection from %s — game full\n",
             inet_ntoa(addr.sin_addr));
      close(fd);
    }

    pthread_mutex_unlock(&clients_mutex);
  }

  return NULL;
}

/*

  main server

  */
int main(int argc, char *argv[]) {
  // argument check
  if (argc != 2) {
    // argv[0] = file name
    // argv[1] = port
    printf("usage: %s <port>\n", argv[0]);
    return 1;
  }

  // set port
  int PORT = atoi(argv[1]);

  // run random num generator for apple init
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

  // pre populate the snake data for both players
  // so if both join we can just use the data
  for (int i = 0; i < 2; i++) {
    initSnake(i);
    score[i] = 0;
  }

  // pre populate the apple data
  initApplePosition(&appleXarr, &appleYarr, GAME_WIDTH, GAME_HEIGHT,
                    appleAmount);
  printf("[server] game initialized: %d apples\n", appleAmount);

  // if both players aren't connected yet we wait until they do
  // the accept_thread will handle this so we spam sleep counter before so
  while (!game_running) {
    sleep_ms(50);
  }

  // we assume that both players are ready so we gonna start the game now
  //
  // we loop forever until something goes wrong (or user ctrl+c the server)
  while (1) {

    // starting tick count now
    long start = now_ms();

    pthread_mutex_lock(&game_mutex);

    // loop for both players
    for (int p = 0; p < 2; p++) {

      // movement limiter
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

      moveSnake(&snakePosX[p], &snakePosY[p], &snakeTailXarr[p],
                &snakeTailYarr[p], snakeSpeed[p], snakeDirection[p],
                snakeLength[p]);

      // apple collision detection
      for (int i = 0; i < appleAmount; i++) {
        if (appleXarr[i] == snakePosX[p] && appleYarr[i] == snakePosY[p]) {
          // pop that apple from array and deduct appleAmount
          array_pop_at(appleXarr, &appleAmount, i);

          // pop workaround
          //
          // we don't want array_pop_at to decrease the appleAmount 2 times for
          // both X and Y only one time per both X and Y so we add appleAmount
          // back as a hacky workaround
          appleAmount += 1;
          array_pop_at(appleYarr, &appleAmount, i);

          // increase snake length and score
          snakeLength[p] += 1;
          score[p] += 1;
        }
      }

      // tail collision check
      //
      // WE START FROM INDEX 1 because INDEX 0 is the current POSITION
      // SO IF WE CHECK INDEX 0 THEN WE INSTADEAD
      for (int i = 1; i < snakeLength[p]; i++) {

        if (snakeTailXarr[p][i] == snakePosX[p] &&
            snakeTailYarr[p][i] == snakePosY[p]) {

          // log
          printf("[server] p%d game over (tail hit); score=%d\n", p, score[p]);

          // reset the score and reinit since player died
          score[p] = 0;
          initSnake(p);

          // end the loop search
          break;
        }
      }

      // game boundary check
      //
      // X > GAME_WIDTH, Y > GAME_HEIGHT and X or Y is more than 0
      if (snakePosX[p] > (GAME_WIDTH - 1) || snakePosY[p] > (GAME_HEIGHT - 1)) {
        if (snakePosX[p] < 0 || snakePosY[p] < 0) {
          // log
          printf("[server] p%d game over (hit wall); score=%d\n", p, score[p]);

          score[p] = 0;
          initSnake(p);
        }
      }

      // snake to snake violence check
      //
      // if the snake hit each other then also flag it
      // we want to make your life harder
      int other = (p == 0) ? 1 : 0;
      for (int i = 0; i < snakeLength[other]; i++) {
        if (snakeTailXarr[other][i] == snakePosX[p] &&
            snakeTailYarr[other][i] == snakePosY[p]) {

          // log
          printf("[server] p%d game over (hit each other); score=%d\n", p,
                 score[p]);

          score[p] = 0;
          initSnake(p);

          break;
        }
      }
    } // finished death condition check

    // proceed to access the gamePacket
    // locking mutex
    pthread_mutex_unlock(&game_mutex);

    struct gamePacket packet;
    // zeroed out the packet to prevent accessing wrong memory while doing so
    // tbh not needed
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

    // sending the gamePacket back to both clients now
    // locking mutex
    pthread_mutex_lock(&clients_mutex);

    // send packet to both players
    for (int i = 0; i < 2; i++) {
      send(client_fd[i], &packet, sizeof(packet), MSG_NOSIGNAL);
    }

    pthread_mutex_unlock(&clients_mutex);

    long remaining = (1000 / TICK_RATE) - (now_ms() - start);
    if (remaining > 0)
      sleep_ms(remaining);
  }

  // the reader_thread calls exit(1) when a player disconnects
  // so this is just a fallback

  close(server_fd);
  return 0;

  //
}