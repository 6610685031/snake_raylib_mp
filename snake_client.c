#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <pthread.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
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
struct gamePacket gamePacket; // gamePacket

struct sendPacket {
  int snakeDirection;
};
struct sendPacket sendPacket; // sendPacket

/* static initializer data */
static int playerId; // playerId

/*

  precise timing modules

*/

// sleep (precise)
static void sleep_ms(long ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

/*

  thread stuffs

*/
static void *sender_thread(void *arg) {
  int fd = *(int *)arg;
  while (1) {
    // send direction struct to the server
    // (by default about 15 times per sec or 15hz, you can change it though)
    // worst-effort way to sync tick rate with server
    // doesn't really works at times but it's good enough

    // calculate tick sleep time from tick rate
    sleep_ms(1000 / TICK_RATE);
    if (send(fd, (char *)&sendPacket, sizeof(sendPacket), 0) < 0)
      break;
  }

  close(fd);
  return NULL;
}

static void *renderer_thread(void *arg) {
  // void cast to arg because it's unused
  // GCC wouldn't compile if i didn't insert "void *arg" to *renderer_thread
  // GCC is very annoying sometimes
  (void)arg;

  // game width x height
  // by default it's 64x64, could be changed
  // the dimension should be 1:1 so that it's easier to calculate (not really)
  const int gameWidth = GAME_WIDTH;
  const int gameHeight = GAME_HEIGHT;

  // leave space for font
  const int fontHeight = 30;

  // actual display width x height (will be scaled up)
  const int screenWidth = 512;
  const int screenHeight = 512 + fontHeight;

  // snake colors: player 0 = RED, player 1 = GREEN
  Color snakeColors[2] = {RED, GREEN};

  // windows
  InitWindow(screenWidth, screenHeight, "basically a snake game");

  // we try to match the game fps with the the tick rate value
  // it won't be fully 100% in sync but it should work well enough
  SetTargetFPS(TICK_RATE);

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

    // draw if the both players joined
    if (gamePacket.playerCount == 2) {
      DrawRectangle(0, 0, gameWidth, gameHeight, BLUE);

      // draw both players snakes
      for (int player = 0; player < 2; player++) {
        // loop player 1, 2
        for (int i = 0; i < gamePacket.snakeLength[player]; i++) {
          DrawPixel(gamePacket.snakeTailXarr[player][i],
                    gamePacket.snakeTailYarr[player][i], snakeColors[player]);
        }
      }

      // draw apples
      for (int i = 0; i < gamePacket.appleAmount; i++) {
        DrawPixel(gamePacket.appleXarr[i], gamePacket.appleYarr[i], YELLOW);
      }
    }

    EndTextureMode();

    // window canvas draw
    BeginDrawing();

    ClearBackground(BLACK);
    Rectangle sourceRec = {0.0f, 0.0f, (float)target.texture.width,
                           (float)-target.texture.height};
    // make space for font
    Rectangle destRec = {0.0f, 0.0f, (float)(screenWidth),
                         (float)(screenHeight - fontHeight)};
    Vector2 origin = {0.0f, (float)-fontHeight};

    // draw if the both players joined
    if (gamePacket.playerCount == 2) {
      DrawTexturePro(target.texture, sourceRec, destRec, origin, 0.0f, WHITE);

      // display both players scores
      char score_str[64];
      snprintf(score_str, sizeof(score_str), "P1: %d   P2: %d",
               gamePacket.score[0], gamePacket.score[1]);
      DrawText(score_str, fontHeight / 2, fontHeight / 6, 20, RAYWHITE);
    } else {

      // display text for waiting
      DrawText("waiting for players to join...", fontHeight / 2, fontHeight / 6,
               20, RAYWHITE);
    }

    EndDrawing();
  }

  UnloadRenderTexture(target);
  CloseWindow();
  exit(0);

  return NULL;
}

/*

  main client

*/
int main(int argc, char *argv[]) {
  // argument check
  if (argc != 3) {
    // argv[0] = file name
    // argv[1] = server ip
    // argv[2] = port
    printf("usage: %s <server-ip> <port>\n", argv[0]);
    return 1;
  }

  // set ip and port
  char *HOST = argv[1];
  int PORT = atoi(argv[2]);

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

  // receive player id from server
  ssize_t n = recv(fd, (char *)&playerId, sizeof(playerId), 0);
  if (n <= 0) {
    puts("[client] failed to receive player ID");
    close(fd);
    return 1;
  }
  printf("[client] assigned player ID: %d", playerId);

  // set direction based on player id
  // player 0 = right, player 1 = left
  if (playerId == 0)
    sendPacket.snakeDirection = 1;
  else if (playerId == 1)
    sendPacket.snakeDirection = 0;

  pthread_t renderer_tid, sender_tid;

  // create the renderer thread (raylib)
  pthread_create(&renderer_tid, NULL, renderer_thread, NULL);
  pthread_detach(renderer_tid);

  // create the sender thread
  pthread_create(&sender_tid, NULL, sender_thread, &fd);
  pthread_detach(sender_tid);

  // recieving gamePacket data from the server
  // and put in inside gamePacket struct
  while ((n = recv(fd, (char *)&gamePacket, sizeof(gamePacket), 0)) > 0) {
    // this will update the already existing gamePacket struct to match the data
    // from the server
  }

  puts("[client] disconnected");
  close(fd);

  return 0;
}