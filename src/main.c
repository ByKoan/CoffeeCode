 // *** Linea para compilar el programa *** 
 // gcc main.c -o main.exe -I C:/SDL3/include -L C:/SDL3/lib -lSDL3


 #define SDL_MAIN_USE_CALLBACKS 1  /* Usamos las callbacks en vez de main() */
 #include <SDL3/SDL.h>
 #include <SDL3/SDL_main.h>
 #include <SDL3/SDL_gpu.h>
 
 /* Usaremos este render para dibujar cada frame. */
 static SDL_Window *window = NULL;
 static SDL_Renderer *renderer = NULL;
 
 /* Esta funcion se ejecutara una vez al iniciar el porgrama. */
 SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
 {
     SDL_SetAppMetadata("Coffee Code", "v1.0", "com.example.coffeecode");
 
     if (!SDL_Init(SDL_INIT_VIDEO)) {
         SDL_Log("SDL: No se ha podido iniciar. %s", SDL_GetError());
         return SDL_APP_FAILURE;
     }

     int WindowHeight = 1000;
     int WindowWidth = 1000;

     if (!SDL_CreateWindowAndRenderer("CoffeeCode", WindowHeight, WindowWidth, SDL_WINDOW_RESIZABLE,  &window, &renderer)) {
         SDL_Log("SDL: No se ha podido crear la ventana/render. %s", SDL_GetError());
         return SDL_APP_FAILURE;
     }

     SDL_SetGPUAllowedFramesInFlight(NULL, 60); /* La aplicacion cargara hasta 60 frames previos */

 
     return SDL_APP_CONTINUE;  
 }
 
 /* Esta funcion se iniciara cuando ocurra algun nuevo evento */
 SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
 {
     if (event->type == SDL_EVENT_QUIT) {
         return SDL_APP_SUCCESS;  /* Finaliza el programa mandandole un "success" al SO */
     }
     return SDL_APP_CONTINUE; 
 }
 
 /* El corazon del programa, esta funcion se ejecuta frame a frame */
 SDL_AppResult SDL_AppIterate(void *appstate) {

    static Uint64 last_frame_time = 0; // Variable estática para almacenar el tiempo del último frame
    const int TARGET_FPS = 60; // Frames por segundo deseados
    const int FRAME_DELAY_MS = 1000 / TARGET_FPS; // Tiempo de espera entre frames en milisegundos

    Uint64 current_time = SDL_GetTicks(); // Obtiene el tiempo actual desde que se inició SDL
    if (last_frame_time == 0) {
        last_frame_time = current_time;
    }

    Uint64 elapsed_time = current_time - last_frame_time;

    if (elapsed_time < FRAME_DELAY_MS) {
        SDL_Delay(FRAME_DELAY_MS - elapsed_time);
        current_time = SDL_GetTicks(); // actualiza el tiempo real después de la espera
    }

    last_frame_time = current_time;

    // === Renderizado (no se modifica) ===
    const double now = ((double)current_time) / 1000.0;
    const float red = (float)(0.5 + 0.5 * SDL_sin(now));
    const float green = (float)(0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 2 / 3));
    const float blue = (float)(0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 4 / 3));
    SDL_SetRenderDrawColorFloat(renderer, red, green, blue, SDL_ALPHA_OPAQUE_FLOAT);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    //SDL_DestroyRenderer(renderer);
    //SDL_DestroyWindow(window);
    //SDL_Quit();

    return SDL_APP_CONTINUE;
}
 
 /* This function runs once at shutdown. */
 void SDL_AppQuit(void *appstate, SDL_AppResult result)
 {
     /* SDL Borrar la aplicacion por nosotros */
 }
 