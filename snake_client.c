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

#define VERYDARKGRAY (Color){25, 25, 25, 255}

static void *sender_thread(void *arg) {
  int fd = *(int *)arg;
  int seq = 0;
  char buf[128];

  while (1) {
    sleep(2); /* send a message every 2 seconds */
    int n = snprintf(buf, sizeof(buf), "current direction: %d\n", seq);
    if (send(fd, buf, n, 0) < 0)
      break;
  }
  return NULL;
}

void create_socket_connection(int port, char host[], int *curDir) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
  };
  inet_pton(AF_INET, host, &addr.sin_addr);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    exit(0);
  }
  printf("[client] connected to %s:%d\n", host, port);

  pthread_t tid;
  pthread_create(&tid, NULL, sender_thread, &fd);
  pthread_detach(tid);

  char buf[512];
  ssize_t n;
  while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
    buf[n] = '\0';
    printf("[client] %s", buf);
  }

  puts("[client] disconnected");
  close(fd);
}

int main(void) {
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

  RenderTexture2D target = LoadRenderTexture(gameWidth, gameHeight);

  // GAME

  // score
  int score = 0;

  // apple amount
  int appleAmount = 10;
  // apple arrays
  int appleXarr[100];
  int appleYarr[100];

  // snake tail length
  int snakeLength = 5;
  // snake tail arrays
  int snakeTailXarr[100];
  int snakeTailYarr[100];

  // snake direction
  int snakeDirection = 0;

  // game loop
  while (!WindowShouldClose()) {

    // create socket connection to port
    create_socket_connection(8080, "127.0.0.1", &snakeDirection);

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

    // ClearBackground(BLUE);
    DrawRectangle(0, 0, gameWidth, gameHeight, BLUE);

    // draw snake from snake tail arrays
    for (int i = 0; i < snakeLength; i++) {
      DrawPixel(snakeTailXarr[i], snakeTailYarr[i], RED);
    }
    for (int i = 0; i < appleAmount; i++) {
      DrawPixel(appleXarr[i], appleYarr[i], YELLOW);
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
    snprintf(score_str, sizeof(score_str), "%d", score);
    strcat(score_an_str, score_str);

    DrawText(score_an_str, fontHeight / 2, fontHeight / 6, 20, RAYWHITE);

    EndDrawing();
  }

  UnloadRenderTexture(target);
  CloseWindow();

  return 0;
}