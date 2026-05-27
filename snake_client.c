#define _POSIX_C_SOURCE 199309L

// raylib
#include <pthread.h>
#include <raylib.h>

// thread
#include <bits/pthread_stack_min.h>

// standard io
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// color
#define VERYDARKGRAY (Color){25, 25, 25, 255}
// host, port
#define HOST "127.0.0.1"
#define PORT 8080
#define NUM_PLAYERS 2

/* data shared between threads */
struct gamePacket {
  int playerCount;
  int score[NUM_PLAYERS];
  int appleAmount;
  int appleXarr[100];
  int appleYarr[100];
  int snakeLength[NUM_PLAYERS];
  int snakeTailXarr[NUM_PLAYERS][100];
  int snakeTailYarr[NUM_PLAYERS][100];
};
struct gamePacket gamePacket; // gamePacket

struct sendPacket {
  int snakeDirection;
};
struct sendPacket sendPacket; // sendPacket

/* player ID assigned by server (-1 = spectator) */
static int myPlayerId = -1;

// nanosleep helper
static void sleep_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

/* thread stuffs*/
static void *sender_thread(void *arg) {
  int fd = *(int *)arg;
  while (1) {
    // send direction struct to the server (~15 Hz, matching server tick rate)
    sleep_ms(66);
    if (send(fd, (char *)&sendPacket, sizeof(sendPacket), 0) < 0)
      break;
  }
  return NULL;
}

static void *renderer_thread() {
  // game width x height (in this case by default 64x64)
  const int gameWidth = 64;
  const int gameHeight = 64;

  // leave space for font
  const int fontHeight = 30;

  // actual display width x height (will be scaled up)
  const int screenWidth = 512;
  const int screenHeight = 512 + fontHeight;

  // snake colors: player 0 = RED, player 1 = GREEN
  Color snakeColors[NUM_PLAYERS] = {RED, GREEN};

  // windows
  InitWindow(screenWidth, screenHeight, "Basically A Snake Game");
  SetTargetFPS(15);

  // texture2D
  RenderTexture2D target = LoadRenderTexture(gameWidth, gameHeight);

  // game loop
  while (!WindowShouldClose()) {

    // if left or right (on horizontal line)
    if (sendPacket.snakeDirection == 0 || sendPacket.snakeDirection == 1) {
      if (IsKeyDown(KEY_UP))
        sendPacket.snakeDirection = 2;
      if (IsKeyDown(KEY_DOWN))
        sendPacket.snakeDirection = 3;
    } else {
      // if up or down (on vertical line)
      if (IsKeyDown(KEY_LEFT))
        sendPacket.snakeDirection = 0;
      if (IsKeyDown(KEY_RIGHT))
        sendPacket.snakeDirection = 1;
    }

    // game canvas draw
    BeginTextureMode(target);
    DrawRectangle(0, 0, gameWidth, gameHeight, BLUE);

    // draw all players' snakes
    for (int p = 0; p < NUM_PLAYERS; p++) {
      for (int i = 0; i < gamePacket.snakeLength[p]; i++) {
        DrawPixel(gamePacket.snakeTailXarr[p][i],
                  gamePacket.snakeTailYarr[p][i], snakeColors[p]);
      }
    }

    // draw apples
    for (int i = 0; i < gamePacket.appleAmount; i++) {
      DrawPixel(gamePacket.appleXarr[i], gamePacket.appleYarr[i], YELLOW);
    }

    EndTextureMode();

    // window canvas draw
    BeginDrawing();

    ClearBackground(VERYDARKGRAY);
    Rectangle sourceRec = {0.0f, 0.0f, (float)target.texture.width,
                           (float)-target.texture.height};
    // make space for font
    Rectangle destRec = {0.0f, 0.0f, (float)(screenWidth),
                         (float)(screenHeight - fontHeight)};
    Vector2 origin = {0.0f, (float)-fontHeight};

    DrawTexturePro(target.texture, sourceRec, destRec, origin, 0.0f, WHITE);

    // display both players' scores
    char score_str[64];
    snprintf(score_str, sizeof(score_str), "P1: %d   P2: %d",
             gamePacket.score[0], gamePacket.score[1]);

    DrawText(score_str, fontHeight / 2, fontHeight / 6, 20, RAYWHITE);

    // show own player ID on the right side
    if (myPlayerId >= 0) {
      char id_str[32];
      snprintf(id_str, sizeof(id_str), "YOU: P%d", myPlayerId + 1);
      int textWidth = MeasureText(id_str, 20);
      DrawText(id_str, screenWidth - textWidth - 10, fontHeight / 6, 20,
               snakeColors[myPlayerId]);
    } else {
      int textWidth = MeasureText("SPECTATOR", 20);
      DrawText("SPECTATOR", screenWidth - textWidth - 10, fontHeight / 6, 20,
               GRAY);
    }

    EndDrawing();
  }

  UnloadRenderTexture(target);
  CloseWindow();

  exit(0);

  return NULL;
}

int main(void) {
  // create socket and connect to specified host, port
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(PORT),
  };
  inet_pton(AF_INET, HOST, &addr.sin_addr);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    exit(0);
  }
  printf("[client] connected to %s:%d\n", HOST, PORT);

  /* receive player ID handshake from server */
  ssize_t n = recv(fd, (char *)&myPlayerId, sizeof(myPlayerId), 0);
  if (n <= 0) {
    puts("[client] failed to receive player ID");
    close(fd);
    return 1;
  }
  printf("[client] assigned player ID: %d (%s)\n", myPlayerId,
         myPlayerId >= 0 ? (myPlayerId == 0 ? "P1" : "P2") : "spectator");

  /* set initial direction based on player ID */
  if (myPlayerId == 0)
    sendPacket.snakeDirection = 1; /* P1 starts facing right */
  else if (myPlayerId == 1)
    sendPacket.snakeDirection = 0; /* P2 starts facing left */

  pthread_t renderer_tid, sender_tid;

  // create the renderer thread (raylib)
  pthread_create(&renderer_tid, NULL, renderer_thread, NULL);
  pthread_detach(renderer_tid);

  // create the sender thread
  pthread_create(&sender_tid, NULL, sender_thread, &fd);
  pthread_detach(sender_tid);

  // recieving gamePacket data from the server and put in inside gamePacket
  // struct
  while ((n = recv(fd, (char *)&gamePacket, sizeof(gamePacket), 0)) > 0) {
    // this will update the already existing gamePacket struct to match the data
    // from the server
  }

  puts("[client] disconnected");
  close(fd);

  return 0;
}