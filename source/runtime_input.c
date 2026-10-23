/* runtime input: preserved standalone implementation. */
#define _GNU_SOURCE 1
#ifdef NX_CONTROLLER_HOST_TEST
#include "input_contract.h"
#else
#include "runtime.h"
#endif

#include <math.h>

static bool granny_menu, granny_move, granny_look;
static float granny_cursor_x = 640.0f, granny_cursor_y = 360.0f;
static float granny_look_x, granny_look_y;
static uint32_t granny_cursor;

bool grnyx_controller_cursor(float *x, float *y) {
    uint32_t p = __atomic_load_n(&granny_cursor, __ATOMIC_RELAXED);
    *x = (float)(p & 0xffffu); *y = (float)((p >> 16) & 0x7fffu);
    return (p & 0x80000000u) != 0;
}

static float granny_axis(float v) {
    if (!isfinite(v) || fabsf(v) <= 0.18f) return 0;
    v = fmaxf(-1.0f, fminf(1.0f, v));
    return copysignf((fabsf(v) - 0.18f) / 0.82f, v);
}

static void granny_finger(GrnyxInputSnapshot *s, uint32_t id, float x, float y) {
    if (s->touch_count >= GRNYX_INPUT_MAX_TOUCHES) return;
    s->touches[s->touch_count++] = (GrnyxTouchPoint){.id = id, .x = x, .y = y,
                                                 .attributes = GRNYX_TOUCH_CONTROLLER};
}

void grnyx_controller_touches(GrnyxInputSnapshot *s, float dt) {
    if (!isfinite(dt) || dt < 0) dt = 0;
    if (dt > 0.05f) dt = 0.05f;
    if (s->buttons_down & GRNYX_BTN_MINUS) {
        granny_menu = !granny_menu;
#ifndef NX_CONTROLLER_HOST_TEST
        GRNYX_LOGCI("INPUT", "controller cursor %s", granny_menu ? "enabled" : "disabled");
#endif
        granny_move = granny_look = false;
        const uint32_t pointer = granny_menu && s->controller_connected
            ? 0x80000000u | (uint32_t)granny_cursor_x | ((uint32_t)granny_cursor_y << 16) : 0;
        __atomic_store_n(&granny_cursor, pointer, __ATOMIC_RELAXED);
        return; /* Release every synthetic finger before changing modes. */
    }
    if (!s->controller_connected || s->touch_count) {
        granny_move = granny_look = false;
        __atomic_store_n(&granny_cursor, 0, __ATOMIC_RELAXED);
        return;
    }
    float lx = granny_axis(s->left_x), ly = granny_axis(s->left_y);
    float rx = granny_axis(s->right_x), ry = granny_axis(s->right_y);
    if (granny_menu) {
        granny_cursor_x = fmaxf(8, fminf(1272, granny_cursor_x + (lx + rx) * dt * 750));
        granny_cursor_y = fmaxf(8, fminf(712, granny_cursor_y - (ly + ry) * dt * 750));
        uint32_t p = 0x80000000u | (uint32_t)granny_cursor_x | ((uint32_t)granny_cursor_y << 16);
        __atomic_store_n(&granny_cursor, p, __ATOMIC_RELAXED);
        if (s->buttons & (GRNYX_BTN_A | GRNYX_BTN_ZR)) granny_finger(s, 16, granny_cursor_x, granny_cursor_y);
        return;
    }
    __atomic_store_n(&granny_cursor, 0, __ATOMIC_RELAXED);
    bool moving = lx != 0 || ly != 0;
    if (moving) {
        /* APK level2 JoystickRing: (153.7002,126), 105x105, scale 1.21.
         * CanvasScaler: reference 800x600, match .5 -> scale sqrt(1.6*1.2).
         * NewJoystickScript normalizes drag by the full width, not radius. */
        float length = fmaxf(1, sqrtf(lx * lx + ly * ly));
        /* Android pointer IDs must fit in its 32-bit pointer-ID set. */
        granny_finger(s, 17, 212.9732f + (granny_move ? lx / length * 177 : 0),
                     545.4093f - (granny_move ? ly / length * 177 : 0));
    }
    granny_move = moving;
    bool looking = rx != 0 || ry != 0;
    if (looking) {
        if (!granny_look) { granny_look_x = 800; granny_look_y = 360; }
        else { granny_look_x += rx * dt * 650; granny_look_y -= ry * dt * 650; }
        if (granny_look_x < 510 || granny_look_x > 1080 || granny_look_y < 180 || granny_look_y > 540)
            looking = false; /* Lift for one poll, then start a fresh swipe. */
        else granny_finger(s, 18, granny_look_x, granny_look_y);
    }
    granny_look = looking;
    /* APK level2 PickUpButton/OpenDoorButton, PlayerHukarSigButton, OptionButton
     * centers after the same CanvasScaler transform as the joystick. */
    if (s->buttons & (GRNYX_BTN_A | GRNYX_BTN_ZR)) granny_finger(s, 19, 1138.3875f, 240.0035f);
    if (s->buttons & (GRNYX_BTN_B | GRNYX_BTN_ZL)) granny_finger(s, 20, 121.9364f, 154.7315f);
    if (s->buttons & GRNYX_BTN_X) granny_finger(s, 21, 640, 655);
    if (s->buttons & GRNYX_BTN_Y) granny_finger(s, 22, 310.1630f, 70.6677f);
}

#ifndef NX_CONTROLLER_HOST_TEST


/* ---- input ---- */
#include <math.h>
#include <string.h>
#include <switch.h>



static PadState input_g_pad;
static bool input_g_initialized;
static bool input_g_sensor_started;
static HidSixAxisSensorHandle input_g_sensor_handles[GRNYX_INPUT_MAX_SENSORS];
static int input_g_sensor_count;
static HidNpadIdType input_g_sensor_id;
static HidNpadStyleTag input_g_sensor_style;
static GrnyxInputSessionStats input_g_stats;


static uint64_t input_map_buttons(u64 raw) {
    uint64_t out = 0;
    if (raw & HidNpadButton_A) out |= GRNYX_BTN_A;
    if (raw & HidNpadButton_B) out |= GRNYX_BTN_B;
    if (raw & HidNpadButton_X) out |= GRNYX_BTN_X;
    if (raw & HidNpadButton_Y) out |= GRNYX_BTN_Y;
    if (raw & HidNpadButton_StickL) out |= GRNYX_BTN_LSTICK;
    if (raw & HidNpadButton_StickR) out |= GRNYX_BTN_RSTICK;
    if (raw & HidNpadButton_L) out |= GRNYX_BTN_L;
    if (raw & HidNpadButton_R) out |= GRNYX_BTN_R;
    if (raw & HidNpadButton_ZL) out |= GRNYX_BTN_ZL;
    if (raw & HidNpadButton_ZR) out |= GRNYX_BTN_ZR;
    if (raw & HidNpadButton_Plus) out |= GRNYX_BTN_PLUS;
    if (raw & HidNpadButton_Minus) out |= GRNYX_BTN_MINUS;
    if (raw & HidNpadButton_Left) out |= GRNYX_BTN_DLEFT;
    if (raw & HidNpadButton_Up) out |= GRNYX_BTN_DUP;
    if (raw & HidNpadButton_Right) out |= GRNYX_BTN_DRIGHT;
    if (raw & HidNpadButton_Down) out |= GRNYX_BTN_DDOWN;
    if (raw & HidNpadButton_AnySL) out |= GRNYX_BTN_SL;
    if (raw & HidNpadButton_AnySR) out |= GRNYX_BTN_SR;
    return out;
}

static float input_normalize_stick(s32 value) {
    float out = (float)value / (float)JOYSTICK_MAX;
    if (out > 1.0f) out = 1.0f;
    if (out < -1.0f) out = -1.0f;
    return out;
}

static HidNpadStyleTag input_choose_sensor_style(u32 styles) {
    if (styles & HidNpadStyleTag_NpadHandheld) return HidNpadStyleTag_NpadHandheld;
    if (styles & HidNpadStyleTag_NpadFullKey)  return HidNpadStyleTag_NpadFullKey;
    if (styles & HidNpadStyleTag_NpadJoyDual)  return HidNpadStyleTag_NpadJoyDual;
    if (styles & HidNpadStyleTag_NpadJoyLeft)  return HidNpadStyleTag_NpadJoyLeft;
    if (styles & HidNpadStyleTag_NpadJoyRight) return HidNpadStyleTag_NpadJoyRight;
    return (HidNpadStyleTag)0;
}

static int input_sensor_handle_count_for_style(HidNpadStyleTag style) {
    return style == HidNpadStyleTag_NpadJoyDual ? 2 : 1;
}

static void input_stop_sensors(void) {
    if (!input_g_sensor_started) return;
    for (int i = 0; i < input_g_sensor_count; ++i)
        (void)hidStopSixAxisSensor(input_g_sensor_handles[i]);
    input_g_sensor_started = false;
    input_g_sensor_count = 0;
}

static void input_refresh_sensors(bool handheld, u32 styles) {
    HidNpadIdType id = handheld ? HidNpadIdType_Handheld : HidNpadIdType_No1;
    HidNpadStyleTag style = input_choose_sensor_style(styles);
    if (!style) {
        input_stop_sensors();
        return;
    }

    if (input_g_sensor_started && id == input_g_sensor_id && style == input_g_sensor_style)
        return;

    input_stop_sensors();
    const int count = input_sensor_handle_count_for_style(style);
    Result rc = hidGetSixAxisSensorHandles(input_g_sensor_handles, count, id, style);
    if (R_FAILED(rc)) {
        GRNYX_LOGCW("INPUT", "sixaxis handles unavailable id=%d style=0x%x rc=0x%08x",
                    (int)id, (unsigned)style, rc);
        return;
    }

    int started = 0;
    for (int i = 0; i < count; ++i) {
        rc = hidStartSixAxisSensor(input_g_sensor_handles[i]);
        if (R_FAILED(rc)) {
            GRNYX_LOGCW("INPUT", "sixaxis start failed index=%d rc=0x%08x", i, rc);
            break;
        }
        started++;
    }
    if (started != count) {
        for (int i = 0; i < started; ++i)
            (void)hidStopSixAxisSensor(input_g_sensor_handles[i]);
        return;
    }

    input_g_sensor_id = id;
    input_g_sensor_style = style;
    input_g_sensor_count = count;
    input_g_sensor_started = true;
    GRNYX_LOGCI("INPUT", "sixaxis started id=%d style=0x%x handles=%d",
                (int)id, (unsigned)style, count);
}

bool grnyx_input_init(void) {
    if (input_g_initialized) return true;
    memset(&input_g_stats, 0, sizeof(input_g_stats));
    granny_menu = false;
    granny_move = granny_look = false;
    granny_cursor_x = 640; granny_cursor_y = 360;
    __atomic_store_n(&granny_cursor, 0, __ATOMIC_RELAXED);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&input_g_pad);
    hidInitializeTouchScreen();

    input_g_sensor_started = false;
    input_g_sensor_count = 0;
    input_g_initialized = true;
    GRNYX_LOGCI("INPUT", "input backend initialized: pad + touchscreen + sixaxis probe");
    return true;
}

