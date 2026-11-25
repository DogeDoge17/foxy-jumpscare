#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "viewporter-client-protocol.h"                  // generated
#include "wlr-layer-shell-unstable-v1-client-protocol.h" // generated
#include <wayland-client.h>

#include <miniaudio/miniaudio.h>
#include <png.h>

#ifndef NDEBUG
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wp_viewporter *viewporter;

static struct wl_surface *surface;
static struct wp_viewport *viewport;
static struct zwlr_layer_surface_v1 *layer_surface;
static struct wl_buffer *buffer;
char *pixel_buffer = NULL;
int fd = -1;

static int width = 0, height = 0;
static uint32_t img_width = 0, img_height = 0;
const uint32_t sprite_width = 1024, sprite_height = 768;
static uint frame = 0;
const uint max_frames = 14;
const uint last_linger = 1;
static bool running = false;

static int create_memfd(const char *name, size_t size) {
#ifdef MFD_CLOEXEC
    int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0)
        return -1;
#else
    char shm_name[128];
    snprintf(shm_name, sizeof(shm_name), "/%s-%ld", name, (long)time(NULL));
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;
    shm_unlink(shm_name);
#endif
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void destroy_buffer(void) {
    if (buffer) {
        wl_buffer_destroy(buffer);
        buffer = NULL;
    }
}

static void draw_frame(void) {
    if (!running || width <= 0 || height <= 0)
        return;

    const int stride = img_width * 4;
    const size_t size = (size_t)stride * img_height;

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)size);
    destroy_buffer();
    buffer = wl_shm_pool_create_buffer(pool, 0, img_width, img_height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    wl_surface_attach(surface, buffer, 0, 0);
    wp_viewport_set_destination(viewport, width, height);
    wp_viewport_set_source(viewport, wl_fixed_from_int(0),
                           wl_fixed_from_int(1 * (frame >= max_frames - 1 ? max_frames - 1 : frame) * sprite_height),
                           wl_fixed_from_int(sprite_width), wl_fixed_from_int(sprite_height));
    wl_surface_damage_buffer(surface, 0, 0, sprite_width, sprite_height);
    wl_surface_commit(surface);
}

static void shm_format(void *data, struct wl_shm *wl_shm, uint32_t format) {
    (void)data;
    (void)wl_shm;
    (void)format;
    // ARGB8888 is a required shared-memory format.
}
static const struct wl_shm_listener shm_listener = {
    .format = shm_format,
};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h) {
    (void)data;
    // Acknowledge, record size, render.
    zwlr_layer_surface_v1_ack_configure(ls, serial);

    // Some compositors may send 0 for w/h first—keep old size until non-zero
    if (w > 0)
        width = (int)w;
    if (h > 0)
        height = (int)h;

    draw_frame();
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    (void)data;
    (void)ls;
    running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
    (void)data;
    LOG("global: %s v%u\n", iface, version);

    if (strcmp(iface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(iface, "wl_shm") == 0) {
        shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
        wl_shm_add_listener(shm, &shm_listener, NULL);
    } else if (strcmp(iface, "zwlr_layer_shell_v1") == 0) {
        layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(iface, "wp_viewporter") == 0) {
        viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int init_wayland() {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) == -1)
        return 1;

    if (!compositor || !shm || !layer_shell || !viewporter) {
        fprintf(stderr, "Missing required globals (%s%s%s%s).\n", !compositor ? "compositor/" : "", !shm ? "shm/" : "",
                !layer_shell ? "layer_shell/" : "", !viewporter ? "viewporter" : "");
        return 1;
    }

    surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        fprintf(stderr, "Failed to create wl_surface.\n");
        return 1;
    }
    viewport = wp_viewporter_get_viewport(viewporter, surface);

    struct wl_region *input_region = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(surface, input_region);
    wl_region_destroy(input_region);

    layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, /* wl_output */ NULL,
                                                          ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "foxy");
    zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, NULL);

    zwlr_layer_surface_v1_set_anchor(layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_surface, 0, 0);

    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, -1);

    wl_surface_commit(surface);
    return 0;
}

void deinit_wayland() {
    destroy_buffer();
    if (layer_surface) {
        zwlr_layer_surface_v1_destroy(layer_surface);
        layer_surface = NULL;
    }
    if (viewport) {
        wp_viewport_destroy(viewport);
        viewport = NULL;
    }
    if (surface) {
        wl_surface_destroy(surface);
        surface = NULL;
    }
    if (viewporter) {
        wp_viewporter_destroy(viewporter);
        viewporter = NULL;
    }
    if (layer_shell) {
        zwlr_layer_shell_v1_destroy(layer_shell);
        layer_shell = NULL;
    }
    if (shm) {
        wl_shm_destroy(shm);
        shm = NULL;
    }
    if (compositor) {
        wl_compositor_destroy(compositor);
        compositor = NULL;
    }
    if (registry) {
        wl_registry_destroy(registry);
        registry = NULL;
    }
    if (display) {
        wl_display_disconnect(display);
        display = NULL;
    }
}

