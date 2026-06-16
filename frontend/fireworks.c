#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define MAX_FIREWORKS 30
#define MAX_PARTICLES 300
#define MAX_RECORDED_EVENTS 2000
#define MAX_PALETTE_SIZE 200

typedef struct {
    float x, y;
    float vx, vy;
    int r, g, b, a;
    float life;
    float max_life;
    int active;
} Particle;

typedef struct {
    float x, y;
    float targetX, targetY;
    float vx, vy;
    int r, g, b;
    int exploded;
    int active;
    Particle particles[MAX_PARTICLES];
} Firework;

typedef struct {
    int frame;
    float x;
    float targetX;
    float targetY;
    float vx;
    float vy;
    int r, g, b;
    int palette_index;
} FireworkEvent;

typedef enum {
    MODE_IDLE = 0,
    MODE_RECORDING = 1,
    MODE_PLAYING = 2
} PlaybackMode;

SDL_Window *window;
SDL_Renderer *renderer;
Firework fireworks[MAX_FIREWORKS];
int screen_width = 0;
int screen_height = 0;

int frame_counter = 0;
PlaybackMode current_mode = MODE_IDLE;

int record_seed = 0;
int record_width = 0;
int record_height = 0;

FireworkEvent recorded_events[MAX_RECORDED_EVENTS];
int recorded_event_count = 0;

int palette[MAX_PALETTE_SIZE][3];
int palette_size = 0;

int play_event_index = 0;
float play_scale_x = 1.0f;
float play_scale_y = 1.0f;

float random_float(float min, float max) {
    return min + (float)rand() / ((float)RAND_MAX / (max - min));
}

void hsl_to_rgb(float h, float s, float l, int *r, int *g, int *b) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - 0.5f * c;
    float r_temp, g_temp, b_temp;

    if (h >= 0 && h < 60) { r_temp = c; g_temp = x; b_temp = 0; }
    else if (h >= 60 && h < 120) { r_temp = x; g_temp = c; b_temp = 0; }
    else if (h >= 120 && h < 180) { r_temp = 0; g_temp = c; b_temp = x; }
    else if (h >= 180 && h < 240) { r_temp = 0; g_temp = x; b_temp = c; }
    else if (h >= 240 && h < 300) { r_temp = x; g_temp = 0; b_temp = c; }
    else { r_temp = c; g_temp = 0; b_temp = x; }

    *r = (int)((r_temp + m) * 255);
    *g = (int)((g_temp + m) * 255);
    *b = (int)((b_temp + m) * 255);
}

int add_to_palette(int r, int g, int b) {
    for (int i = 0; i < palette_size; i++) {
        if (palette[i][0] == r && palette[i][1] == g && palette[i][2] == b) {
            return i;
        }
    }
    if (palette_size < MAX_PALETTE_SIZE) {
        palette[palette_size][0] = r;
        palette[palette_size][1] = g;
        palette[palette_size][2] = b;
        palette_size++;
        return palette_size - 1;
    }
    return 0;
}

void init_particle(Particle *p, float x, float y, int r, int g, int b) {
    p->x = x;
    p->y = y;
    float angle = random_float(0, M_PI * 2);
    float speed = random_float(2.0f, 6.0f);
    p->vx = cos(angle) * speed;
    p->vy = sin(angle) * speed;
    p->r = r;
    p->g = g;
    p->b = b;
    p->a = 255;
    p->life = random_float(40, 80);
    p->max_life = p->life;
    p->active = 1;
}

void init_firework_from_event(Firework *f, FireworkEvent *evt) {
    if (current_mode == MODE_PLAYING) {
        f->x = evt->x * play_scale_x;
        f->targetX = evt->targetX * play_scale_x;
        f->targetY = evt->targetY * play_scale_y;
        f->y = screen_height;
        f->vx = evt->vx * play_scale_x;
        f->vy = evt->vy * play_scale_y;
    } else {
        f->x = evt->x;
        f->targetX = evt->targetX;
        f->targetY = evt->targetY;
        f->y = screen_height;
        f->vx = evt->vx;
        f->vy = evt->vy;
    }
    f->r = evt->r;
    f->g = evt->g;
    f->b = evt->b;
    f->exploded = 0;
    f->active = 1;
}

void init_firework(Firework *f) {
    f->x = random_float(100, screen_width - 100);
    f->y = screen_height;
    f->targetX = f->x + random_float(-100, 100);
    f->targetY = random_float(100, screen_height * 0.4);

    float angle = atan2(f->targetY - f->y, f->targetX - f->x);
    float speed = random_float(10.0f, 15.0f);

    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed;

    float h = random_float(0, 360);
    hsl_to_rgb(h, 1.0f, 0.5f, &f->r, &f->g, &f->b);

    f->exploded = 0;
    f->active = 1;

    if (current_mode == MODE_RECORDING && recorded_event_count < MAX_RECORDED_EVENTS) {
        FireworkEvent evt;
        evt.frame = frame_counter;
        evt.x = f->x;
        evt.targetX = f->targetX;
        evt.targetY = f->targetY;
        evt.vx = f->vx;
        evt.vy = f->vy;
        evt.r = f->r;
        evt.g = f->g;
        evt.b = f->b;
        evt.palette_index = add_to_palette(f->r, f->g, f->b);
        recorded_events[recorded_event_count++] = evt;
    }
}