void grnyx_input_update(GrnyxInputSnapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!input_g_initialized) return;

    padUpdate(&input_g_pad);
    out->controller_connected = padIsConnected(&input_g_pad);
    out->handheld = padIsHandheld(&input_g_pad);
    out->style_set = padGetStyleSet(&input_g_pad);
    out->buttons = input_map_buttons(padGetButtons(&input_g_pad));
    out->buttons_down = input_map_buttons(padGetButtonsDown(&input_g_pad));
    out->buttons_up = input_map_buttons(padGetButtonsUp(&input_g_pad));

    HidAnalogStickState left = padGetStickPos(&input_g_pad, 0);
    HidAnalogStickState right = padGetStickPos(&input_g_pad, 1);
    out->left_x = input_normalize_stick(left.x);
    out->left_y = input_normalize_stick(left.y);
    out->right_x = input_normalize_stick(right.x);
    out->right_y = input_normalize_stick(right.y);

    HidTouchScreenState touch_state;
    memset(&touch_state, 0, sizeof(touch_state));
    if (hidGetTouchScreenStates(&touch_state, 1) > 0) {
        int count = touch_state.count;
        if (count < 0) count = 0;
        if (count > GRNYX_INPUT_MAX_TOUCHES) count = GRNYX_INPUT_MAX_TOUCHES;
        out->touch_count = count;
        for (int i = 0; i < count; ++i) {
            const HidTouchState *src = &touch_state.touches[i];
            GrnyxTouchPoint *dst = &out->touches[i];
            dst->id = src->finger_id;
            dst->x = (float)src->x;
            dst->y = (float)src->y;
            dst->diameter_x = (float)src->diameter_x;
            dst->diameter_y = (float)src->diameter_y;
            dst->attributes = src->attributes;
        }
    }

    static uint64_t last_tick;
    uint64_t tick = armGetSystemTick(), frequency = armGetSystemTickFreq();
    float dt = last_tick && frequency ? (float)(tick - last_tick) / (float)frequency : 0;
    last_tick = tick;
    grnyx_controller_touches(out, dt);

    if (out->controller_connected)
        input_refresh_sensors(out->handheld, out->style_set);
    else
        input_stop_sensors();

    if (input_g_sensor_started) {
        for (int i = 0; i < input_g_sensor_count && i < GRNYX_INPUT_MAX_SENSORS; ++i) {
            HidSixAxisSensorState state;
            memset(&state, 0, sizeof(state));
            if (hidGetSixAxisSensorStates(input_g_sensor_handles[i], &state, 1) > 0) {
                out->sensors[out->sensor_count].available = true;
                out->sensors[out->sensor_count].accel_x = state.acceleration.x;
                out->sensors[out->sensor_count].accel_y = state.acceleration.y;
                out->sensors[out->sensor_count].accel_z = state.acceleration.z;
                out->sensors[out->sensor_count].gyro_x = state.angular_velocity.x;
                out->sensors[out->sensor_count].gyro_y = state.angular_velocity.y;
                out->sensors[out->sensor_count].gyro_z = state.angular_velocity.z;
                out->sensor_count++;
            }
        }
    }

    input_g_stats.frames++;
    if (out->controller_connected) input_g_stats.saw_controller = true;
    if (out->buttons_down) input_g_stats.saw_button = true;
    if (fabsf(out->left_x) > 0.20f || fabsf(out->left_y) > 0.20f) input_g_stats.saw_left_stick = true;
    if (fabsf(out->right_x) > 0.20f || fabsf(out->right_y) > 0.20f) input_g_stats.saw_right_stick = true;
    if (out->touch_count > 0) input_g_stats.saw_touch = true;
    if (out->touch_count > 1) input_g_stats.saw_multitouch = true;
    if (out->sensor_count > 0) {
        if (!input_g_stats.saw_sensor)
            GRNYX_LOGCI("INPUT", "sixaxis first sample accel=(%.3f,%.3f,%.3f) gyro=(%.3f,%.3f,%.3f)",
                        (double)out->sensors[0].accel_x, (double)out->sensors[0].accel_y,
                        (double)out->sensors[0].accel_z, (double)out->sensors[0].gyro_x,
                        (double)out->sensors[0].gyro_y, (double)out->sensors[0].gyro_z);
        input_g_stats.saw_sensor = true;
    }
    if ((uint32_t)out->touch_count > input_g_stats.max_touches) input_g_stats.max_touches = (uint32_t)out->touch_count;

    if (out->buttons_down)
        GRNYX_LOGCI("INPUT", "buttons down=0x%016llx held=0x%016llx",
                    (unsigned long long)out->buttons_down, (unsigned long long)out->buttons);
    if (out->buttons_up)
        GRNYX_LOGCI("INPUT", "buttons up=0x%016llx held=0x%016llx",
                    (unsigned long long)out->buttons_up, (unsigned long long)out->buttons);
}

void grnyx_input_get_stats(GrnyxInputSessionStats *out) {
    if (out) *out = input_g_stats;
}

void grnyx_input_shutdown(void) {
    if (!input_g_initialized) return;
    input_stop_sensors();
    input_g_initialized = false;
    GRNYX_LOGCI("INPUT", "input backend shutdown");
}



/* ---- sensors ---- */
#pragma push_macro("GRNYX_ANDROID_STANDARD_GRAVITY")
#pragma push_macro("GRNYX_SENSOR_QUEUE_MAX")





#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct GrnyxASensorManager { int tag; };
struct GrnyxASensor {
    int type;
    int handle;
    int min_delay_us;
    const char *name;
    const char *vendor;
    float resolution;
};
struct GrnyxASensorEventQueue {
    GrnyxALooper *looper;
    bool accel_enabled;
    bool gyro_enabled;
    int32_t period_us;
    uint64_t last_sequence;
    unsigned consumed;
    int fds[2];
    bool notified;
};

typedef struct sensors_SensorState {
    pthread_mutex_t lock;
    bool initialized;
    uint64_t sequence;
    int64_t timestamp_ns;
    bool have_accel;
    bool have_gyro;
    float accel[3];
    float gyro[3];
} sensors_SensorState;

static struct GrnyxASensorManager sensors_g_manager = { 0x6400 };
static const struct GrnyxASensor sensors_g_accel = {
    GRNYX_ASENSOR_TYPE_ACCELEROMETER, 1, 10000,
    "GRNYX Accelerometer", "GRNYX", 0.0f
};
static const struct GrnyxASensor sensors_g_gyro = {
    GRNYX_ASENSOR_TYPE_GYROSCOPE, 4, 10000,
    "GRNYX Gyroscope", "GRNYX", 0.0f
};
static const GrnyxASensor *const sensors_g_sensor_list[] = { &sensors_g_accel, &sensors_g_gyro };
static sensors_SensorState sensors_g_state = { .lock = PTHREAD_MUTEX_INITIALIZER };
#define GRNYX_SENSOR_QUEUE_MAX 8
#define GRNYX_ANDROID_STANDARD_GRAVITY 9.80665f
static GrnyxASensorEventQueue *sensors_g_queues[GRNYX_SENSOR_QUEUE_MAX];
static uint64_t sensors_g_accel_events_delivered;

/* Readiness lives on a real GRNYX descriptor so callback and identifier queues
 * use the same ALooper path as other Android descriptor sources. */
static unsigned sensors_sensor_queue_ready(const GrnyxASensorEventQueue *q) {
    unsigned ready = (q->accel_enabled && sensors_g_state.have_accel ? 1u : 0u) |
                     (q->gyro_enabled && sensors_g_state.have_gyro ? 2u : 0u);
    return ready & ~(q->last_sequence == sensors_g_state.sequence ? q->consumed : 0u);
}

/* Caller holds g_state.lock. Coalesce notification bytes along with samples. */
static void sensors_sensor_queue_notify(GrnyxASensorEventQueue *q) {
    unsigned char token = 1;
    const bool ready = sensors_sensor_queue_ready(q) != 0;
    if (ready && !q->notified)
        q->notified = grnyx_fd_write(q->fds[1], &token, 1) == 1;
    else if (!ready && q->notified) {
        (void)grnyx_fd_read(q->fds[0], &token, 1);
        q->notified = false;
    }
}

static int64_t sensors_monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * INT64_C(1000000000) + ts.tv_nsec;
}

GrnyxASensorManager *ASensorManager_getInstance(void) { return &sensors_g_manager; }
GrnyxASensorManager *ASensorManager_getInstanceForPackage(const char *package_name) {
    (void)package_name;
    return &sensors_g_manager;
}

int ASensorManager_getSensorList(GrnyxASensorManager *manager,
                                 const GrnyxASensor *const **out_list) {
    if (!manager || !out_list) return -EINVAL;
    *out_list = sensors_g_sensor_list;
    return (int)(sizeof(sensors_g_sensor_list) / sizeof(sensors_g_sensor_list[0]));
}

const GrnyxASensor *ASensorManager_getDefaultSensor(GrnyxASensorManager *manager, int type) {
    if (!manager) return NULL;
    if (type == GRNYX_ASENSOR_TYPE_ACCELEROMETER) return &sensors_g_accel;
    if (type == GRNYX_ASENSOR_TYPE_GYROSCOPE) return &sensors_g_gyro;
    return NULL;
}

GrnyxASensorEventQueue *ASensorManager_createEventQueue(GrnyxASensorManager *manager,
                                                         GrnyxALooper *looper,
                                                         int ident,
                                                         int (*callback)(int, int, void *),
                                                         void *data) {
    if (!manager || !looper) {
        errno = EINVAL;
        return NULL;
    }
    GrnyxASensorEventQueue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->looper = looper;
    q->period_us = 33333;
    if (grnyx_fd_pipe(q->fds) != 0) { free(q); return NULL; }
    pthread_mutex_lock(&sensors_g_state.lock);
    q->last_sequence = sensors_g_state.sequence;
    bool registered = false;
    for (size_t i = 0; i < GRNYX_SENSOR_QUEUE_MAX; ++i) {
        if (!sensors_g_queues[i]) { sensors_g_queues[i] = q; registered = true; break; }
    }
    if (registered && ALooper_addFd(looper, q->fds[0], ident, 1, callback, data) != 1) {
        for (size_t i = 0; i < GRNYX_SENSOR_QUEUE_MAX; ++i) if (sensors_g_queues[i] == q) sensors_g_queues[i] = NULL;
        registered = false;
        errno = EINVAL;
    } else if (!registered) errno = ENOSPC;
    pthread_mutex_unlock(&sensors_g_state.lock);
    if (!registered) {
        (void)grnyx_fd_close(q->fds[0]); (void)grnyx_fd_close(q->fds[1]);
        free(q); return NULL;
    }
    GRNYX_LOGCI("SENSOR", "event queue created ident=%d callback=%p looper=%p fd=%d", ident, (void *)callback, (void *)looper, q->fds[0]);
    return q;
}

int ASensorManager_destroyEventQueue(GrnyxASensorManager *manager, GrnyxASensorEventQueue *queue) {
    if (!manager || !queue) return -EINVAL;
    pthread_mutex_lock(&sensors_g_state.lock);
    for (size_t i = 0; i < GRNYX_SENSOR_QUEUE_MAX; ++i) if (sensors_g_queues[i] == queue) sensors_g_queues[i] = NULL;
    (void)ALooper_removeFd(queue->looper, queue->fds[0]);
    (void)grnyx_fd_close(queue->fds[0]); (void)grnyx_fd_close(queue->fds[1]);
    pthread_mutex_unlock(&sensors_g_state.lock);
    free(queue);
    return 0;
}

static bool sensors_sensor_matches(const GrnyxASensor *sensor, int type) {
    return sensor && sensor->type == type;
}

int ASensorEventQueue_enableSensor(GrnyxASensorEventQueue *queue, const GrnyxASensor *sensor) {
    if (!queue || !sensor) return -EINVAL;
    if (!sensors_sensor_matches(sensor, 1) && !sensors_sensor_matches(sensor, 4)) return -EINVAL;
    pthread_mutex_lock(&sensors_g_state.lock);
    if (sensors_sensor_matches(sensor, GRNYX_ASENSOR_TYPE_ACCELEROMETER)) queue->accel_enabled = true;
    else if (sensors_sensor_matches(sensor, GRNYX_ASENSOR_TYPE_GYROSCOPE)) queue->gyro_enabled = true;
    sensors_sensor_queue_notify(queue);
    pthread_mutex_unlock(&sensors_g_state.lock);
    GRNYX_LOGCI("SENSOR", "sensor enabled type=%d", sensor->type);
    return 0;
}

