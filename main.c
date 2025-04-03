#define SDL_MAIN_USE_CALLBACKS 1 // replace main() with SDL_AppInit()

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

// StartUp of the application 

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("CoffeCode", "1.0", "com.coffeecode.sdl3");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("CoffeCode: Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
        // Check if the window and renderer are created successfully
    }
    
}