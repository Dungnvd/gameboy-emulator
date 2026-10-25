#include <SDL.h>
#include <cstdio>

int main(int argc, char* argv[]){
    (void)argc;
    (void)argv;

    constexpr int kScreenW = 160;
    constexpr int KScreenH = 144;
    constexpr int kScale = 4;

    if(SDL_Init(SDL_INIT_VIDEO) != 0){
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "gbemu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kScreenW * kScale, KScreenH * kScale,0
    );

    if(!window){
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if(!renderer){
        std::fprintf(stderr, "SDL_CreateREndere failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT) running = false;
            if(e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        SDL_SetRenderDrawColor(renderer, 0x9B, 0xBC, 0x0F, 0xFF);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}