int ASensorEventQueue_disableSensor(GrnyxASensorEventQueue *queue, const GrnyxASensor *sensor) {
    if (!queue || !sensor) return -EINVAL;
    if (!sensors_sensor_matches(sensor, 1) && !sensors_sensor_matches(sensor, 4)) return -EINVAL;
    pthread_mutex_lock(&sensors_g_state.lock);
    if (sensors_sensor_matches(sensor, GRNYX_ASENSOR_TYPE_ACCELEROMETER)) queue->accel_enabled = false;
    else if (sensors_sensor_matches(sensor, GRNYX_ASENSOR_TYPE_GYROSCOPE)) queue->gyro_enabled = false;
    sensors_sensor_queue_notify(queue);
    pthread_mutex_unlock(&sensors_g_state.lock);
    return 0;
}

int ASensorEventQueue_setEventRate(GrnyxASensorEventQueue *queue, const GrnyxASensor *sensor,
                                   int32_t usec) {
    if (!queue || !sensor || usec < 0) return -EINVAL;
    pthread_mutex_lock(&sensors_g_state.lock);
    queue->period_us = usec < sensor->min_delay_us ? sensor->min_delay_us : usec;
    pthread_mutex_unlock(&sensors_g_state.lock);
    return 0;
}

static void sensors_sensor_write_event(GrnyxASensorEvent *ev, const GrnyxASensor *sensor, const float *values) {
    memset(ev, 0, sizeof(*ev));
    ev->version = (int32_t)sizeof(*ev);
    ev->sensor = sensor->handle;
    ev->type = sensor->type;
    ev->timestamp = sensors_g_state.timestamp_ns;
    memcpy(ev->data, values, 3 * sizeof(float));
}

size_t grnyx_sensors_read_latest(GrnyxASensorEvent *events, size_t capacity) {
    if (!events) return 0;
    size_t count = 0;
    pthread_mutex_lock(&sensors_g_state.lock);
    if (sensors_g_state.have_accel && count < capacity) sensors_sensor_write_event(&events[count++], &sensors_g_accel, sensors_g_state.accel);
    if (sensors_g_state.have_gyro && count < capacity) sensors_sensor_write_event(&events[count++], &sensors_g_gyro, sensors_g_state.gyro);
    pthread_mutex_unlock(&sensors_g_state.lock);
    return count;
}

int ASensorEventQueue_hasEvents(GrnyxASensorEventQueue *queue) {
    if (!queue) return -EINVAL;
    pthread_mutex_lock(&sensors_g_state.lock);
    const bool ready = sensors_sensor_queue_ready(queue) != 0;
    pthread_mutex_unlock(&sensors_g_state.lock);
    return ready ? 1 : 0;
}

ssize_t ASensorEventQueue_getEvents(GrnyxASensorEventQueue *queue,
                                    GrnyxASensorEvent *events, size_t count) {
    if (!queue || (!events && count)) return -EINVAL;
    if (count == 0) return 0;

    pthread_mutex_lock(&sensors_g_state.lock);
    if (queue->last_sequence != sensors_g_state.sequence) {
        queue->last_sequence = sensors_g_state.sequence;
        queue->consumed = 0;
    }
    const unsigned ready = sensors_sensor_queue_ready(queue);
    size_t written = 0;
    if ((ready & 1u) && written < count) {
        GrnyxASensorEvent *ev = &events[written++];
        memset(ev, 0, sizeof(*ev));
        ev->version = (int32_t)sizeof(*ev);
        ev->sensor = sensors_g_accel.handle;
        ev->type = sensors_g_accel.type;
        ev->timestamp = sensors_g_state.timestamp_ns;
        ev->data[0] = sensors_g_state.accel[0];
        ev->data[1] = sensors_g_state.accel[1];
        ev->data[2] = sensors_g_state.accel[2];
        queue->consumed |= 1u;
        sensors_g_accel_events_delivered++;
        if (sensors_g_accel_events_delivered <= 3u || (sensors_g_accel_events_delivered % 300u) == 0u) {
            GRNYX_LOGCI("SENSOR", "Android accel event=%llu m/s2=(%.3f,%.3f,%.3f)",
                        (unsigned long long)sensors_g_accel_events_delivered,
                        (double)ev->data[0], (double)ev->data[1], (double)ev->data[2]);
        }
    }
    if ((ready & 2u) && written < count) {
        GrnyxASensorEvent *ev = &events[written++];
        memset(ev, 0, sizeof(*ev));
        ev->version = (int32_t)sizeof(*ev);
        ev->sensor = sensors_g_gyro.handle;
        ev->type = sensors_g_gyro.type;
        ev->timestamp = sensors_g_state.timestamp_ns;
        ev->data[0] = sensors_g_state.gyro[0];
        ev->data[1] = sensors_g_state.gyro[1];
        ev->data[2] = sensors_g_state.gyro[2];
        queue->consumed |= 2u;
    }
    sensors_sensor_queue_notify(queue);
    pthread_mutex_unlock(&sensors_g_state.lock);
    return (ssize_t)written;
}

const char *ASensor_getName(const GrnyxASensor *sensor) {
    return sensor ? sensor->name : NULL;
}

const char *ASensor_getVendor(const GrnyxASensor *sensor) {
    return sensor ? sensor->vendor : NULL;
}

int ASensor_getType(const GrnyxASensor *sensor) {
    return sensor ? sensor->type : 0;
}

float ASensor_getResolution(const GrnyxASensor *sensor) {
    /* GRNYX currently exposes a host-backed virtual sensor rather than raw
     * sensor-HAL quantization. Zero advertises no stronger precision claim. */
    return sensor ? sensor->resolution : 0.0f;
}

int ASensor_getMinDelay(const GrnyxASensor *sensor) {
    return sensor ? sensor->min_delay_us : 0;
}

void grnyx_sensors_publish(const GrnyxInputSnapshot *snapshot) {
    if (!snapshot) return;
    pthread_mutex_lock(&sensors_g_state.lock);
    sensors_g_state.sequence++;
    sensors_g_state.timestamp_ns = sensors_monotonic_ns();
    sensors_g_state.have_accel = false;
    sensors_g_state.have_gyro = false;
    if (snapshot->sensor_count > 0 && snapshot->sensors[0].available) {
        const GrnyxMotionSample *s = &snapshot->sensors[0];
        sensors_g_state.have_accel = true;
        sensors_g_state.have_gyro = true;
        /* Sensors use the device's fixed natural frame. Display.getRotation()
         * already reports portrait quarter-turns from the Switch's natural
         * landscape orientation; rotating samples here makes apps apply that
         * turn twice and swaps side tilt with forward tilt. Keep chassis X/Y
         * fixed and convert libnx's gravity sign and g units at this boundary
         * (screen-up Switch Z=-1 g, Android Z=+9.80665 m/s^2). */
        sensors_g_state.accel[0] = -s->accel_x * GRNYX_ANDROID_STANDARD_GRAVITY;
        sensors_g_state.accel[1] = -s->accel_y * GRNYX_ANDROID_STANDARD_GRAVITY;
        sensors_g_state.accel[2] = -s->accel_z * GRNYX_ANDROID_STANDARD_GRAVITY;

        /* Angular velocity retains its chassis axes and rotation signs. */
        sensors_g_state.gyro[0] = s->gyro_x;
        sensors_g_state.gyro[1] = s->gyro_y;
        sensors_g_state.gyro[2] =  s->gyro_z;
    }
    for (size_t i = 0; i < GRNYX_SENSOR_QUEUE_MAX; ++i)
        if (sensors_g_queues[i]) sensors_sensor_queue_notify(sensors_g_queues[i]);
    pthread_mutex_unlock(&sensors_g_state.lock);
}

void grnyx_sensors_reset(void) {
    pthread_mutex_lock(&sensors_g_state.lock);
    sensors_g_state.sequence++;
    sensors_g_state.timestamp_ns = 0;
    sensors_g_state.have_accel = false;
    sensors_g_state.have_gyro = false;
    memset(sensors_g_state.accel, 0, sizeof(sensors_g_state.accel));
    memset(sensors_g_state.gyro, 0, sizeof(sensors_g_state.gyro));
    sensors_g_accel_events_delivered = 0;
    for (size_t i = 0; i < GRNYX_SENSOR_QUEUE_MAX; ++i) {
        if (sensors_g_queues[i]) {
            (void)ALooper_removeFd(sensors_g_queues[i]->looper, sensors_g_queues[i]->fds[0]);
            (void)grnyx_fd_close(sensors_g_queues[i]->fds[0]);
            (void)grnyx_fd_close(sensors_g_queues[i]->fds[1]);
        }
        free(sensors_g_queues[i]);
        sensors_g_queues[i] = NULL;
    }
    pthread_mutex_unlock(&sensors_g_state.lock);
}

#pragma pop_macro("GRNYX_SENSOR_QUEUE_MAX")
#pragma pop_macro("GRNYX_ANDROID_STANDARD_GRAVITY")


/* ---- cocos keyboard ---- */



#include <stdlib.h>
#include <string.h>


#ifdef __SWITCH__
#include <switch.h>
#endif

GrnyxResult grnyx_keyboard_show(const GrnyxKeyboardConfig *config,char *out_text,
                                 size_t out_text_size,bool *accepted) {
    if (!config || !accepted || !out_text || out_text_size < 4u) return GRNYX_ERR_ARGUMENT;
    *accepted = false;
    const char *initial_text = config->initial_text;
    if (!initial_text) initial_text = "";
    out_text[0] = '\0';

#ifdef __SWITCH__
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        GRNYX_LOGCW("KEYBOARD", "swkbdCreate failed rc=0x%08x", (unsigned)rc);
        return GRNYX_ERR_IO;
    }

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetType(&kbd,config->numeric ? SwkbdType_NumPad : SwkbdType_Normal);
    swkbdConfigSetPasswordFlag(&kbd,config->password ? 1 : 0);
    swkbdConfigSetDicFlag(&kbd,config->password ? 0 : 1);
    swkbdConfigSetReturnButtonFlag(&kbd,config->multiline ? 1 : 0);
    if(config->numeric && config->decimal)swkbdConfigSetLeftOptionalSymbolKey(&kbd,".");
    if(config->numeric && config->signed_number)swkbdConfigSetRightOptionalSymbolKey(&kbd,"-");
    if(config->action_label)swkbdConfigSetOkButtonText(&kbd,config->action_label);
    swkbdConfigSetInitialText(&kbd, initial_text);
    /* One UTF-16 unit needs at most three UTF-8 bytes. Never cut a character
     * to fit the output buffer. The modal libnx output holds 1000 units. */
    size_t max_units=(out_text_size-1u)/3u;
    if(max_units>1000u)max_units=1000u;
    swkbdConfigSetStringLenMax(&kbd,(u32)max_units);
    swkbdConfigSetHeaderText(&kbd, "Text input");

    rc = swkbdShow(&kbd, out_text, out_text_size);
    swkbdClose(&kbd);
    if (R_FAILED(rc)) {
        GRNYX_LOGCW("KEYBOARD", "swkbdShow closed/cancelled rc=0x%08x", (unsigned)rc);
        /* libnx uses BadExit for both cancel and abnormal exits. Retain rc,
         * return no accepted edit, and let the UI caller preserve its text. */
        out_text[0]='\0';
        return rc==MAKERESULT(Module_Libnx,LibnxError_LibAppletBadExit)?GRNYX_OK:GRNYX_ERR_IO;
    }
    GRNYX_LOGCI("KEYBOARD", "Switch software keyboard committed bytes=%zu", strlen(out_text));
    *accepted=true;
    return GRNYX_OK;