int load_image(char **buffer, uint32_t *width, uint32_t *height) {
    FILE *fp = fopen(FOXY_DATA_DIR "/spritesheet.png", "rb");
    if (!fp) {
        fprintf(stderr, "PNG error: %s\n", strerror(errno));
        return -1;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return -1;
    }

    // These cleanup values must survive libpng's longjmp on a read error.
    uint8_t *volatile data = MAP_FAILED;
    volatile size_t size = 0;
    volatile int image_fd = -1;
    if (setjmp(png_jmpbuf(png)))
        goto error;

    png_init_io(png, fp);
    png_read_info(png, info);
    uint32_t w = png_get_image_width(png, info);
    uint32_t h = png_get_image_height(png, info);
    if (w > INT_MAX / 4 || h > INT_MAX / (w * 4)) {
        fprintf(stderr, "PNG is too large for a Wayland SHM buffer.\n");
        goto error;
    }

    int color_type = png_get_color_type(png, info);
    png_set_expand(png);
    if (png_get_bit_depth(png, info) == 16)
        png_set_strip_16(png);
    if (!(color_type & PNG_COLOR_MASK_COLOR))
        png_set_gray_to_rgb(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
    png_set_bgr(png);
    // Wayland premultiplies encoded color values, so suppress gamma conversion.
    png_set_alpha_mode(png, PNG_ALPHA_PREMULTIPLIED, 1.0);
    png_set_gamma(png, 1.0, 1.0);
    int passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);

    size = (size_t)w * h * 4;
    image_fd = create_memfd("spritesheet", size);
    if (image_fd < 0) {
        fprintf(stderr, "memfd/shm allocation failed: %s\n", strerror(errno));
        goto error;
    }

    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, image_fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        goto error;
    }

    for (int pass = 0; pass < passes; pass++)
        for (uint32_t y = 0; y < h; y++)
            png_read_row(png, data + (size_t)y * w * 4, NULL);
    png_read_end(png, NULL);

    if (fd >= 0)
        close(fd);
    fd = image_fd;
    *width = w;
    *height = h;
    *buffer = (char *)data;
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return 0;

error:
    if (data != MAP_FAILED)
        munmap(data, size);
    if (image_fd >= 0)
        close(image_fd);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return -1;
}

int start_jumpscare() {
    int redraw_timer = -1;
    running = true;
    int res = init_wayland();
    if (res != 0) {
        goto cleanup;
    }

    res = load_image(&pixel_buffer, &img_width, &img_height);
    if (res != 0) {
        goto cleanup;
    }

    ma_engine engine;
    ma_sound sound;

    ma_engine_init(NULL, &engine);

    // Load sound
    ma_sound_init_from_file(&engine, FOXY_DATA_DIR "/jumpscare.mp3", 0, NULL, NULL, &sound);
    ma_sound_start(&sound);

    redraw_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    const struct itimerspec redraw_interval = {
        .it_interval = {.tv_nsec = 43333332},
        .it_value = {.tv_nsec = 43333332},
    };
    if (redraw_timer < 0 || timerfd_settime(redraw_timer, 0, &redraw_interval, NULL) < 0) {
        perror("Failed to start redraw timer");
        res = 1;
        goto cleanup;
    }
    while (running) {
        if (wl_display_dispatch_pending(display) == -1) {
            res = 1;
            break;
        }
        if (!running)
            break;
        int flushed = wl_display_flush(display);
        if (flushed < 0 && errno != EAGAIN) {
            res = 1;
            break;
        }

        struct pollfd events[] = {
            {.fd = wl_display_get_fd(display), .events = POLLIN | (flushed < 0 ? POLLOUT : 0)},
            {.fd = redraw_timer, .events = POLLIN},
        };
        if (poll(events, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            res = 1;
            break;
        }
        if (events[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            res = 1;
            break;
        }
        if ((events[0].revents & POLLIN) && wl_display_dispatch(display) == -1) {
            res = 1;
            break;
        }
        if (!running)
            break;
        if (events[1].revents & POLLIN) {
            uint64_t expirations;
            if (read(redraw_timer, &expirations, sizeof(expirations)) < 0) {
                if (errno == EINTR)
                    continue;
                res = 1;
                break;
            }
            draw_frame();
            frame++;
            if (frame > max_frames + last_linger)
                break;
        }
    }

cleanup:
    if (redraw_timer >= 0)
        close(redraw_timer);
    deinit_wayland();
    if (pixel_buffer)
        munmap(pixel_buffer, (size_t)img_height * img_width * 4);
    if (fd >= 0)
        close(fd);

    while (ma_sound_is_playing(&sound))
        ;
    ma_sound_uninit(&sound);
    ma_engine_uninit(&engine);
    fd = -1;
    pixel_buffer = NULL;
    img_width = img_height = 0;
    width = height = 0;
    frame = 0;
    running = false;

    return res;
}

// 1024 by 768

int main(int argc, char **argv) {
    ulong chance = 10000;

    long strtol(const char *nptr, char **endptr, int base);
    if (argc > 1) {
        long parsed = atoi(argv[1]);

        if (parsed > 0) {
            chance = parsed;
        }
    }

    srand(time(NULL));
    while (true) {
        double u = (rand() + 1.0) / (RAND_MAX + 1.0);
        size_t seconds = (size_t)(log(u) / log1p(-1.0 / chance));

        printf("jumpscaring in %zu seconds\n", seconds);
        sleep(seconds);
        if (start_jumpscare() != 0)
            return EXIT_FAILURE;
    }
}