void update() {
    frame_counter++;

    if (current_mode == MODE_PLAYING) {
        while (play_event_index < recorded_event_count &&
               recorded_events[play_event_index].frame <= frame_counter) {
            for (int i = 0; i < MAX_FIREWORKS; i++) {
                if (!fireworks[i].active) {
                    init_firework_from_event(&fireworks[i], &recorded_events[play_event_index]);
                    break;
                }
            }
            play_event_index++;
        }

        if (play_event_index >= recorded_event_count) {
            int all_done = 1;
            for (int i = 0; i < MAX_FIREWORKS; i++) {
                if (fireworks[i].active) {
                    all_done = 0;
                    break;
                }
            }
            if (all_done) {
                current_mode = MODE_IDLE;
            }
        }
    } else {
        if (rand() % 10 == 0) {
            for (int i = 0; i < MAX_FIREWORKS; i++) {
                if (!fireworks[i].active) {
                    init_firework(&fireworks[i]);
                    break;
                }
            }
        }
    }

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) continue;

        Firework *f = &fireworks[i];

        if (!f->exploded) {
            f->x += f->vx;
            f->y += f->vy;
            f->vy += 0.15;

            if (f->vy >= 0 || f->y <= f->targetY) {
                f->exploded = 1;
                for (int j = 0; j < MAX_PARTICLES; j++) {
                    init_particle(&f->particles[j], f->x, f->y, f->r, f->g, f->b);
                }
            }
        } else {
            int active_particles = 0;
            for (int j = 0; j < MAX_PARTICLES; j++) {
                if (f->particles[j].active) {
                    active_particles++;
                    Particle *p = &f->particles[j];

                    p->x += p->vx;
                    p->y += p->vy;
                    p->vx *= 0.96;
                    p->vy *= 0.96;
                    p->vy += 0.15;

                    p->life -= 1.0f;

                    float alpha_ratio = p->life / p->max_life;
                    p->a = (int)(alpha_ratio * 255);

                    if (p->life <= 0) {
                        p->active = 0;
                    }
                }
            }
            if (active_particles == 0) {
                f->active = 0;
            }
        }
    }
}

void draw() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 25);
    SDL_Rect rect = {0, 0, screen_width, screen_height};
    SDL_RenderFillRect(renderer, &rect);

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) continue;

        Firework *f = &fireworks[i];

        if (!f->exploded) {
            SDL_SetRenderDrawColor(renderer, f->r, f->g, f->b, 255);
            SDL_Rect r = {(int)f->x - 2, (int)f->y - 2, 4, 4};
            SDL_RenderFillRect(renderer, &r);
        } else {
            for (int j = 0; j < MAX_PARTICLES; j++) {
                if (f->particles[j].active) {
                    Particle *p = &f->particles[j];
                    SDL_SetRenderDrawColor(renderer, p->r, p->g, p->b, p->a);
                    SDL_Rect p_rect = {(int)p->x - 1, (int)p->y - 1, 3, 3};
                    SDL_RenderFillRect(renderer, &p_rect);
                }
            }
        }
    }

    SDL_RenderPresent(renderer);
}

void main_loop() {
    update();
    draw();
}

EMSCRIPTEN_KEEPALIVE
void start_recording() {
    current_mode = MODE_RECORDING;
    frame_counter = 0;
    recorded_event_count = 0;
    palette_size = 0;
    record_seed = time(NULL);
    srand(record_seed);
    record_width = screen_width;
    record_height = screen_height;

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].active = 0;
    }

    printf("Recording started: seed=%d, size=%dx%d\n", record_seed, record_width, record_height);
}

EMSCRIPTEN_KEEPALIVE
void stop_recording() {
    current_mode = MODE_IDLE;
    printf("Recording stopped: %d events, %d palette colors\n", recorded_event_count, palette_size);
}