#else
    /* Host-only deterministic override used by the retained JNI/DEX regression.
     * Switch builds never inspect this environment variable. */
    if(getenv("GRNYX_TEST_KEYBOARD_FAIL"))return GRNYX_ERR_IO;
    if(getenv("GRNYX_TEST_KEYBOARD_CANCEL"))return GRNYX_OK;
    const char *host_text = getenv("GRNYX_TEST_KEYBOARD_TEXT");
    if (!host_text) host_text = initial_text;
    size_t n = strlen(host_text);
    if (n >= out_text_size) return GRNYX_ERR_ARGUMENT;
    memcpy(out_text, host_text, n);
    out_text[n] = '\0';
    *accepted=true;
    return GRNYX_OK;
#endif
}

GrnyxResult grnyx_cocos_keyboard_show(const char *initial_text,char *out_text,size_t capacity) {
    GrnyxKeyboardConfig config={.initial_text=initial_text,.multiline=true};bool accepted=false;
    GrnyxResult rc=grnyx_keyboard_show(&config,out_text,capacity,&accepted);
    /* Keep the established Cocos unchanged-edit cancellation behavior. */
    if(!accepted && rc==GRNYX_OK){
        if(!initial_text)initial_text="";
        size_t n=strlen(initial_text);if(n>=capacity)n=capacity-1u;
        memcpy(out_text,initial_text,n);out_text[n]='\0';return GRNYX_OK;
    }
    return rc;
}



/* ---- native activity ---- */
#pragma push_macro("CONFIG_GETTER")
#pragma push_macro("CONFIG_SETTER")
#pragma push_macro("GRNYX_AINPUT_EVENT_TYPE_KEY")
#pragma push_macro("GRNYX_AINPUT_EVENT_TYPE_MOTION")
#pragma push_macro("GRNYX_AKEY_DOWN")
#pragma push_macro("GRNYX_AKEY_UP")
#pragma push_macro("GRNYX_ALOOPER_EVENT_ERROR")
#pragma push_macro("GRNYX_ALOOPER_EVENT_HANGUP")
#pragma push_macro("GRNYX_ALOOPER_EVENT_INPUT")
#pragma push_macro("GRNYX_ALOOPER_EVENT_INVALID")
#pragma push_macro("GRNYX_ALOOPER_EVENT_OUTPUT")
#pragma push_macro("GRNYX_ALOOPER_POLL_CALLBACK")
#pragma push_macro("GRNYX_ALOOPER_POLL_ERROR")
#pragma push_macro("GRNYX_ALOOPER_POLL_TIMEOUT")
#pragma push_macro("GRNYX_ALOOPER_POLL_WAKE")
#pragma push_macro("GRNYX_ALOOPER_PREPARE_ALLOW_NON_CALLBACKS")
#pragma push_macro("GRNYX_AMOTION_DOWN")
#pragma push_macro("GRNYX_AMOTION_MOVE")
#pragma push_macro("GRNYX_AMOTION_UP")
#pragma push_macro("GRNYX_CHOREO_FRAME_NS")
#pragma push_macro("GRNYX_INPUT_MAX_POINTERS")
#pragma push_macro("GRNYX_INPUT_QUEUE_CAP")









#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern bool grnyx_diag_full_trace_enabled(void) __attribute__((weak));
static bool native_activity_choreographer_full_trace(void) {
    return grnyx_diag_full_trace_enabled && grnyx_diag_full_trace_enabled();
}

#define GRNYX_INPUT_QUEUE_CAP 32
#define GRNYX_INPUT_MAX_POINTERS 16

#define GRNYX_AINPUT_EVENT_TYPE_KEY 1
#define GRNYX_AINPUT_EVENT_TYPE_MOTION 2
#define GRNYX_AKEY_DOWN 0
#define GRNYX_AKEY_UP 1
#define GRNYX_AMOTION_DOWN 0
#define GRNYX_AMOTION_UP 1
#define GRNYX_AMOTION_MOVE 2
#define GRNYX_ALOOPER_PREPARE_ALLOW_NON_CALLBACKS 1
#define GRNYX_ALOOPER_EVENT_INPUT 1
#define GRNYX_ALOOPER_EVENT_OUTPUT 2
#define GRNYX_ALOOPER_EVENT_ERROR 4
#define GRNYX_ALOOPER_EVENT_HANGUP 8
#define GRNYX_ALOOPER_EVENT_INVALID 16
#define GRNYX_ALOOPER_POLL_WAKE -1
#define GRNYX_ALOOPER_POLL_CALLBACK -2
#define GRNYX_ALOOPER_POLL_TIMEOUT -3
#define GRNYX_ALOOPER_POLL_ERROR -4

struct GrnyxANativeWindow {
    int32_t refs;
    int32_t width;
    int32_t height;
    int32_t format;
};

struct GrnyxAChoreographer {
    pthread_t owner;
    GrnyxALooper *looper;
    bool initialized;
};

typedef struct native_activity_GrnyxChoreographerCallback {
    pthread_t owner;
    GrnyxALooper *looper;
    uint64_t due_ns;
    bool is64;
    union {
        void (*legacy)(long, void *);
        void (*cb64)(int64_t, void *);
    } fn;
    void *data;
    void *caller; /* Diagnostic posting boundary, never used for dispatch. */
    struct native_activity_GrnyxChoreographerCallback *next;
} native_activity_GrnyxChoreographerCallback;

#define GRNYX_CHOREO_FRAME_NS UINT64_C(16666667)

static pthread_mutex_t native_activity_g_choreo_lock = PTHREAD_MUTEX_INITIALIZER;
static native_activity_GrnyxChoreographerCallback *native_activity_g_choreo_head;
static native_activity_GrnyxChoreographerCallback *native_activity_g_choreo_tail;
static _Thread_local GrnyxAChoreographer native_activity_g_thread_choreographer;
static atomic_uint native_activity_g_choreo_get_reports, native_activity_g_choreo_post_reports, native_activity_g_choreo_dispatch_reports;
static uint64_t native_activity_g_choreo_last_queue_report_ns; /* Protected by g_choreo_lock. */

struct GrnyxAConfiguration {
    GrnyxAssetConfiguration values;
};

typedef struct native_activity_GrnyxLooperFd {
    int fd;
    int ident;
    int events;
    int (*callback)(int fd, int events, void *data);
    void *data;
} native_activity_GrnyxLooperFd;

struct GrnyxALooper {
    pthread_mutex_t lock;
    native_activity_GrnyxLooperFd *fds;
    size_t fd_count;
    size_t fd_capacity;
    int opts;
    pthread_t owner_thread;
    atomic_int references;
    int wake_read_fd;
    int wake_write_fd;
};

struct GrnyxAInputEvent {
    int32_t type;
    int32_t action;
    int32_t key_code;
    size_t pointer_count;
    int32_t ids[GRNYX_INPUT_MAX_POINTERS];
    float xs[GRNYX_INPUT_MAX_POINTERS];
    float ys[GRNYX_INPUT_MAX_POINTERS];
};

struct GrnyxAInputQueue {
    pthread_mutex_t lock;
    GrnyxAInputEvent events[GRNYX_INPUT_QUEUE_CAP];
    size_t head;
    size_t count;
    GrnyxAInputEvent current;
    bool current_valid;
    GrnyxALooper *looper;
    int ident;
    int (*callback)(int fd, int events, void *data);
    void *data;
};

typedef struct native_activity_GrnyxNativeRuntime {
    bool initialized;
    bool started;
    bool finish_requested;
    bool input_initialized;
    GrnyxANativeActivity activity;
    GrnyxANativeActivityCallbacks callbacks;
    GrnyxANativeWindow window;
    GrnyxAInputQueue input_queue;
    char internal_path[512];
    char external_path[512];
    char obb_path[512];
    GrnyxInputSnapshot previous_input;
    GrnyxDisplayPlan display_plan;
    bool previous_touch_active;
    int32_t previous_touch_id;
    float previous_touch_x;
    float previous_touch_y;
} native_activity_GrnyxNativeRuntime;

static native_activity_GrnyxNativeRuntime native_activity_g_native;
static __thread GrnyxALooper *native_activity_g_thread_looper;

static bool native_activity_copy_string(char *dst, size_t cap, const char *src) {
    if (!dst || !cap || !src) return false;
    const size_t n = strlen(src);
    if (n >= cap) return false;
    memcpy(dst, src, n + 1);
    return true;
}

static void native_activity_queue_wake(GrnyxAInputQueue *queue) {
    if (!queue || !queue->looper) return;
    ALooper_wake(queue->looper);
}

static bool native_activity_queue_event(GrnyxAInputQueue *queue, const GrnyxAInputEvent *event) {
    if (!queue || !event) return false;
    pthread_mutex_lock(&queue->lock);
    if (queue->count == GRNYX_INPUT_QUEUE_CAP) {
        queue->head = (queue->head + 1) % GRNYX_INPUT_QUEUE_CAP;
        queue->count--;
    }
    const size_t tail = (queue->head + queue->count) % GRNYX_INPUT_QUEUE_CAP;
    queue->events[tail] = *event;
    queue->count++;
    pthread_mutex_unlock(&queue->lock);
    native_activity_queue_wake(queue);
    return true;
}

static bool native_activity_queue_has_events(GrnyxAInputQueue *queue) {
    if (!queue) return false;
    pthread_mutex_lock(&queue->lock);
    const bool have = queue->current_valid || queue->count > 0;
    pthread_mutex_unlock(&queue->lock);
    return have;
}

static void native_activity_make_single_motion(GrnyxAInputEvent *event, int action, int32_t id, float x, float y) {
    memset(event, 0, sizeof(*event));
    event->type = GRNYX_AINPUT_EVENT_TYPE_MOTION;
    event->action = action;
    event->pointer_count = 1;
    event->ids[0] = id;
    event->xs[0] = x;
    event->ys[0] = y;
}

static void native_activity_make_key_event(GrnyxAInputEvent *event, int action, int32_t key_code) {
    memset(event, 0, sizeof(*event));
    event->type = GRNYX_AINPUT_EVENT_TYPE_KEY;
    event->action = action;
    event->key_code = key_code;
}

static void native_activity_queue_key_changes(GrnyxAInputQueue *queue, uint64_t changed, int action) {
    static const uint64_t bits[] = {
        GRNYX_BTN_DUP, GRNYX_BTN_DDOWN, GRNYX_BTN_DLEFT, GRNYX_BTN_DRIGHT,
        GRNYX_BTN_A, GRNYX_BTN_B, GRNYX_BTN_X, GRNYX_BTN_Y,
        GRNYX_BTN_L, GRNYX_BTN_R, GRNYX_BTN_ZL, GRNYX_BTN_ZR,
        GRNYX_BTN_LSTICK, GRNYX_BTN_RSTICK, GRNYX_BTN_MINUS,
    };
    for (size_t i = 0; i < sizeof(bits)/sizeof(bits[0]); ++i) {
        if (!(changed & bits[i])) continue;
        const int32_t code = grnyx_android_keycode_for_button(bits[i]);
        if (!code) continue;
        GrnyxAInputEvent ev;
        native_activity_make_key_event(&ev, action, code);
        (void)native_activity_queue_event(queue, &ev);
    }
}

