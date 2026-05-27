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

/* data shared between threads*/

struct gamePacket {
  int score;
  int appleAmount;
  int appleXarr[100];
  int appleYarr[100];
  int snakeLength;
  int snakeTailXarr[100];
  int snakeTailYarr[100];
};

struct gamePacket gP; // gamePacket

// snake direction
int snakeDirection = 0;

static void *sender_thread(void *arg) {
  int fd = *(int *)arg;

  while (1) {
    if (send(fd, (void *)&snakeDirection, sizeof(snakeDirection), 0) < 0)
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

  // windows
  InitWindow(screenWidth, screenHeight, "Basically A Snake Game");
  SetTargetFPS(15);

  // texture2D
  RenderTexture2D target = LoadRenderTexture(gameWidth, gameHeight);

  // game loop
  while (!WindowShouldClose()) {

    // if left or right (on horizontal line)
    if (snakeDirection == 0 || snakeDirection == 1) {
      if (IsKeyDown(KEY_UP))
        snakeDirection = 2;
      if (IsKeyDown(KEY_DOWN))
        snakeDirection = 3;
    } else {
      // if up or down (on vertical line)
      if (IsKeyDown(KEY_LEFT))
        snakeDirection = 0;
      if (IsKeyDown(KEY_RIGHT))
        snakeDirection = 1;
    }

    // game canvas draw
    BeginTextureMode(target);
    DrawRectangle(0, 0, gameWidth, gameHeight, BLUE);

    // draw snake from snake tail arrays
    for (int i = 0; i < gP.snakeLength; i++) {
      DrawPixel(gP.snakeTailXarr[i], gP.snakeTailYarr[i], RED);
    }
    for (int i = 0; i < gP.appleAmount; i++) {
      DrawPixel(gP.appleXarr[i], gP.appleYarr[i], YELLOW);
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

    // convert score to string to output the score
    char score_str[20];
    char score_an_str[20] = "SCORE: ";
    snprintf(score_str, sizeof(score_str), "%d", gP.score);
    strcat(score_an_str, score_str);

    DrawText(score_an_str, fontHeight / 2, fontHeight / 6, 20, RAYWHITE);

    EndDrawing();
  }

  UnloadRenderTexture(target);
  CloseWindow();

  exit(0);

  return NULL;
}

int main(void) {
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

  pthread_t renderer_tid, sender_tid;

  // create the renderer thread (raylib)
  pthread_create(&renderer_tid, NULL, renderer_thread, NULL);
  pthread_detach(renderer_tid);

  // create the sender thread
  pthread_create(&sender_tid, NULL, sender_thread, &fd);
  pthread_detach(sender_tid);

  ssize_t n;
  while ((n = recv(fd, (char *)&gP, sizeof(gP), 0)) > 0) {
  }

  puts("[client] disconnected");
  close(fd);

  return 0;
}