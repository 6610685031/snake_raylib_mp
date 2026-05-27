// random
#include <time.h>

// raylib
#include <raylib.h>

// standard io
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERYDARKGRAY (Color){ 25, 25, 25, 255 }

#define DEFAULT_SNAKE_SPEED 1
#define DEFAULT_DISPLAY_SCALE 2

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

int array_pop_at(int array[], int *size, int index) {
    if (index < 0 || index >= *size) {
        // fix your shits lol
        printf("error: index out of bounds\n");
        exit(0);
    }

    int popped_value = array[index];

    // shift elements left to fill the gap
    for (int i = index; i < *size - 1; i++) {
        array[i] = array[i + 1];
    }

    // decrease size
    (*size)--;

    return popped_value;
}

void initApplePosition(int (*appleX)[], int (*appleY)[], int amount) {
  // rng
  srand(time(NULL));

  // 63 = max game height, width
  for (int i = 0; i < amount; i++) {
    (*appleX)[i] = (rand() % 63) + 1;
    (*appleY)[i] = (rand() % 63) + 1;
  }
}

void initSnakePosition(int *curPosX, int *curPosY, int (*tailX)[], int (*tailY)[], int length) {
  // first index = head
  // last index = last tail
  for (int i = 0; i < length; i++) {
    (*tailX)[i] = *curPosX + i;
    (*tailY)[i] = *curPosY;
  }
}

void moveSnake(int *curPosX, int *curPosY, int (*tailX)[], int (*tailY)[], int speed, int curDir, int length) {
  if(curDir == 0) *curPosX -= speed; // LEFT
  if(curDir == 1) *curPosX += speed; // RIGHT
  if(curDir == 2) *curPosY -= speed; // UP 
  if(curDir == 3) *curPosY += speed; // DOWN

  // insert new position at front index
  insert(*tailX, &length, *curPosX);
  insert(*tailY, &length, *curPosY);
}

int main(void)
{
    // display and window settings
    const int gameWidth = 128;
    const int gameHeight = 128;

    const int screenWidth = 512;
    const int screenHeight = 512;

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

    // snake position
    int snakePositionX = gameWidth / 2 / DEFAULT_DISPLAY_SCALE;
    int snakePositionY = gameHeight / 2 / DEFAULT_DISPLAY_SCALE;

    // current direction of snake: 0 = left, 1 = right, 2 = up, 3 = down
    int snakeDirection = 0;
    // int previousDirection;

    // current snake speed
    int snakeSpeed = DEFAULT_SNAKE_SPEED;

    initSnakePosition(&snakePositionX, &snakePositionY, &snakeTailXarr, &snakeTailYarr, snakeLength);
    initApplePosition(&appleXarr, &appleYarr, appleAmount);

    while (!WindowShouldClose())
    {
      moveSnake(&snakePositionX, &snakePositionY, &snakeTailXarr, &snakeTailYarr, snakeSpeed, snakeDirection, snakeLength);

      // UNUSED: // movement key detection
      // UNUSED: previousDirection = snakeDirection;

      // movement limiter

      // if left or right (on horizontal line)
      if (snakeDirection == 0 || snakeDirection == 1) {
        if (IsKeyDown(KEY_UP)) snakeDirection = 2;
        if (IsKeyDown(KEY_DOWN)) snakeDirection = 3;
      } else {
        // if up or down (on vertical line)
        if (IsKeyDown(KEY_LEFT)) snakeDirection = 0;
        if (IsKeyDown(KEY_RIGHT)) snakeDirection = 1;
      }

      // apple collision detection
      for(int i = 0; i < appleAmount; i++) {
        if(appleXarr[i] == snakePositionX && appleYarr[i] == snakePositionY) {
          // pop that apple from array and deduct appleAmount
          array_pop_at(appleXarr, &appleAmount, i);

          // pop workaround
          //
          // we don't want array_pop_at to decrease the appleAmount 2 times for both X and Y
          // only one time per both X and Y
          // so we add appleAmount back as a hacky workaround
          appleAmount += 1;
          array_pop_at(appleYarr, &appleAmount, i);

          // increase snake length and score
          snakeLength += 1;
          score += 1;
        }
      }

      // tail collision check
      // START FROM INDEX 1 because skip the current POSITION
      // SO IT WON'T KILL US INSTANTLY
      for(int i = 1; i < snakeLength; i++) {
        if(snakeTailXarr[i] == snakePositionX && snakeTailYarr[i] == snakePositionY) {
          // Game over
          exit(0);
        }
      }

      // game canvas draw
      BeginTextureMode(target);

        ClearBackground(VERYDARKGRAY);
        // draw snake from snake tail arrays
        for(int i = 0; i < snakeLength; i++) {
          DrawPixel(snakeTailXarr[i], snakeTailYarr[i], RED);
        }
        for(int i = 0; i < appleAmount; i++) {
          DrawPixel(appleXarr[i], appleYarr[i], YELLOW);
        }

      EndTextureMode();
      
      // window canvas draw
      BeginDrawing();

        ClearBackground(VERYDARKGRAY);
        Rectangle sourceRec = { 0.0f, 0.0f, (float) target.texture.width, (float) -target.texture.height };
        Rectangle destRec = { 0.0f, 0.0f, (float) GetScreenWidth() * DEFAULT_DISPLAY_SCALE, (float) GetScreenHeight() * DEFAULT_DISPLAY_SCALE};
        Vector2 origin = { 0.0f, 0.0f };
            
        DrawTexturePro(target.texture, sourceRec, destRec, origin, 0.0f, WHITE);

        // convert score to string to output the score
        char score_str[20];
        char score_an_str[20] = "SCORE: ";
        snprintf(score_str, sizeof(score_str), "%d", score);
        strcat(score_an_str, score_str);

        DrawText(score_an_str, 25, 25, 20, RAYWHITE);

      EndDrawing();
    }

    UnloadRenderTexture(target);
    CloseWindow();

    return 0;
}