bool grnyx_native_activity_init(const GrnyxNativeActivityConfig *config,
                                void *vm, void *env, void *activity_object,
                                GrnyxAssetManager *asset_manager) {
    if (!config || !config->internal_data_path || !config->external_data_path || !asset_manager)
        return false;
    grnyx_native_activity_reset();
    if (!native_activity_copy_string(native_activity_g_native.internal_path, sizeof(native_activity_g_native.internal_path), config->internal_data_path) ||
        !native_activity_copy_string(native_activity_g_native.external_path, sizeof(native_activity_g_native.external_path), config->external_data_path) ||
        !native_activity_copy_string(native_activity_g_native.obb_path, sizeof(native_activity_g_native.obb_path), config->obb_path ? config->obb_path : config->internal_data_path))
        return false;

    memset(&native_activity_g_native.callbacks, 0, sizeof(native_activity_g_native.callbacks));
    memset(&native_activity_g_native.activity, 0, sizeof(native_activity_g_native.activity));
    native_activity_g_native.window.refs = 1;
    native_activity_g_native.window.width = config->window_width > 0 ? config->window_width : 1280;
    native_activity_g_native.window.height = config->window_height > 0 ? config->window_height : 720;
    native_activity_g_native.window.format = config->window_format;
    if (config->display_plan.logical_width > 0 && config->display_plan.logical_height > 0) {
        native_activity_g_native.display_plan = config->display_plan;
    } else {
        (void)grnyx_display_make_plan(GRNYX_SCREEN_ORIENTATION_UNSPECIFIED, false,
                                      native_activity_g_native.window.width, native_activity_g_native.window.height,
                                      &native_activity_g_native.display_plan);
    }
    pthread_mutex_init(&native_activity_g_native.input_queue.lock, NULL);

    native_activity_g_native.activity.callbacks = &native_activity_g_native.callbacks;
    native_activity_g_native.activity.vm = vm;
    native_activity_g_native.activity.env = env;
    native_activity_g_native.activity.clazz = activity_object;
    native_activity_g_native.activity.internalDataPath = native_activity_g_native.internal_path;
    native_activity_g_native.activity.externalDataPath = native_activity_g_native.external_path;
    native_activity_g_native.activity.sdkVersion = config->sdk_version > 0 ? config->sdk_version : 30;
    native_activity_g_native.activity.assetManager = asset_manager;
    native_activity_g_native.activity.obbPath = native_activity_g_native.obb_path;
    native_activity_g_native.initialized = true;
    GRNYX_LOGCI("NATIVE", "NativeActivity prepared size=%dx%d sdk=%d data=%s",
                native_activity_g_native.window.width, native_activity_g_native.window.height,
                native_activity_g_native.activity.sdkVersion, native_activity_g_native.internal_path);
    return true;
}

void grnyx_native_activity_reset(void) {
    if (native_activity_g_native.input_initialized) {
        grnyx_input_shutdown();
    }
    grnyx_sensors_reset();
    if (native_activity_g_native.initialized) {
        pthread_mutex_destroy(&native_activity_g_native.input_queue.lock);
    }
    pthread_mutex_lock(&native_activity_g_choreo_lock);
    native_activity_GrnyxChoreographerCallback *cb = native_activity_g_choreo_head;
    native_activity_g_choreo_head = native_activity_g_choreo_tail = NULL;
    native_activity_g_choreo_last_queue_report_ns = 0;
    pthread_mutex_unlock(&native_activity_g_choreo_lock);
    while (cb) {
        native_activity_GrnyxChoreographerCallback *next = cb->next;
        free(cb);
        cb = next;
    }
    memset(&native_activity_g_thread_choreographer, 0, sizeof(native_activity_g_thread_choreographer));
    atomic_store(&native_activity_g_choreo_get_reports, 0);
    atomic_store(&native_activity_g_choreo_post_reports, 0);
    atomic_store(&native_activity_g_choreo_dispatch_reports, 0);
    memset(&native_activity_g_native, 0, sizeof(native_activity_g_native));
}

GrnyxANativeActivity *grnyx_native_activity(void) { return native_activity_g_native.initialized ? &native_activity_g_native.activity : NULL; }
GrnyxANativeWindow *grnyx_native_window(void) {
    return native_activity_g_native.window.refs > 0 ? &native_activity_g_native.window : NULL;
}
void grnyx_native_window_prepare(int32_t width, int32_t height, int32_t format) {
    if (native_activity_g_native.window.refs <= 0) native_activity_g_native.window.refs = 1;
    native_activity_g_native.window.width = width > 0 ? width : 1280;
    native_activity_g_native.window.height = height > 0 ? height : 720;
    native_activity_g_native.window.format = format;
}
GrnyxAInputQueue *grnyx_native_input_queue(void) { return native_activity_g_native.initialized ? &native_activity_g_native.input_queue : NULL; }
bool grnyx_native_window_dimensions(const GrnyxANativeWindow *window, int32_t *out_width, int32_t *out_height) {
    if (!window || window != &native_activity_g_native.window || native_activity_g_native.window.refs <= 0) return false;
    if (out_width) *out_width = window->width;
    if (out_height) *out_height = window->height;
    return window->width > 0 && window->height > 0;
}

bool grnyx_native_activity_start(void) {
    if (!native_activity_g_native.initialized) return false;
    GrnyxANativeActivityCallbacks *cb = &native_activity_g_native.callbacks;
    if (!cb->onStart || !cb->onResume || !cb->onNativeWindowCreated || !cb->onInputQueueCreated) {
        GRNYX_LOGCE("NATIVE", "guest NativeActivity callbacks incomplete start=%p resume=%p window=%p input=%p",
                    (void *)cb->onStart, (void *)cb->onResume,
                    (void *)cb->onNativeWindowCreated, (void *)cb->onInputQueueCreated);
        return false;
    }
    if (!grnyx_input_init()) {
        GRNYX_LOGCE("NATIVE", "Switch input backend init failed");
        return false;
    }
    native_activity_g_native.input_initialized = true;

    cb->onStart(&native_activity_g_native.activity);
    cb->onResume(&native_activity_g_native.activity);
    cb->onInputQueueCreated(&native_activity_g_native.activity, &native_activity_g_native.input_queue);
    cb->onNativeWindowCreated(&native_activity_g_native.activity, &native_activity_g_native.window);
    if (cb->onWindowFocusChanged) cb->onWindowFocusChanged(&native_activity_g_native.activity, 1);
    if (cb->onContentRectChanged) {
        GrnyxARect rect = {0, 0, native_activity_g_native.window.width, native_activity_g_native.window.height};
        cb->onContentRectChanged(&native_activity_g_native.activity, &rect);
    }
    native_activity_g_native.started = true;
    GRNYX_LOGCI("NATIVE", "NativeActivity lifecycle START/RESUME/window/input/focus delivered");
    return true;
}

void grnyx_native_activity_pump_input(bool *out_exit_requested) {
    if (out_exit_requested) *out_exit_requested = false;
    if (!native_activity_g_native.started || !native_activity_g_native.input_initialized) return;

    GrnyxInputSnapshot now;
    grnyx_input_update(&now);
    grnyx_sensors_publish(&now);
    if ((now.buttons_down & GRNYX_BTN_PLUS) || native_activity_g_native.finish_requested) {
        if (out_exit_requested) *out_exit_requested = true;
    }

    bool cur_touch = false;
    int32_t cur_id = -1;
    float cur_x = 0.0f, cur_y = 0.0f;
    if (now.touch_count > 0) {
        cur_id = (int32_t)now.touches[0].id;
        cur_touch = grnyx_display_map_touch(&native_activity_g_native.display_plan,
                                            now.touches[0].x, now.touches[0].y,
                                            &cur_x, &cur_y);
    }

    const bool prev_touch = native_activity_g_native.previous_touch_active;
    GrnyxAInputEvent event;
    if (!prev_touch && cur_touch) {
        native_activity_make_single_motion(&event, GRNYX_AMOTION_DOWN, cur_id, cur_x, cur_y);
        (void)native_activity_queue_event(&native_activity_g_native.input_queue, &event);
    } else if (prev_touch && cur_touch) {
        native_activity_make_single_motion(&event, GRNYX_AMOTION_MOVE, cur_id, cur_x, cur_y);
        (void)native_activity_queue_event(&native_activity_g_native.input_queue, &event);
    } else if (prev_touch && !cur_touch) {
        native_activity_make_single_motion(&event, GRNYX_AMOTION_UP,
                           native_activity_g_native.previous_touch_id,
                           native_activity_g_native.previous_touch_x,
                           native_activity_g_native.previous_touch_y);
        (void)native_activity_queue_event(&native_activity_g_native.input_queue, &event);
    }

    /* Controller buttons remain controller keys.  GRNYX no longer fabricates
     * touchscreen taps from gamepad buttons; that mixed host/Android behavior
     * violated the input contract and made touch-only/gamepad-aware apps
     * indistinguishable. */
    native_activity_queue_key_changes(&native_activity_g_native.input_queue, now.buttons_down, GRNYX_AKEY_DOWN);
    native_activity_queue_key_changes(&native_activity_g_native.input_queue, now.buttons_up, GRNYX_AKEY_UP);

    native_activity_g_native.previous_touch_active = cur_touch;
    if (cur_touch) {
        native_activity_g_native.previous_touch_id = cur_id;
        native_activity_g_native.previous_touch_x = cur_x;
        native_activity_g_native.previous_touch_y = cur_y;
    }
    native_activity_g_native.previous_input = now;
}

void grnyx_native_activity_stop(void) {
    if (!native_activity_g_native.started) return;
    GrnyxANativeActivityCallbacks *cb = &native_activity_g_native.callbacks;
    GRNYX_LOGCI("NATIVE", "NativeActivity shutdown lifecycle begin");
    if (cb->onWindowFocusChanged) cb->onWindowFocusChanged(&native_activity_g_native.activity, 0);
    if (cb->onInputQueueDestroyed) cb->onInputQueueDestroyed(&native_activity_g_native.activity, &native_activity_g_native.input_queue);
    if (cb->onNativeWindowDestroyed) cb->onNativeWindowDestroyed(&native_activity_g_native.activity, &native_activity_g_native.window);
    if (cb->onPause) cb->onPause(&native_activity_g_native.activity);
    if (cb->onStop) cb->onStop(&native_activity_g_native.activity);
    if (cb->onDestroy) cb->onDestroy(&native_activity_g_native.activity);
    native_activity_g_native.started = false;
    GRNYX_LOGCI("NATIVE", "NativeActivity shutdown lifecycle complete");
}

void ANativeActivity_finish(GrnyxANativeActivity *activity) {
    (void)activity;
    native_activity_g_native.finish_requested = true;
}
void ANativeActivity_setWindowFormat(GrnyxANativeActivity *activity, int32_t format) {
    (void)activity; native_activity_g_native.window.format = format;
}
void ANativeActivity_setWindowFlags(GrnyxANativeActivity *activity, uint32_t add_flags, uint32_t remove_flags) {
    (void)activity; (void)add_flags; (void)remove_flags;
}
void ANativeActivity_showSoftInput(GrnyxANativeActivity *activity, uint32_t flags) { (void)activity; (void)flags; }
void ANativeActivity_hideSoftInput(GrnyxANativeActivity *activity, uint32_t flags) { (void)activity; (void)flags; }

void ANativeWindow_acquire(GrnyxANativeWindow *window) { if (window) window->refs++; }
void ANativeWindow_release(GrnyxANativeWindow *window) { if (window && window->refs > 0) window->refs--; }
int32_t ANativeWindow_getWidth(GrnyxANativeWindow *window) { return window ? window->width : 0; }
int32_t ANativeWindow_getHeight(GrnyxANativeWindow *window) { return window ? window->height : 0; }
int32_t ANativeWindow_getFormat(GrnyxANativeWindow *window) { return window ? window->format : 0; }
int32_t ANativeWindow_setBuffersGeometry(GrnyxANativeWindow *window, int32_t width, int32_t height, int32_t format) {
    if (!window) return -EINVAL;
    /* Android permits width/height 0 to mean the base window dimensions. */
    if (width > 0) window->width = width;
    if (height > 0) window->height = height;
    if (format != 0) window->format = format;
    return 0;
}

