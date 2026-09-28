/* Compile against the actual worker. SendInput is intercepted: no desktop clicks. */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>

static LONGLONG samples[40000];
static volatile LONG sample_count, invalid_batch, fail_input, stall_input;
static HANDLE first_click;
static DWORD test_duration = 3000;
static UINT WINAPI measured_input(UINT count, LPINPUT input, int size) {
    LARGE_INTEGER now;
    LONG index = sample_count;
    DWORD down = input[count - 2].mi.dwFlags, up = input[count - 1].mi.dwFlags;
    if (size != sizeof(INPUT) || (count != 2 && count != 3) ||
        !((down == MOUSEEVENTF_LEFTDOWN && up == MOUSEEVENTF_LEFTUP) ||
          (down == MOUSEEVENTF_RIGHTDOWN && up == MOUSEEVENTF_RIGHTUP))) invalid_batch = 1;
    if (count == 3 && (input[0].mi.dwFlags != MOUSEEVENTF_MOVE ||
        abs(input[0].mi.dx) > 1 || abs(input[0].mi.dy) > 1)) invalid_batch = 1;
    QueryPerformanceCounter(&now);
    if (index < 40000) samples[index] = now.QuadPart;
    InterlockedIncrement(&sample_count);
    if (!index) SetEvent(first_click);
    if (InterlockedExchange(&stall_input, 0)) Sleep(30);
    return fail_input ? 0 : count;
}
#define SendInput measured_input
#ifdef BASELINE
#include "../build/baseline.c"
#else
#include "../clicker.c"
#endif
#undef SendInput

static HANDLE begin_worker(int rate, int right, int jitter, int basic_timer) {
    HANDLE thread;
    ResetEvent(first_click);
    sample_count = invalid_batch = fail_input = stall_input = 0;
#ifdef BASELINE
    (void)basic_timer;
    g_running = 1;
    g_click_button = rate ? (right ? CLICK_RIGHT : CLICK_LEFT) : CLICK_NONE;
    g_cps = rate ? rate : 1000;
    g_jitter_on = jitter;
    g_si = 1;
    g_wake_event = CreateEventW(0, FALSE, FALSE, 0);
    thread = CreateThread(0, 0, clicker_thread_proc, 0, 0, 0);
#else
    g_config = (rate ? rate : 1000) | SEND_INPUT | (rate ? (right ? RIGHT : LEFT) : 0) | (jitter ? JITTER : 0);
    g_wake = CreateEventW(0, FALSE, FALSE, 0);
    g_timer = basic_timer ? 0 : CreateWaitableTimerExW(0, 0, 2, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!g_timer) g_timer = CreateWaitableTimerW(0, FALSE, 0);
    thread = CreateThread(0, 0, click_thread, 0, 0, 0);
#endif
    if (!thread) { fprintf(stderr, "worker creation failed\n"); exit(1); }
    return thread;
}

static double finish_worker(HANDLE thread) {
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
#ifdef BASELINE
    InterlockedExchange(&g_running, 0);
    SetEvent(g_wake_event);
#else
    change_config(QUIT | BUTTON_MASK, QUIT);
#endif
    if (WaitForSingleObject(thread, 2000) != WAIT_OBJECT_0) {
        fprintf(stderr, "worker did not stop within 2 seconds\n"); exit(1);
    }
    QueryPerformanceCounter(&end);
#ifdef BASELINE
    CloseHandle(g_wake_event);
#else
    CloseHandle(g_wake); CloseHandle(g_timer);
#endif
    return (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart;
}

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void measure(const char *name, int rate, int right, int jitter, int basic) {
    static double gaps[40000];
    LARGE_INTEGER start, end, frequency;
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER k, u;
    ULONG64 cycles = 0;
    double stop_ms, elapsed, cpu, mean_cps = 0;
    HANDLE thread;
    int i, count;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    thread = begin_worker(rate, right, jitter, basic);
    Sleep(test_duration);
    stop_ms = finish_worker(thread);
    QueryPerformanceCounter(&end);
    GetThreadTimes(thread, &created, &exited, &kernel, &user);
    QueryThreadCycleTime(thread, &cycles);
    CloseHandle(thread);
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    elapsed = (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
    cpu = (double)(k.QuadPart + u.QuadPart) / 10000.0;
    count = (int)sample_count;
    if (invalid_batch || count >= 40000 || (!rate && count)) { fprintf(stderr, "invalid input stream\n"); exit(1); }
    for (i = 1; i < count; ++i) gaps[i - 1] = (double)(samples[i] - samples[i - 1]) * 1000.0 / (double)frequency.QuadPart;
    if (count > 1) {
        mean_cps = (double)(count - 1) * (double)frequency.QuadPart / (double)(samples[count - 1] - samples[0]);
        qsort(gaps, (size_t)(count - 1), sizeof(double), compare_double);
    }
    printf("{\"case\":\"%s\",\"target_cps\":%d,\"clicks\":%d,\"observed_cps\":%.3f,\"worker_cycles\":%llu,\"cycles_per_click\":%.2f,\"worker_cpu_ms\":%.3f,\"one_core_percent\":%.4f,\"gap_p50_ms\":%.4f,\"gap_p99_ms\":%.4f,\"stop_ms\":%.4f}\n",
           name, rate, count, mean_cps, cycles, count ? (double)cycles / count : 0, cpu, cpu / (elapsed * 10.0), count > 1 ? gaps[(count - 2) / 2] : 0,
           count > 1 ? gaps[(count - 1) * 99 / 100] : 0, stop_ms);
    fflush(stdout);
}

int main(int argc, char **argv) {
    (void)argv;
    first_click = CreateEventW(0, TRUE, FALSE, 0);
    if (argc > 1) {
        test_duration = 15000;
        measure("long_1000_cps", 1000, 0, 0, 0);
        CloseHandle(first_click);
        return 0;
    }
    measure("idle", 0, 0, 0, 0);
    measure("1_cps", 1, 0, 0, 0);
    measure("60_cps", 60, 0, 0, 0);
    measure("333_cps", 333, 0, 0, 0);
    measure("1000_cps", 1000, 0, 0, 0);
    measure("right_jitter_1000", 1000, 1, 1, 0);
#ifndef BASELINE
    measure("basic_timer_60", 60, 0, 0, 1);
    {
        HANDLE thread = begin_worker(1000, 0, 0, 0);
        LONG stopped;
        WaitForSingleObject(first_click, 1000);
        InterlockedExchange(&fail_input, 1);
        Sleep(100);
        if (config() & BUTTON_MASK) { fprintf(stderr, "failed input did not pause\n"); return 1; }
        stopped = sample_count; Sleep(100);
        if (sample_count != stopped) { fprintf(stderr, "input continued after failure\n"); return 1; }
        finish_worker(thread); CloseHandle(thread);
    }
    {
        HANDLE thread = begin_worker(1, 0, 0, 0);
        WaitForSingleObject(first_click, 1000);
        Sleep(20);
        if (finish_worker(thread) > 100) { fprintf(stderr, "1 CPS cancellation was slow\n"); return 1; }
        CloseHandle(thread);
    }
    printf("{\"checks\":\"input batching, jitter range, failure pause, long-wait cancellation passed\"}\n");
#endif
    CloseHandle(first_click);
    return 0;
}
