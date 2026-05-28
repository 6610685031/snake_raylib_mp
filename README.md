# Requirements
You need to be running LINUX to run this game since it uses POSIX thread implementations (pthread).

You will also need RayLib Library to be able to run and compile the app.

Follow the instructions in this link to install RayLib on your Linux.

https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux

# GCC

```
gcc snake_server.c -o snake_server -lpthread -lrt
gcc snake_client.c -o snake_client -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
```

# CLANG

Just run make since Makefile already uses CLANG
```
make
```