GrnyxANativeWindow *ANativeWindow_fromSurface(void *env, void *surface) {
    (void)env;
    if (!surface || !grnyx_jni_is_instance_of_class_host(surface, "android/view/Surface")) return NULL;
    GrnyxANativeWindow *window = grnyx_native_window();
    if (!window) return NULL;
    ANativeWindow_acquire(window);
    return window;
}

void *ANativeWindow_toSurface(void *env, GrnyxANativeWindow *window) {
    if (!env || !window) return NULL;
    void *surface = grnyx_jni_new_local_object(env, "android/view/Surface");
    if (!surface) return NULL;
    (void)grnyx_jni_set_object_host_data(surface, window);
    return surface;
}

int32_t ANativeWindow_lock(GrnyxANativeWindow *window, void *buffer, GrnyxARect *dirty) {
    (void)dirty;
    if (!window || !buffer) return -22; /* Android EINVAL, independent of host errno. */
    GRNYX_LOGCW("NATIVE", "ANativeWindow_lock unavailable: no CPU window-buffer backend");
    return -38; /* ENOSYS: leave the caller's buffer and dirty rectangle untouched. */
}

int32_t ANativeWindow_unlockAndPost(GrnyxANativeWindow *window) {
    if (!window) return -22;
    return -38;
}

