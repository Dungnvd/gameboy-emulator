// Don't let SDL rename main() on Windows
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>

namespace {
    constexpr int kScreenW = 160;
    constexpr int kScreenH = 144;
    constexpr int kScale = 4;

    struct Color {
        Uint8 r, g, b;
    };

    constexpr std::array<Color, 4> kPalette = { {
        {0x0F, 0x38, 0x0F},
        {0x30, 0x62, 0x30},
        {0x8B, 0xAC, 0x0F},
        {0x9B, 0xBC, 0x0F}
        } };

    using WindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using RendererPtr = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

    int run() {
        WindowPtr window(
            SDL_CreateWindow("gbemu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreenW * kScale, kScreenH * kScale, 0),
            &SDL_DestroyWindow
        );
        if (!window) {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return 1;
        }

        RendererPtr renderer(
            SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC),
            &SDL_DestroyRenderer
        );
        if (!renderer) {
            std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return 1;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui_ImplSDL2_InitForSDLRenderer(window.get(), renderer.get());
        ImGui_ImplSDLRenderer2_Init(renderer.get());

        std::size_t colorIndex = kPalette.size() - 1;
        bool running = true;

        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) {
                    running = false;
                }
                else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && !ImGui::GetIO().WantCaptureKeyboard) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    else if (e.key.keysym.sym == SDLK_SPACE) {
                        colorIndex = (colorIndex + 1) % kPalette.size();
                    }
                }
            }

            // Start a new ImGui frame
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            // Describe UI
            ImGui::Begin("Debug");
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Color index: %zu", colorIndex);
            if (ImGui::Button("Next color")) {
                colorIndex = (colorIndex + 1) % kPalette.size();
            }
            if (ImGui::Button("Previous color")) {
                colorIndex = (colorIndex + kPalette.size() - 1) % kPalette.size();
            }
            ImGui::End();

            // Draw: background first, then ImGui on top
            ImGui::Render();
            const Color& c = kPalette[colorIndex];
            SDL_SetRenderDrawColor(renderer.get(), c.r, c.g, c.b, 0xFF);
            SDL_RenderClear(renderer.get());
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer.get());
            SDL_RenderPresent(renderer.get());
        }

        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        return 0;
    }
}

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int result = run();
    SDL_Quit();

    return result;

}