EMSCRIPTEN_KEEPALIVE
int get_recorded_data(char *buffer, int buffer_size) {
    if (buffer == NULL || buffer_size <= 0) return 0;

    int offset = 0;

    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\n"
        "  \"seed\": %d,\n"
        "  \"width\": %d,\n"
        "  \"height\": %d,\n"
        "  \"duration_frames\": %d,\n"
        "  \"palette_size\": %d,\n"
        "  \"palette\": [\n",
        record_seed, record_width, record_height, frame_counter, palette_size);

    for (int i = 0; i < palette_size; i++) {
        offset += snprintf(buffer + offset, buffer_size - offset,
            "    [%d, %d, %d]%s\n",
            palette[i][0], palette[i][1], palette[i][2],
            (i < palette_size - 1) ? "," : "");
    }

    offset += snprintf(buffer + offset, buffer_size - offset,
        "  ],\n"
        "  \"event_count\": %d,\n"
        "  \"events\": [\n",
        recorded_event_count);

    for (int i = 0; i < recorded_event_count; i++) {
        FireworkEvent *e = &recorded_events[i];
        offset += snprintf(buffer + offset, buffer_size - offset,
            "    {\"frame\": %d, \"x\": %.2f, \"targetX\": %.2f, \"targetY\": %.2f, "
            "\"vx\": %.4f, \"vy\": %.4f, \"r\": %d, \"g\": %d, \"b\": %d, \"palette_idx\": %d}%s\n",
            e->frame, e->x, e->targetX, e->targetY,
            e->vx, e->vy, e->r, e->g, e->b, e->palette_index,
            (i < recorded_event_count - 1) ? "," : "");
    }

    offset += snprintf(buffer + offset, buffer_size - offset,
        "  ]\n"
        "}\n");

    return offset;
}

EMSCRIPTEN_KEEPALIVE
int start_playback(const char *json_data) {
    if (json_data == NULL || strlen(json_data) == 0) {
        return 0;
    }

    const char *p = json_data;
    int w = 0, h = 0;
    int ev_count = 0;

    p = strstr(p, "\"width\":");
    if (p) { sscanf(p, "\"width\": %d", &w); }
    p = strstr(p, "\"height\":");
    if (p) { sscanf(p, "\"height\": %d", &h); }
    p = strstr(p, "\"event_count\":");
    if (p) { sscanf(p, "\"event_count\": %d", &ev_count); }

    record_width = w;
    record_height = h;
    play_scale_x = (float)screen_width / (float)w;
    play_scale_y = (float)screen_height / (float)h;

    recorded_event_count = 0;
    palette_size = 0;

    const char *evt_start = strstr(json_data, "\"events\":");
    if (evt_start) {
        const char *brace = evt_start;
        while ((brace = strchr(brace, '{')) != NULL && recorded_event_count < MAX_RECORDED_EVENTS) {
            FireworkEvent evt;
            memset(&evt, 0, sizeof(evt));

            char *num_start = strstr(brace, "\"frame\":");
            if (num_start) sscanf(num_start, "\"frame\": %d", &evt.frame);
            num_start = strstr(brace, "\"x\":");
            if (num_start) sscanf(num_start, "\"x\": %f", &evt.x);
            num_start = strstr(brace, "\"targetX\":");
            if (num_start) sscanf(num_start, "\"targetX\": %f", &evt.targetX);
            num_start = strstr(brace, "\"targetY\":");
            if (num_start) sscanf(num_start, "\"targetY\": %f", &evt.targetY);
            num_start = strstr(brace, "\"vx\":");
            if (num_start) sscanf(num_start, "\"vx\": %f", &evt.vx);
            num_start = strstr(brace, "\"vy\":");
            if (num_start) sscanf(num_start, "\"vy\": %f", &evt.vy);
            num_start = strstr(brace, "\"r\":");
            if (num_start) sscanf(num_start, "\"r\": %d", &evt.r);
            num_start = strstr(brace, "\"g\":");
            if (num_start) sscanf(num_start, "\"g\": %d", &evt.g);
            num_start = strstr(brace, "\"b\":");
            if (num_start) sscanf(num_start, "\"b\": %d", &evt.b);
            num_start = strstr(brace, "\"palette_idx\":");
            if (num_start) sscanf(num_start, "\"palette_idx\": %d", &evt.palette_index);

            recorded_events[recorded_event_count++] = evt;
            brace++;
        }
    }

    frame_counter = 0;
    play_event_index = 0;
    current_mode = MODE_PLAYING;

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].active = 0;
    }

    printf("Playback started: %d events, scale=%.2fx%.2f, recorded_size=%dx%d, current_size=%dx%d\n",
           recorded_event_count, play_scale_x, play_scale_y, w, h, screen_width, screen_height);

    return 1;
}

EMSCRIPTEN_KEEPALIVE
int get_mode() {
    return current_mode;
}

EMSCRIPTEN_KEEPALIVE
int get_recorded_event_count() {
    return recorded_event_count;
}

EMSCRIPTEN_KEEPALIVE
int get_record_width() {
    return record_width;
}

EMSCRIPTEN_KEEPALIVE
int get_record_height() {
    return record_height;
}

EMSCRIPTEN_KEEPALIVE
int get_frame() {
    return frame_counter;
}

EMSCRIPTEN_KEEPALIVE
void reset_performance() {
    frame_counter = 0;
    current_mode = MODE_IDLE;
    play_event_index = 0;
    recorded_event_count = 0;
    palette_size = 0;

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].active = 0;
    }
}

int main() {
    srand(time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    double w, h;
    emscripten_get_element_css_size("#canvas", &w, &h);
    screen_width = (int)w;
    screen_height = (int)h;

    SDL_CreateWindowAndRenderer(screen_width, screen_height, 0, &window, &renderer);

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].active = 0;
    }

    emscripten_set_main_loop(main_loop, 0, 1);

    return 0;
}