static uint64_t native_activity_choreographer_now_ns(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint64_t native_activity_choreographer_next_frame_ns(uint64_t earliest_ns) {
    if (earliest_ns == 0) earliest_ns = 1;
    const uint64_t rem = earliest_ns % GRNYX_CHOREO_FRAME_NS;
    return rem ? earliest_ns + (GRNYX_CHOREO_FRAME_NS - rem)
               : earliest_ns + GRNYX_CHOREO_FRAME_NS;
}

GrnyxAChoreographer *AChoreographer_getInstance(void) {
    if (!native_activity_g_thread_choreographer.initialized) {
        native_activity_g_thread_choreographer.owner = pthread_self();
        native_activity_g_thread_choreographer.looper = native_activity_g_thread_looper;
        native_activity_g_thread_choreographer.initialized = true;
    } else if (!native_activity_g_thread_choreographer.looper && native_activity_g_thread_looper) {
        /* Android associates a Choreographer with the calling thread's Looper.
         * Keep the historical permissive getInstance() behavior for callers that
         * query before preparing a Looper, but bind it as soon as one exists. */
        native_activity_g_thread_choreographer.looper = native_activity_g_thread_looper;
    }
    if (atomic_fetch_add(&native_activity_g_choreo_get_reports, 1) < 8u || native_activity_choreographer_full_trace())
        GRNYX_LOGCI("CHOREO", "get choreographer=%p owner=%p looper=%p caller=%p",
                    (void *)&native_activity_g_thread_choreographer,
                    (void *)(uintptr_t)native_activity_g_thread_choreographer.owner,
                    (void *)native_activity_g_thread_choreographer.looper,
                    __builtin_extract_return_addr(__builtin_return_address(0)));
    return &native_activity_g_thread_choreographer;
}

static void native_activity_choreographer_post_delayed(GrnyxAChoreographer *choreographer, bool is64,
                                       void *callback, void *data, uint32_t delay_ms,
                                       void *caller) {
    if (!choreographer || !callback) return;
    native_activity_GrnyxChoreographerCallback *node = calloc(1, sizeof(*node));
    if (!node) return;

    const uint64_t now = native_activity_choreographer_now_ns();
    uint64_t earliest = now;
    if (delay_ms > 0 && UINT64_MAX - earliest > (uint64_t)delay_ms * UINT64_C(1000000))
        earliest += (uint64_t)delay_ms * UINT64_C(1000000);
    else if (delay_ms > 0)
        earliest = UINT64_MAX - GRNYX_CHOREO_FRAME_NS;

    node->owner = choreographer->initialized ? choreographer->owner : pthread_self();
    node->looper = choreographer->looper;
    node->due_ns = native_activity_choreographer_next_frame_ns(earliest);
    node->is64 = is64;
    if (is64) node->fn.cb64 = (void (*)(int64_t, void *))callback;
    else node->fn.legacy = (void (*)(long, void *))callback;
    node->data = data;
    node->caller = caller;
    /* Dispatch can free the node as soon as it is queued. Keep reporting/wake
     * values local instead of inspecting the node after releasing the lock. */
    GrnyxALooper *looper = node->looper;
    const pthread_t owner = node->owner;
    const uint64_t due_ns = node->due_ns;

    pthread_mutex_lock(&native_activity_g_choreo_lock);
    if (native_activity_g_choreo_tail) native_activity_g_choreo_tail->next = node; else native_activity_g_choreo_head = node;
    native_activity_g_choreo_tail = node;
    pthread_mutex_unlock(&native_activity_g_choreo_lock);

    /* AChoreographer callbacks are delivered by the owning ALooper. Posting
     * from another thread must make a sleeping pollAll() reconsider its timer. */
    if (looper) ALooper_wake(looper);

    if (atomic_fetch_add(&native_activity_g_choreo_post_reports, 1) < 8u || native_activity_choreographer_full_trace()) {
        GRNYX_LOGCI("CHOREO", "post owner=%p looper=%p callback=%p data=%p is64=%d delayMs=%u nowNs=%llu dueNs=%llu caller=%p",
                    (void *)(uintptr_t)owner, (void *)looper, callback, data, is64,
                    delay_ms, (unsigned long long)now, (unsigned long long)due_ns, caller);
    }
}

void AChoreographer_postFrameCallback(GrnyxAChoreographer *choreographer,
                                      void (*callback)(long, void *), void *data) {
    native_activity_choreographer_post_delayed(choreographer, false, (void *)callback, data, 0,
                              __builtin_extract_return_addr(__builtin_return_address(0)));
}

void AChoreographer_postFrameCallback64(GrnyxAChoreographer *choreographer,
                                        void (*callback)(int64_t, void *), void *data) {
    native_activity_choreographer_post_delayed(choreographer, true, (void *)callback, data, 0,
                              __builtin_extract_return_addr(__builtin_return_address(0)));
}

void AChoreographer_postFrameCallbackDelayed(GrnyxAChoreographer *choreographer,
                                             void (*callback)(long, void *), void *data,
                                             long delay_millis) {
    const uint32_t delay = delay_millis <= 0 ? 0u :
                           (delay_millis > (long)UINT32_MAX ? UINT32_MAX : (uint32_t)delay_millis);
    native_activity_choreographer_post_delayed(choreographer, false, (void *)callback, data, delay,
                              __builtin_extract_return_addr(__builtin_return_address(0)));
}

void AChoreographer_postFrameCallbackDelayed64(GrnyxAChoreographer *choreographer,
                                               void (*callback)(int64_t, void *), void *data,
                                               uint32_t delay_millis) {
    native_activity_choreographer_post_delayed(choreographer, true, (void *)callback, data, delay_millis,
                              __builtin_extract_return_addr(__builtin_return_address(0)));
}

static size_t native_activity_choreographer_dispatch_due(GrnyxALooper *looper, pthread_t self) {
    const uint64_t now = native_activity_choreographer_now_ns();
    native_activity_GrnyxChoreographerCallback *run_head = NULL, *run_tail = NULL;

    pthread_mutex_lock(&native_activity_g_choreo_lock);
    native_activity_GrnyxChoreographerCallback **link = &native_activity_g_choreo_head;
    while (*link) {
        native_activity_GrnyxChoreographerCallback *node = *link;
        const bool owner_match = node->looper ? (node->looper == looper)
                                              : pthread_equal(node->owner, self);
        if (!owner_match || node->due_ns > now) {
            link = &node->next;
            continue;
        }
        *link = node->next;
        node->next = NULL;
        if (run_tail) run_tail->next = node; else run_head = node;
        run_tail = node;
    }
    native_activity_g_choreo_tail = NULL;
    for (native_activity_GrnyxChoreographerCallback *it = native_activity_g_choreo_head; it; it = it->next) native_activity_g_choreo_tail = it;
    pthread_mutex_unlock(&native_activity_g_choreo_lock);

    size_t count = 0;
    const int64_t frame_time = (int64_t)now;
    while (run_head) {
        native_activity_GrnyxChoreographerCallback *next = run_head->next;
        const bool report = atomic_fetch_add(&native_activity_g_choreo_dispatch_reports, 1) < 8u || native_activity_choreographer_full_trace();
        void *callback = run_head->is64 ? (void *)run_head->fn.cb64 : (void *)run_head->fn.legacy;
        if (report)
            GRNYX_LOGCI("CHOREO", "dispatch BEGIN owner=%p current=%p looper=%p callback=%p data=%p is64=%d frameNs=%lld dueNs=%llu postCaller=%p",
                        (void *)(uintptr_t)run_head->owner, (void *)(uintptr_t)self,
                        (void *)run_head->looper, callback, run_head->data, run_head->is64,
                        (long long)frame_time, (unsigned long long)run_head->due_ns, run_head->caller);
        const uint64_t begin_ns = report ? native_activity_choreographer_now_ns() : 0;
        if (run_head->is64) run_head->fn.cb64(frame_time, run_head->data);
        else run_head->fn.legacy((long)frame_time, run_head->data);
        if (report)
            GRNYX_LOGCI("CHOREO", "dispatch END callback=%p data=%p frameNs=%lld elapsedNs=%llu",
                        callback, run_head->data, (long long)frame_time,
                        (unsigned long long)(native_activity_choreographer_now_ns() - begin_ns));
        free(run_head);
        run_head = next;
        count++;
    }
    if (count && native_activity_choreographer_full_trace()) {
        GRNYX_LOGCI("CHOREO", "dispatch looper=%p callbacks=%zu frameNs=%lld",
                    (void *)looper, count, (long long)frame_time);
    }
    return count;
}

static int native_activity_choreographer_wait_ms(GrnyxALooper *looper, pthread_t self) {
    uint64_t next_due = UINT64_MAX;
    pthread_mutex_lock(&native_activity_g_choreo_lock);
    for (native_activity_GrnyxChoreographerCallback *it = native_activity_g_choreo_head; it; it = it->next) {
        const bool owner_match = it->looper ? (it->looper == looper)
                                            : pthread_equal(it->owner, self);
        if (owner_match && it->due_ns < next_due) next_due = it->due_ns;
    }
    pthread_mutex_unlock(&native_activity_g_choreo_lock);
    if (next_due == UINT64_MAX) return -1;

    const uint64_t now = native_activity_choreographer_now_ns();
    if (next_due <= now) return 0;
    const uint64_t delta_ns = next_due - now;
    uint64_t ms = (delta_ns + UINT64_C(999999)) / UINT64_C(1000000);
    if (ms > (uint64_t)INT32_MAX) ms = INT32_MAX;
    return (int)ms;
}

void grnyx_choreographer_pump(void) {
    (void)native_activity_choreographer_dispatch_due(native_activity_g_thread_looper, pthread_self());
    const uint64_t now = native_activity_choreographer_now_ns();
    native_activity_GrnyxChoreographerCallback overdue[4];
    size_t count = 0, queued = 0, waiting = 0;
    pthread_mutex_lock(&native_activity_g_choreo_lock);
    if (now >= native_activity_g_choreo_last_queue_report_ns &&
        now - native_activity_g_choreo_last_queue_report_ns >= UINT64_C(5000000000)) {
        native_activity_g_choreo_last_queue_report_ns = now;
        for (native_activity_GrnyxChoreographerCallback *it = native_activity_g_choreo_head; it; it = it->next) {
            ++queued;
            if (now < it->due_ns || now - it->due_ns < UINT64_C(1000000000)) continue;
            ++waiting;
            if (count < sizeof(overdue) / sizeof(overdue[0])) overdue[count++] = *it;
        }
    }
    pthread_mutex_unlock(&native_activity_g_choreo_lock);
    for (size_t i = 0; i < count; ++i) {
        GRNYX_LOGCW("CHOREO", "queued overdueMs=%llu queued=%zu overdue=%zu owner=%p current=%p looper=%p callback=%p data=%p is64=%d dueNs=%llu nowNs=%llu postCaller=%p",
                    (unsigned long long)((now - overdue[i].due_ns) / UINT64_C(1000000)),
                    queued, waiting, (void *)(uintptr_t)overdue[i].owner,
                    (void *)(uintptr_t)pthread_self(), (void *)overdue[i].looper,
                    overdue[i].is64 ? (void *)overdue[i].fn.cb64 : (void *)overdue[i].fn.legacy,
                    overdue[i].data, overdue[i].is64, (unsigned long long)overdue[i].due_ns,
                    (unsigned long long)now, overdue[i].caller);
    }
}

GrnyxAConfiguration *AConfiguration_new(void) {
    return (GrnyxAConfiguration *)calloc(1, sizeof(GrnyxAConfiguration));
}

void AConfiguration_delete(GrnyxAConfiguration *config) { free(config); }

void AConfiguration_copy(GrnyxAConfiguration *dest, GrnyxAConfiguration *src) {
    if (dest && src) dest->values = src->values;
}

void AConfiguration_fromAssetManager(GrnyxAConfiguration *config, GrnyxAssetManager *manager) {
    if (!config) return;
    GrnyxAssetConfiguration values = {0};
    if (grnyx_assets_get_configuration(manager, &values)) config->values = values;
    else memset(&config->values, 0, sizeof(config->values));
}

#define CONFIG_GETTER(name, field) \
    int32_t AConfiguration_get##name(GrnyxAConfiguration *config) { return config ? config->values.field : 0; }
#define CONFIG_SETTER(name, field) \
    void AConfiguration_set##name(GrnyxAConfiguration *config, int32_t value) { if (config) config->values.field = value; }

CONFIG_GETTER(Mcc, mcc)
CONFIG_SETTER(Mcc, mcc)
CONFIG_GETTER(Mnc, mnc)
CONFIG_SETTER(Mnc, mnc)

void AConfiguration_getLanguage(GrnyxAConfiguration *config, char *out) {
    if (!out) return;
    out[0] = config ? config->values.language[0] : 0;
    out[1] = config ? config->values.language[1] : 0;
}
void AConfiguration_setLanguage(GrnyxAConfiguration *config, const char *language) {
    if (!config) return;
    config->values.language[0] = language ? language[0] : 0;
    config->values.language[1] = language ? language[1] : 0;
}
void AConfiguration_getCountry(GrnyxAConfiguration *config, char *out) {
    if (!out) return;
    out[0] = config ? config->values.country[0] : 0;
    out[1] = config ? config->values.country[1] : 0;
}
void AConfiguration_setCountry(GrnyxAConfiguration *config, const char *country) {
    if (!config) return;
    config->values.country[0] = country ? country[0] : 0;
    config->values.country[1] = country ? country[1] : 0;
}

CONFIG_GETTER(Orientation, orientation)
CONFIG_SETTER(Orientation, orientation)
CONFIG_GETTER(Touchscreen, touchscreen)
CONFIG_SETTER(Touchscreen, touchscreen)
CONFIG_GETTER(Density, density)
CONFIG_SETTER(Density, density)
CONFIG_GETTER(Keyboard, keyboard)
CONFIG_SETTER(Keyboard, keyboard)
CONFIG_GETTER(Navigation, navigation)
CONFIG_SETTER(Navigation, navigation)
CONFIG_GETTER(KeysHidden, keys_hidden)
CONFIG_SETTER(KeysHidden, keys_hidden)
CONFIG_GETTER(NavHidden, nav_hidden)
CONFIG_SETTER(NavHidden, nav_hidden)
CONFIG_GETTER(SdkVersion, sdk_version)
CONFIG_SETTER(SdkVersion, sdk_version)
CONFIG_GETTER(ScreenSize, screen_size)
CONFIG_SETTER(ScreenSize, screen_size)
CONFIG_GETTER(ScreenLong, screen_long)
CONFIG_SETTER(ScreenLong, screen_long)
CONFIG_GETTER(UiModeType, ui_mode_type)
CONFIG_SETTER(UiModeType, ui_mode_type)
CONFIG_GETTER(UiModeNight, ui_mode_night)
CONFIG_SETTER(UiModeNight, ui_mode_night)
CONFIG_GETTER(ScreenWidthDp, screen_width_dp)
CONFIG_SETTER(ScreenWidthDp, screen_width_dp)
CONFIG_GETTER(ScreenHeightDp, screen_height_dp)
CONFIG_SETTER(ScreenHeightDp, screen_height_dp)
CONFIG_GETTER(SmallestScreenWidthDp, smallest_screen_width_dp)
CONFIG_SETTER(SmallestScreenWidthDp, smallest_screen_width_dp)
CONFIG_GETTER(LayoutDirection, layout_direction)
CONFIG_SETTER(LayoutDirection, layout_direction)
CONFIG_GETTER(ScreenRound, screen_round)
CONFIG_SETTER(ScreenRound, screen_round)

#undef CONFIG_GETTER
#undef CONFIG_SETTER

GrnyxALooper *ALooper_forThread(void) {
    return native_activity_g_thread_looper;
}

static short native_activity_looper_poll_events(int events) {
    short out = 0;
    if (events & GRNYX_ALOOPER_EVENT_INPUT) out |= POLLIN;
    if (events & GRNYX_ALOOPER_EVENT_OUTPUT) out |= POLLOUT;
    return out;
}

static int native_activity_looper_android_events(short revents) {
    int out = 0;
    if (revents & POLLIN) out |= GRNYX_ALOOPER_EVENT_INPUT;
    if (revents & POLLOUT) out |= GRNYX_ALOOPER_EVENT_OUTPUT;
    if (revents & POLLERR) out |= GRNYX_ALOOPER_EVENT_ERROR;
    if (revents & POLLHUP) out |= GRNYX_ALOOPER_EVENT_HANGUP;
    if (revents & POLLNVAL) out |= GRNYX_ALOOPER_EVENT_INVALID;
    return out;
}

static void native_activity_looper_drain_wake(GrnyxALooper *looper) {
    if (!looper || looper->wake_read_fd < 0) return;
    unsigned char buffer[64];
    while (grnyx_fd_read(looper->wake_read_fd, buffer, sizeof(buffer)) > 0) {}
}

GrnyxALooper *ALooper_prepare(int opts) {
    if (native_activity_g_thread_looper) return native_activity_g_thread_looper;
    if ((opts & ~GRNYX_ALOOPER_PREPARE_ALLOW_NON_CALLBACKS) != 0) return NULL;
    GrnyxALooper *looper = (GrnyxALooper *)calloc(1, sizeof(*looper));
    if (!looper) return NULL;
    int wake_fds[2] = {-1, -1};
    if (grnyx_fd_pipe(wake_fds) != 0) {
        free(looper);
        return NULL;
    }
    if (pthread_mutex_init(&looper->lock, NULL) != 0) {
        (void)grnyx_fd_close(wake_fds[0]);
        (void)grnyx_fd_close(wake_fds[1]);
        free(looper);
        return NULL;
    }
    looper->opts = opts;
    looper->owner_thread = pthread_self();
    looper->wake_read_fd = wake_fds[0];
    looper->wake_write_fd = wake_fds[1];
    atomic_init(&looper->references, 1);
    native_activity_g_thread_looper = looper;
    return looper;
}

void ALooper_acquire(GrnyxALooper *looper) {
    if (looper) (void)atomic_fetch_add_explicit(&looper->references, 1, memory_order_relaxed);
}

void ALooper_release(GrnyxALooper *looper) {
    if (!looper) return;
    int refs = atomic_load_explicit(&looper->references, memory_order_relaxed);
    while (refs > 1 && !atomic_compare_exchange_weak_explicit(&looper->references, &refs, refs - 1,
                                                               memory_order_release, memory_order_relaxed)) {}
}

void ALooper_wake(GrnyxALooper *looper) {
    if (!looper || looper->wake_write_fd < 0) return;
    const unsigned char token = 1;
    if (grnyx_fd_write(looper->wake_write_fd, &token, 1) < 0 && errno != EAGAIN) {
        GRNYX_LOGCW("NATIVE", "ALooper wake write failed errno=%d", errno);
    }
}

static void native_activity_looper_wake_if_cross_thread(GrnyxALooper *looper) {
    if (looper && !pthread_equal(pthread_self(), looper->owner_thread)) ALooper_wake(looper);
}

int ALooper_addFd(GrnyxALooper *looper, int fd, int ident, int events,
                  int (*callback)(int fd, int events, void *data), void *data) {
    const int allowed_events = GRNYX_ALOOPER_EVENT_INPUT | GRNYX_ALOOPER_EVENT_OUTPUT;
    if (!looper || fd < 0 || !(events & allowed_events) || (events & ~allowed_events)) return -1;
    if (!callback && (!(looper->opts & GRNYX_ALOOPER_PREPARE_ALLOW_NON_CALLBACKS) || ident < 0)) return -1;

    pthread_mutex_lock(&looper->lock);
    for (size_t i = 0; i < looper->fd_count; ++i) {
        if (looper->fds[i].fd == fd) {
            looper->fds[i] = (native_activity_GrnyxLooperFd){fd, ident, events, callback, data};
            pthread_mutex_unlock(&looper->lock);
            native_activity_looper_wake_if_cross_thread(looper);
            return 1;
        }
    }
    if (looper->fd_count == looper->fd_capacity) {
        size_t next_capacity = looper->fd_capacity ? looper->fd_capacity * 2u : 8u;
        native_activity_GrnyxLooperFd *grown = (native_activity_GrnyxLooperFd *)realloc(looper->fds, next_capacity * sizeof(*grown));
        if (!grown) {
            pthread_mutex_unlock(&looper->lock);
            return -1;
        }
        looper->fds = grown;
        looper->fd_capacity = next_capacity;
    }
    looper->fds[looper->fd_count++] = (native_activity_GrnyxLooperFd){fd, ident, events, callback, data};
    pthread_mutex_unlock(&looper->lock);
    native_activity_looper_wake_if_cross_thread(looper);
    return 1;
}

int ALooper_removeFd(GrnyxALooper *looper, int fd) {
    if (!looper || fd < 0) return 0;
    int removed = 0;
    pthread_mutex_lock(&looper->lock);
    for (size_t i = 0; i < looper->fd_count; ++i) {
        if (looper->fds[i].fd != fd) continue;
        memmove(&looper->fds[i], &looper->fds[i + 1],
                (looper->fd_count - i - 1u) * sizeof(looper->fds[0]));
        looper->fd_count--;
        removed = 1;
        break;
    }
    pthread_mutex_unlock(&looper->lock);
    if (removed) native_activity_looper_wake_if_cross_thread(looper);
    return removed;
}

static int native_activity_poll_input_queue(int *out_events, void **out_data) {
    GrnyxAInputQueue *queue = &native_activity_g_native.input_queue;
    if (queue->looper == native_activity_g_thread_looper && native_activity_queue_has_events(queue)) {
        if (out_events) *out_events = GRNYX_ALOOPER_EVENT_INPUT;
        if (out_data) *out_data = queue->data;
        return queue->ident;
    }
    return GRNYX_ALOOPER_POLL_TIMEOUT;
}

static int native_activity_looper_process_ready(GrnyxALooper *looper, const native_activity_GrnyxLooperFd *snapshot,
                                const short *revents, size_t count,
                                int *out_fd, int *out_events, void **out_data) {
    bool callback_called = false;
    int ready_ident = GRNYX_ALOOPER_POLL_TIMEOUT;
    int ready_fd = -1;
    int ready_events = 0;
    void *ready_data = NULL;

    for (size_t i = 0; i < count; ++i) {
        if (!revents[i]) continue;
        const int events = native_activity_looper_android_events(revents[i]);
        if (!events) continue;
        if (snapshot[i].callback) {
            const int keep = snapshot[i].callback(snapshot[i].fd, events, snapshot[i].data);
            callback_called = true;
            if (!keep) (void)ALooper_removeFd(looper, snapshot[i].fd);
        } else if (ready_ident == GRNYX_ALOOPER_POLL_TIMEOUT) {
            ready_ident = snapshot[i].ident;
            ready_fd = snapshot[i].fd;
            ready_events = events;
            ready_data = snapshot[i].data;
        }
    }

    if (ready_ident >= 0) {
        if (out_fd) *out_fd = ready_fd;
        if (out_events) *out_events = ready_events;
        if (out_data) *out_data = ready_data;
        return ready_ident;
    }
    return callback_called ? GRNYX_ALOOPER_POLL_CALLBACK : GRNYX_ALOOPER_POLL_TIMEOUT;
}

/* The looper's host waits are signal safe points (grnyx_signal_safe_call):
 * a guest thread parked in ALooper_pollOnce still takes thread-directed
 * signals. No guest callback runs inside them. */
typedef struct native_activity_LooperHostWait {
    struct pollfd *fds;
    nfds_t count;
    int timeout_ms;
    struct timespec *sleep;
} native_activity_LooperHostWait;

static long native_activity_looper_host_wait_run(void *ctx) {
    native_activity_LooperHostWait *wait = (native_activity_LooperHostWait *)ctx;
    if (wait->fds) return poll(wait->fds, wait->count, wait->timeout_ms);
    return nanosleep(wait->sleep, wait->sleep);
}

int ALooper_pollOnce(int timeout_millis, int *out_fd, int *out_events, void **out_data) {
    GrnyxALooper *looper = native_activity_g_thread_looper;
    if (out_fd) *out_fd = -1;
    if (out_events) *out_events = 0;
    if (out_data) *out_data = NULL;
    if (!looper) return GRNYX_ALOOPER_POLL_ERROR;

    const bool wait_forever = timeout_millis < 0;
    int remaining_ms = timeout_millis;

    for (;;) {
        if (native_activity_choreographer_dispatch_due(looper, pthread_self()) > 0)
            return GRNYX_ALOOPER_POLL_CALLBACK;

        int input_ready = native_activity_poll_input_queue(out_events, out_data);
        if (input_ready != GRNYX_ALOOPER_POLL_TIMEOUT) return input_ready;

        pthread_mutex_lock(&looper->lock);
        const size_t count = looper->fd_count;
        native_activity_GrnyxLooperFd *snapshot = count ? (native_activity_GrnyxLooperFd *)malloc(count * sizeof(*snapshot)) : NULL;
        if (count && !snapshot) {
            pthread_mutex_unlock(&looper->lock);
            return GRNYX_ALOOPER_POLL_ERROR;
        }
        if (count) memcpy(snapshot, looper->fds, count * sizeof(*snapshot));
        pthread_mutex_unlock(&looper->lock);

        short *revents = count ? (short *)calloc(count, sizeof(*revents)) : NULL;
        struct pollfd *host = count ? (struct pollfd *)calloc(count, sizeof(*host)) : NULL;
        size_t *host_map = count ? (size_t *)calloc(count, sizeof(*host_map)) : NULL;
        if (count && (!revents || !host || !host_map)) {
            free(snapshot); free(revents); free(host); free(host_map);
            return GRNYX_ALOOPER_POLL_ERROR;
        }

        bool immediate = false;
        size_t host_count = 0;
        for (size_t i = 0; i < count; ++i) {
            const short requested = native_activity_looper_poll_events(snapshot[i].events);
            if (grnyx_fd_is_virtual(snapshot[i].fd)) {
                revents[i] = grnyx_fd_poll_revents(snapshot[i].fd, requested);
                if (revents[i]) immediate = true;
            } else {
                host[host_count].fd = snapshot[i].fd;
                host[host_count].events = requested;
                host_map[host_count++] = i;
            }
        }

        const bool wake_ready = (grnyx_fd_poll_revents(looper->wake_read_fd, POLLIN) & POLLIN) != 0;
        if (wake_ready) immediate = true;

        int slice_ms = 0;
        if (!immediate && timeout_millis != 0) {
            slice_ms = wait_forever ? 8 : (remaining_ms < 8 ? remaining_ms : 8);
            const int choreo_ms = native_activity_choreographer_wait_ms(looper, pthread_self());
            if (choreo_ms >= 0 && choreo_ms < slice_ms) slice_ms = choreo_ms;
            if (slice_ms < 0) slice_ms = 0;
        }

        if (host_count > 0) {
            int rc;
            native_activity_LooperHostWait wait = { host, (nfds_t)host_count, immediate ? 0 : slice_ms, NULL };
            do { rc = (int)grnyx_signal_safe_call(native_activity_looper_host_wait_run, &wait); } while (rc < 0 && errno == EINTR);
            if (rc < 0) {
                free(snapshot); free(revents); free(host); free(host_map);
                return GRNYX_ALOOPER_POLL_ERROR;
            }
            for (size_t i = 0; i < host_count; ++i) revents[host_map[i]] = host[i].revents;
        } else if (!immediate && slice_ms > 0) {
            struct timespec ts = {.tv_sec = slice_ms / 1000,
                                  .tv_nsec = (long)(slice_ms % 1000) * 1000000L};
            native_activity_LooperHostWait wait = { NULL, 0, 0, &ts };
            while (grnyx_signal_safe_call(native_activity_looper_host_wait_run, &wait) != 0 && errno == EINTR) {}
        }

        const int ready = native_activity_looper_process_ready(looper, snapshot, revents, count,
                                               out_fd, out_events, out_data);
        free(snapshot); free(revents); free(host); free(host_map);
        if (ready != GRNYX_ALOOPER_POLL_TIMEOUT) return ready;

        if (wake_ready || (grnyx_fd_poll_revents(looper->wake_read_fd, POLLIN) & POLLIN)) {
            native_activity_looper_drain_wake(looper);
            return GRNYX_ALOOPER_POLL_WAKE;
        }
        if (timeout_millis == 0) return GRNYX_ALOOPER_POLL_TIMEOUT;
        if (!wait_forever) {
            remaining_ms -= slice_ms;
            if (remaining_ms <= 0) return GRNYX_ALOOPER_POLL_TIMEOUT;
        }
    }
}

int ALooper_pollAll(int timeout_millis, int *out_fd, int *out_events, void **out_data) {
    int result;
    do {
        result = ALooper_pollOnce(timeout_millis, out_fd, out_events, out_data);
        if (result == GRNYX_ALOOPER_POLL_CALLBACK) timeout_millis = 0;
    } while (result == GRNYX_ALOOPER_POLL_CALLBACK);
    return result;
}

void AInputQueue_attachLooper(GrnyxAInputQueue *queue, GrnyxALooper *looper, int ident,
                              int (*callback)(int fd, int events, void *data), void *data) {
    if (!queue) return;
    pthread_mutex_lock(&queue->lock);
    queue->looper = looper; queue->ident = ident; queue->callback = callback; queue->data = data;
    pthread_mutex_unlock(&queue->lock);
    native_activity_queue_wake(queue);
}
void AInputQueue_detachLooper(GrnyxAInputQueue *queue) {
    if (!queue) return;
    pthread_mutex_lock(&queue->lock);
    queue->looper = NULL; queue->ident = 0; queue->callback = NULL; queue->data = NULL;
    pthread_mutex_unlock(&queue->lock);
}
int32_t AInputQueue_getEvent(GrnyxAInputQueue *queue, GrnyxAInputEvent **out_event) {
    if (!queue || !out_event) return -1;
    pthread_mutex_lock(&queue->lock);
    if (queue->current_valid || queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    queue->current = queue->events[queue->head];
    queue->head = (queue->head + 1) % GRNYX_INPUT_QUEUE_CAP;
    queue->count--;
    queue->current_valid = true;
    *out_event = &queue->current;
    pthread_mutex_unlock(&queue->lock);
    return 0;
}
int32_t AInputQueue_preDispatchEvent(GrnyxAInputQueue *queue, GrnyxAInputEvent *event) { (void)queue; (void)event; return 0; }
void AInputQueue_finishEvent(GrnyxAInputQueue *queue, GrnyxAInputEvent *event, int handled) {
    (void)event; (void)handled;
    if (!queue) return;
    pthread_mutex_lock(&queue->lock); queue->current_valid = false; pthread_mutex_unlock(&queue->lock);
}
int32_t AInputEvent_getType(const GrnyxAInputEvent *event) { return event ? event->type : 0; }
int32_t AKeyEvent_getAction(const GrnyxAInputEvent *event) { return event && event->type == GRNYX_AINPUT_EVENT_TYPE_KEY ? event->action : 0; }
int32_t AKeyEvent_getKeyCode(const GrnyxAInputEvent *event) { return event && event->type == GRNYX_AINPUT_EVENT_TYPE_KEY ? event->key_code : 0; }
int32_t AMotionEvent_getAction(const GrnyxAInputEvent *event) { return event ? event->action : 0; }
size_t AMotionEvent_getPointerCount(const GrnyxAInputEvent *event) { return event ? event->pointer_count : 0; }
int32_t AMotionEvent_getPointerId(const GrnyxAInputEvent *event, size_t i) { return event && i<event->pointer_count ? event->ids[i] : -1; }
float AMotionEvent_getX(const GrnyxAInputEvent *event, size_t i) { return event && i<event->pointer_count ? event->xs[i] : 0.0f; }
float AMotionEvent_getY(const GrnyxAInputEvent *event, size_t i) { return event && i<event->pointer_count ? event->ys[i] : 0.0f; }

#pragma pop_macro("GRNYX_INPUT_QUEUE_CAP")
#pragma pop_macro("GRNYX_INPUT_MAX_POINTERS")
#pragma pop_macro("GRNYX_CHOREO_FRAME_NS")
#pragma pop_macro("GRNYX_AMOTION_UP")
#pragma pop_macro("GRNYX_AMOTION_MOVE")
#pragma pop_macro("GRNYX_AMOTION_DOWN")
#pragma pop_macro("GRNYX_ALOOPER_PREPARE_ALLOW_NON_CALLBACKS")
#pragma pop_macro("GRNYX_ALOOPER_POLL_WAKE")
#pragma pop_macro("GRNYX_ALOOPER_POLL_TIMEOUT")
#pragma pop_macro("GRNYX_ALOOPER_POLL_ERROR")
#pragma pop_macro("GRNYX_ALOOPER_POLL_CALLBACK")
#pragma pop_macro("GRNYX_ALOOPER_EVENT_OUTPUT")
#pragma pop_macro("GRNYX_ALOOPER_EVENT_INVALID")
#pragma pop_macro("GRNYX_ALOOPER_EVENT_INPUT")
#pragma pop_macro("GRNYX_ALOOPER_EVENT_HANGUP")
#pragma pop_macro("GRNYX_ALOOPER_EVENT_ERROR")
#pragma pop_macro("GRNYX_AKEY_UP")
#pragma pop_macro("GRNYX_AKEY_DOWN")
#pragma pop_macro("GRNYX_AINPUT_EVENT_TYPE_MOTION")
#pragma pop_macro("GRNYX_AINPUT_EVENT_TYPE_KEY")
#pragma pop_macro("CONFIG_SETTER")
#pragma pop_macro("CONFIG_GETTER")
#endif /* NX_CONTROLLER_HOST_TEST */
