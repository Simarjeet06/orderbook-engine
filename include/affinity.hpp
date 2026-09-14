#pragma once
// Best-effort thread placement. Linux gives hard core pinning via
// sched_setaffinity; macOS only exposes an affinity *hint* (a tag the
// scheduler may use to co-locate threads sharing an L2 cache) via
// thread_policy_set -- it will not pin a thread to a specific core index.
// Real deployment target for this kind of system is Linux; this exists so
// the code at least runs and does something sane on a dev laptop.
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

inline void set_affinity_hint(int core_or_tag) {
#if defined(__APPLE__)
    thread_affinity_policy_data_t policy = {core_or_tag};
    thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());
    thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                       reinterpret_cast<thread_policy_t>(&policy),
                       THREAD_AFFINITY_POLICY_COUNT);
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_or_tag, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)core_or_tag;
#endif
}
