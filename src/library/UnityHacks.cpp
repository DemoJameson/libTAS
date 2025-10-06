/*
    Copyright 2015-2024 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "UnityHacks.h"
#include "logging.h"
#include "frame.h"
#include "GlobalState.h"
#include "global.h"
#include "hookpatch.h"
#ifdef __unix__
#include "checkpoint/ProcSelfMaps.h"
#elif defined(__APPLE__) && defined(__MACH__)
#include "checkpoint/MachVmMaps.h"
#endif
#include "checkpoint/MemArea.h"
#include "checkpoint/ThreadManager.h"
#include "checkpoint/ThreadInfo.h"
#include "../shared/unity_funcs.h"

#include <unistd.h>
#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <condition_variable>
#include <sys/mman.h> // PROT_READ, PROT_WRITE, etc.

namespace libtas {

static bool unity = false;
static uintptr_t executableBase = 0;
static uintptr_t executableEnd = 0;
static uintptr_t loadingThreadAddr = 0;

UnityHacks::ScrollingBuffer::ScrollingBuffer(int max_size = 400) {
    MaxSize = max_size;
    Offset  = 0;
    DataX.reserve(MaxSize);
    DataY.reserve(MaxSize);
}

void UnityHacks::ScrollingBuffer::AddPoint(int x, int y) {
    if (DataX.size() < MaxSize) {
        DataX.push_back(x);
        DataY.push_back(y);
    }
    else {
        DataX[Offset] = x;
        DataY[Offset] = y;
        Offset =  (Offset + 1) % MaxSize;
    }
}
void UnityHacks::ScrollingBuffer::Erase() {
    if (DataX.size() > 0) {
        DataX.clear();
        DataY.clear();
        Offset  = 0;
    }
}

UnityHacks::ScrollingBuffers::ScrollingBuffers() {
    /* Push back the total count */
    Buffers[0] = ScrollingBuffer();
    Buffers[0].name = "Total";
}

void UnityHacks::ScrollingBuffers::AddPoint(float x, float y, int tid) {
    bool new_buffer = (Buffers.find(tid) == Buffers.end());
    Buffers[tid].AddPoint(x, y);
    if (new_buffer) {
        for (ThreadInfo* th = ThreadManager::getThreadList(); th != nullptr; th = th->next) {
            if (th->translated_tid == tid) {
                if (th->state == ThreadInfo::ST_CKPNTHREAD)
                    Buffers[tid].name = "Main";
                else
                    Buffers[tid].name = th->name;
                break;
            }
        }
    }
}

static UnityHacks::ScrollingBuffers jobData;

const UnityHacks::ScrollingBuffers& UnityHacks::getJobData()
{
    return jobData;
}

void UnityHacks::setUnity()
{
    if (!unity) {
        LOG(LL_DEBUG, LCF_HOOK, "   detected Unity engine");
        unity = true;
    }
}

bool UnityHacks::isUnity()
{
    return unity;
}

static std::mutex mutex;

void UnityHacks::getExecutableMemory()
{
    /* This function might be called by multiple threads */
    std::lock_guard<std::mutex> lock(mutex);
    
    if (executableBase != 0)
        return;
    
#ifdef __unix__
    ProcSelfMaps memMapLayout;
#elif defined(__APPLE__) && defined(__MACH__)
    MachVmMaps memMapLayout;
#endif
    Area area;
    while (memMapLayout.getNextArea(&area)) {
        if (area.addr == (void*)0x400000 || area.addr == (void*)0x8048000)
            break;
    }
    
    if (!area.addr) {
        /* Try again */
        memMapLayout.reset();
        while (memMapLayout.getNextArea(&area)) {
            if ((area.flags & Area::AREA_FILE) && (area.flags & Area::AREA_PRIV) && (area.prot == PROT_READ))
                break;
        }
        
        LOG(LL_DEBUG, LCF_WAIT, "Game executable has non-default mapping! We found this:");
        LOG(LL_DEBUG, LCF_WAIT, "Region %p-%p (%s) with size %zu", area.addr, area.endAddr, area.name, area.size);
    }

    executableBase = reinterpret_cast<uintptr_t>(area.addr);
    executableEnd = reinterpret_cast<uintptr_t>(area.endAddr);
}

bool UnityHacks::isLoadingThread(uintptr_t addr)
{
    if (!unity) return false;
    
    if (loadingThreadAddr != 0)
        return (loadingThreadAddr == addr);

    /* The first Unity thread that executes a routine from its executable seems
     * to always be a loading thread */
    
    getExecutableMemory();

    if ((addr >= executableBase) && (addr < executableEnd)) {
        loadingThreadAddr = addr;
        return true;
    }
    
    return false;
}

static std::mutex unity_mutex;
static std::condition_variable unity_condition;
static unsigned int unity_running_threads = 0;
static unsigned int unity_nonterminating_threads = 0;
static unsigned int unity_waiting_threads = 0;
static unsigned int unity_job_count = 0;

void UnityHacks::syncNotify()
{
    static thread_local bool first = true;
    if (!first) {
        GlobalNative gn;
        std::lock_guard<decltype(unity_mutex)> lock(unity_mutex);
        LOG(LL_DEBUG, LCF_HACKS, "   Notifty the end of Unity job");
        --unity_running_threads;
        /* TODO: a nonterminating thread that eventually finishes is not supported. */
        unity_condition.notify_all();
    }
    else
        first = false;
}

void UnityHacks::syncWait()
{
    GlobalNative gn;
    std::unique_lock<decltype(unity_mutex)> lock(unity_mutex);
    ++unity_waiting_threads;
    ++unity_job_count;
    ThreadInfo* th = ThreadManager::getCurrentThread();
    th->unityJobCount++;
    LOG(LL_DEBUG, LCF_HACKS, "   Wait before running a Unity job");
    while (unity_running_threads > unity_nonterminating_threads) {
        
        if (Global::is_exiting)
            return;
        
        /* Store the current nonterminating count, so that only one thread that
         * timeouts can increase it */
        unsigned int old_unity_nonterminating_threads = unity_nonterminating_threads;
        
        if (std::cv_status::timeout == unity_condition.wait_for(lock, std::chrono::milliseconds(2000))) {

            /* The current running thread did not finish in time. We consider
            * it as running a nonterminating job, and we increase its count
            * so that one other thread can now run its job. We take extra
            * precaution that exactly one thread will raise the count here
            * by comparing with the old count, to see if another thread did
            * increase it already.
            * TODO: For now, the count can never decrease */

            if ((unity_running_threads > unity_nonterminating_threads) &&
                (unity_nonterminating_threads == old_unity_nonterminating_threads)) {                
                LOG(LL_DEBUG, LCF_HACKS, "   Increase Unity nonterminating thread count");
                unity_nonterminating_threads++;
            }
        }
    }
    LOG(LL_DEBUG, LCF_HACKS, "   Start running a Unity job");
    --unity_waiting_threads;
    ++unity_running_threads;
}

void UnityHacks::syncWaitAll()
{
    // GlobalNative gn;
    
    /* We would normally use the same conditional variable to wait here that
     * all jobs have finished, but it can cause a softlock.
     * It seems the reason is that other threads can send a signal (SIGXCPU) to
     * main thread to trigger a sigsuspend() while it is waiting on conditional
     * variable, and the same thread is responsible for sending a SIGTSTP signal
     * to resume execution. But this thread is waiting for another thread to
     * finish its job, so it never resumes main thread. And the thread that
     * currrently runs the job can never finish, because the conditional
     * variable is locked. This leads to a deadlock.
     
     * Main Thread (that is interrupted from waiting on conditional variable)
     * #0  0x00007ffff797c281 in __GI___sigsuspend (set=0x7fffffffc8f0) at ../sysdeps/unix/sysv/linux/sigsuspend.c:26
     * #1  0x00007ffff7edaae2 in libtas::sigsuspend (set=0x7fffffffc8f0) at general/signalwrappers.cpp:182
     * #2  0x00007fffed1acaf9 in ?? () from /home/clement/Games/Dandara/Dandara_Data/Mono/x86_64/libmono.so
     * #3  <signal handler called> (SIGXCPU)
     * #4  0x00007ffff79c5da4 in __futex_abstimed_wait_common64 (private=0, cancel=true, abstime=0x0, op=393, expected=0, futex_word=0x7ffff7fb12a8 <libtas::unity_condition+40>) at ./nptl/futex-internal.c:57
     * #5  __futex_abstimed_wait_common (futex_word=futex_word@entry=0x7ffff7fb12a8 <libtas::unity_condition+40>, expected=expected@entry=0, clockid=clockid@entry=0, abstime=abstime@entry=0x0, private=private@entry=0, cancel=cancel@entry=true) at ./nptl/futex-internal.c:87
     * #6  0x00007ffff79c5e0b in __GI___futex_abstimed_wait_cancelable64 (futex_word=futex_word@entry=0x7ffff7fb12a8 <libtas::unity_condition+40>, expected=expected@entry=0, clockid=clockid@entry=0, abstime=abstime@entry=0x0, private=private@entry=0) at ./nptl/futex-internal.c:139
     * #7  0x00007ffff79c8468 in __pthread_cond_wait_common (abstime=0x0, clockid=0, mutex=0x7ffff7fb12c0 <libtas::unity_mutex>, cond=0x7ffff7fb1280 <libtas::unity_condition>) at ./nptl/pthread_cond_wait.c:503
     * #8  ___pthread_cond_wait (cond=0x7ffff7fb1280 <libtas::unity_condition>, mutex=0x7ffff7fb12c0 <libtas::unity_mutex>) at ./nptl/pthread_cond_wait.c:618
     * #9  0x00007ffff7ea93ad in libtas::UnityHacks::syncWaitAll () at GameHacks.cpp:164
     * #10 0x00007ffff7ea8230 in libtas::frameBoundary(std::function<void ()>, libtas::RenderHUD&) (draw=..., hud=...) at frame.cpp:265
     * #11 0x00007ffff7f0f76d in libtas::SDL_GL_SwapWindow (window=<optimized out>) at sdl/sdlwindows.cpp:102
     * ...
     *
     * Thread 2 (that sent the SIGXCPU signal and is waiting for another job to finish)
     * #0  __futex_abstimed_wait_common64 (private=0, cancel=true, abstime=0x7fffecefce70, op=137, expected=0, futex_word=0x7ffff7fb12ac <libtas::unity_condition+44>) at ./nptl/futex-internal.c:57
     * #1  __futex_abstimed_wait_common (futex_word=futex_word@entry=0x7ffff7fb12ac <libtas::unity_condition+44>, expected=expected@entry=0, clockid=clockid@entry=1, abstime=abstime@entry=0x7fffecefce70, private=private@entry=0, cancel=cancel@entry=true) at ./nptl/futex-internal.c:87
     * #2  0x00007ffff79c5e0b in __GI___futex_abstimed_wait_cancelable64 (futex_word=futex_word@entry=0x7ffff7fb12ac <libtas::unity_condition+44>, expected=expected@entry=0, clockid=clockid@entry=1, abstime=abstime@entry=0x7fffecefce70, private=private@entry=0) at ./nptl/futex-internal.c:139
     * #3  0x00007ffff79c8a3f in __pthread_cond_wait_common (abstime=<optimized out>, clockid=1, mutex=0x7ffff7fb12c0 <libtas::unity_mutex>, cond=0x7ffff7fb1280 <libtas::unity_condition>) at ./nptl/pthread_cond_wait.c:503
     * #4  ___pthread_cond_clockwait64 (abstime=<optimized out>, clockid=1, mutex=0x7ffff7fb12c0 <libtas::unity_mutex>, cond=0x7ffff7fb1280 <libtas::unity_condition>) at ./nptl/pthread_cond_wait.c:682
     * #5  ___pthread_cond_clockwait64 (cond=0x7ffff7fb1280 <libtas::unity_condition>, mutex=0x7ffff7fb12c0 <libtas::unity_mutex>, clockid=1, abstime=<optimized out>) at ./nptl/pthread_cond_wait.c:670
     * #6  0x00007ffff7ea927c in std::__condvar::wait_until (__abs_time=..., __clock=1, __m=..., this=0x7ffff7fb1280 <libtas::unity_condition>) at /usr/include/c++/12/bits/std_mutex.h:169
     * #7  std::condition_variable::__wait_until_impl<std::chrono::duration<long, std::ratio<1l, 1000000000l> > > (__lock=<synthetic pointer>..., __atime=..., this=0x7ffff7fb1280 <libtas::unity_condition>) at /usr/include/c++/12/condition_variable:200
     * #8  std::condition_variable::wait_until<std::chrono::duration<long, std::ratio<1l, 1000000000l> > > (__atime=..., __lock=<synthetic pointer>..., this=0x7ffff7fb1280 <libtas::unity_condition>) at /usr/include/c++/12/condition_variable:110
     * #9  std::condition_variable::wait_for<long, std::ratio<1l, 1000l> > (__rtime=..., __lock=<synthetic pointer>..., this=0x7ffff7fb1280 <libtas::unity_condition>) at /usr/include/c++/12/condition_variable:162
     * #10 libtas::UnityHacks::syncWait () at GameHacks.cpp:136
     * #11 0x00007ffff7ed9044 in libtas::sem_wait (sem=<optimized out>) at general/pthreadwrappers.cpp:652
     * ...
     *
     * Thread 3 (that has finished its job and tries to signal the conditional variable)
     * #0  futex_wait (private=0, expected=3, futex_word=0x7ffff7fb1290 <libtas::unity_condition+16>) at ../sysdeps/nptl/futex-internal.h:146
     * #1  futex_wait_simple (private=0, expected=3, futex_word=0x7ffff7fb1290 <libtas::unity_condition+16>) at ../sysdeps/nptl/futex-internal.h:177
     * #2  __condvar_quiesce_and_switch_g1 (private=0, g1index=<synthetic pointer>, wseq=<optimized out>, cond=0x7ffff7fb1280 <libtas::unity_condition>) at ./nptl/pthread_cond_common.c:276
     * #3  ___pthread_cond_broadcast (cond=0x7ffff7fb1280 <libtas::unity_condition>) at ./nptl/pthread_cond_broadcast.c:72
     * #4  0x00007ffff7ea9162 in libtas::UnityHacks::syncNotify () at GameHacks.cpp:118
     * #5  0x00007ffff7ed9034 in libtas::sem_wait (sem=0x7ffff58f8c00) at general/pthreadwrappers.cpp:648
     * ...
     */
    
    // unsigned int old_job_count = 0;
    // unity_mutex.lock();
    // int sleep_length = 200;
    // 
    // if (unity_job_count > old_job_count)
    //     LOG(LL_DEBUG, LCF_HACKS, "   Wait that all Unity jobs finish");
    // 
    // while (unity_job_count > old_job_count) {
    //     old_job_count = unity_job_count;
    //     unity_mutex.unlock();
    // 
    //     if (Global::is_exiting)
    //         return;
    // 
    //     NATIVECALL(usleep(sleep_length));
    //     unity_mutex.lock();
    //     while (unity_waiting_threads || (unity_running_threads > unity_nonterminating_threads)) {
    //         unity_mutex.unlock();
    //         NATIVECALL(usleep(100));
    //         unity_mutex.lock();
    //     }
    // }

    /* Register and reset counts */
    jobData.AddPoint(framecount, unity_job_count, 0);
    unity_job_count = 0;
    
    for (ThreadInfo* th = ThreadManager::getThreadList(); th != nullptr; th = th->next) {
        if (th->unityJobCount) {
            jobData.AddPoint(framecount, th->unityJobCount, th->translated_tid);
            th->unityJobCount = 0;
        }
    }

    // unity_mutex.unlock();
}

void UnityHacks::waitFromName(pthread_t target_thread, const char *name)
{
    // if ((strcmp(name, "Loading.Preload") == 0) ||
    //     (strcmp(name, "Loading.AsyncRe") == 0) ||
    //     (strcmp(name, "Background Job.") == 0) ||
    //     (strncmp(name, "Job.Worker ", 11) == 0))
    // {
    //     ThreadInfo* thread = ThreadManager::getThread(target_thread);
    //     thread->unityThread = true;
    // }
}

typedef void ujob_control_t;
typedef void ujob_lane_t;
typedef void ujob_job_t;
typedef long ujob_handle_t;
typedef void ujob_dependency_chain;
typedef void WorkStealingRange;
typedef void JobsCallbackFunctions;
typedef void ScriptingBackendNativeObjectPtrOpaque;

// class JobsCallbackFunctions;
// typedef void* JobsCallbackFunctions;
class JobScheduleParameters;
class JobFence;

class JobQueue;
class BackgroundJobQueue;
typedef int JobQueue_JobQueuePriority;
typedef char JobQueue_JobQueueWorkStealMode;
typedef int MemLabelId;
typedef void JobGroup;
typedef void JobQueue_ThreadInfo;
typedef long BackgroundJobQueue_JobFence;
typedef void JobInfo;
typedef void JobBatchHandles;

struct JobGroupID {
    JobGroup* group;
    int tag;
};

namespace orig {
    unsigned int (*futexWait)(int* x, int y, unsigned int z);
    void (*ujob_execute_job)(ujob_control_t*, ujob_lane_t*, ujob_job_t*, ujob_handle_t, unsigned int);
    unsigned long (*ujob_schedule_job_internal)(ujob_control_t* x, ujob_handle_t y, unsigned int z);
    long (*ujob_schedule_parallel_for_internal)(ujob_control_t* x, JobsCallbackFunctions* y, void* z, WorkStealingRange* a, unsigned int b, unsigned int c, ujob_handle_t const* d, int e, unsigned char f);
    void (*ujobs_add_to_lane_and_wake_one_thread)(ujob_control_t*, ujob_job_t*, ujob_lane_t*);
    void (*ujob_participate)(ujob_control_t* x, ujob_handle_t y, ujob_job_t* z, int* a, ujob_dependency_chain const* b);
    int (*job_completed)(ujob_control_t* x, ujob_lane_t* y, ujob_job_t* z, ujob_handle_t a);
    int (*JobsUtility_CUSTOM_CreateJobReflectionData)(ScriptingBackendNativeObjectPtrOpaque* x, ScriptingBackendNativeObjectPtrOpaque* y, ScriptingBackendNativeObjectPtrOpaque* z, ScriptingBackendNativeObjectPtrOpaque* a, ScriptingBackendNativeObjectPtrOpaque* b);
    int (*JobsUtility_CUSTOM_Schedule)(JobScheduleParameters& x, JobFence& y);
    long (*ScheduleBatchJob)(void* x, ujob_handle_t y);
    void (*JobQueue6_ScheduleGroups)(JobQueue *t, JobBatchHandles* x, int y);
    void (*worker_thread_routine)(void* x);
    void (*JobQueue_ScheduleJob)(JobQueue *t, void (*func)(void*), void* arg, JobGroup* z, int a, int b);
    void (*JobQueue_CompleteAllJobs)(JobQueue *t);
    long (*JobQueue_ScheduleJobMultipleDependencies)(JobQueue *t, void (*x)(void*), void* y, JobGroupID* z, int a, MemLabelId b);
    JobGroup* (*JobQueue_CreateJobBatch)(JobQueue *t, void (*func)(void*), void* arg, JobGroup* z, int id, JobGroup* a);
    void (*JobQueue_ScheduleGroups)(JobQueue *t, JobGroup* x, JobGroup* y);
    void (*JobQueue_WaitForJobGroupID)(JobQueue *t, JobGroup *x, int y, bool a);
    bool (*JobQueue_ExecuteOneJob)(JobQueue *t);
    long* (*JobQueue_ScheduleGroup)(JobQueue *t, JobGroup* x, int y);
    JobGroupID (*JobQueue_ScheduleGroupInternal)(JobQueue *t, JobGroup* x, int y, bool z);
    long (*JobQueue_ProcessJobs)(JobQueue_ThreadInfo* x, void* y);
    long (*JobQueue_Exec)(JobQueue *t, JobInfo* x, long long y, int z, bool a);
    long (*JobQueue_ExecuteJobFromQueue)(JobQueue *t, bool x);
    void (*JobQueue_ScheduleDependencies)(JobQueue *t, JobGroupID *x, JobInfo *y, JobInfo *z, bool a);
    void (*BackgroundJobQueue_ScheduleJobInternal)(BackgroundJobQueue *t, void (*x)(void*), void* y, BackgroundJobQueue_JobFence* z, JobQueue_JobQueuePriority a);
    void (*BackgroundJobQueue_ScheduleMainThreadJobInternal)(BackgroundJobQueue *t, void (*x)(void*), void* y);
    void (*BackgroundJobQueue_ExecuteMainThreadJobs)(BackgroundJobQueue *t);
}

static int futexWait(int* x, int y, unsigned int z)
{
    ThreadInfo* thread = ThreadManager::getCurrentThread();
    LOGTRACE(LCF_HACKS);
    if (thread->unityThread) {
        UnityHacks::syncNotify();

        int ret = orig::futexWait(x, y, z);

        UnityHacks::syncWait();
        return ret;
    }

    return orig::futexWait(x, y, z);
}

// #include <signal.h>

/* Unity 2020 - 2021 */

static void JobQueue_ScheduleJob(JobQueue *t, void (*func)(void*), void* arg, JobGroup* z, int a, int b)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_ScheduleJob called with func %p, arg %p, JobGroup %p, JobGroup tag %d and priority %d", func, arg, z, a, b);
    return orig::JobQueue_ScheduleJob(t, func, arg, z, a, b);
}

static void JobQueue_CompleteAllJobs(JobQueue *t)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobQueue_CompleteAllJobs(t);
}

static long JobQueue_ScheduleJobMultipleDependencies(JobQueue *t, void (*x)(void*), void* y, JobGroupID* z, int a, MemLabelId b)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobQueue_ScheduleJobMultipleDependencies(t, x, y, z, a, b);
}

static JobGroup* JobQueue_CreateJobBatch(JobQueue *t, void (*func)(void*), void* arg, JobGroup* z, int id, JobGroup* a)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobQueue_CreateJobBatch(t, func, arg, z, id, a);
}

static void JobQueue_ScheduleGroups(JobQueue *t, JobGroup* x, JobGroup* y)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobQueue_ScheduleGroups(t, x, y);
}

static void JobQueue_WaitForJobGroupID(JobQueue *t, JobGroup *x, int y, bool z)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_WaitForJobGroupID called with JobGroup %p, JobGroup tag %d and steal mode %d", x, y, z);
    return orig::JobQueue_WaitForJobGroupID(t, x, y, z);
}

static bool JobQueue_ExecuteOneJob(JobQueue *t)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobQueue_ExecuteOneJob(t);
}

static long* JobQueue_ScheduleGroup(JobQueue *t, JobGroup* x, int y)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_ScheduleGroup called with JobGroup %p and priority %d", x, y);
    return orig::JobQueue_ScheduleGroup(t, x, y);
}

static JobGroupID JobQueue_ScheduleGroupInternal(JobQueue *t, JobGroup *x, int y, bool z)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_ScheduleGroupInternal called with JobGroup %p, priority %d and sync %d", x, y, z);

    /* Return value is 16 bytes (accross registers RDX:RAX), so we need to use
     * a 16-byte struct to recover it. */
    // JobGroupID j = orig::JobQueue_ScheduleGroupInternal(t, x, y, z);
    JobGroupID j = orig::JobQueue_ScheduleGroupInternal(t, x, y, true);
    LOG(LL_DEBUG, LCF_HACKS, "    returns JobGroup %p and JobGroup tag %d", j.group, j.tag);

    /* Immediatly wait for the job */
    // JobQueue_WaitForJobGroupID(t, j.group, j.tag, true);
    return j;
}

static void JobQueue_ScheduleDependencies(JobQueue *t, JobGroupID *x, JobInfo *y, JobInfo *z, bool a)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_ScheduleDependencies called with sync %d", a);
    // return orig::JobQueue_ScheduleDependencies(t, x, y, z, a);
    return orig::JobQueue_ScheduleDependencies(t, x, y, z, true);
}

static long JobQueue_ProcessJobs(JobQueue_ThreadInfo* x, void* y)
{
    LOGTRACE(LCF_HACKS);
    ThreadInfo* thread = ThreadManager::getCurrentThread();
    thread->unityThread = true;

    return orig::JobQueue_ProcessJobs(x, y);
}

static long JobQueue_Exec(JobQueue *t, JobInfo* x, long long y, int z, bool a)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_Exec called with JobInfo %p and sync %d", x, a);
    long executed = orig::JobQueue_Exec(t, x, y, z, a);
    if (executed) {
        ++unity_job_count;
        ThreadInfo* th = ThreadManager::getCurrentThread();
        th->unityJobCount++;
    }
    return executed;
}

static long JobQueue_ExecuteJobFromQueue(JobQueue *t, bool x)
{
    LOG(LL_TRACE, LCF_HACKS, "JobQueue_ExecuteJobFromQueue called with sync %d", x);
    return 0;
    // return orig::JobQueue_ExecuteJobFromQueue(t, x);
}

static void BackgroundJobQueue_ScheduleJobInternal(BackgroundJobQueue *t, void (*x)(void*), void* y, BackgroundJobQueue_JobFence* z, JobQueue_JobQueuePriority a)
{
    LOGTRACE(LCF_HACKS);
    return orig::BackgroundJobQueue_ScheduleJobInternal(t, x, y, z, a);
}

static void BackgroundJobQueue_ScheduleMainThreadJobInternal(BackgroundJobQueue *t, void (*x)(void*), void* y)
{
    LOGTRACE(LCF_HACKS);
    return orig::BackgroundJobQueue_ScheduleMainThreadJobInternal(t, x, y);
}

static void BackgroundJobQueue_ExecuteMainThreadJobs(BackgroundJobQueue *t)
{
    LOGTRACE(LCF_HACKS);
    return orig::BackgroundJobQueue_ExecuteMainThreadJobs(t);
}

/* Unity 6 */

static void ujob_execute_job(ujob_control_t* x, ujob_lane_t* y, ujob_job_t* z, ujob_handle_t a, unsigned int b)
{
    LOGTRACE(LCF_HACKS);
    ++unity_job_count;
    ThreadInfo* th = ThreadManager::getCurrentThread();
    th->unityJobCount++;
    return orig::ujob_execute_job(x, y, z, a, b);
}

static unsigned long ujob_schedule_job_internal(ujob_control_t* x, ujob_handle_t y, unsigned int z)
{
    LOGTRACE(LCF_HACKS);
    return orig::ujob_schedule_job_internal(x, y, z);
}

static long ujob_schedule_parallel_for_internal(ujob_control_t* x, JobsCallbackFunctions* y, void* z, WorkStealingRange* a, unsigned int b, unsigned int c, ujob_handle_t const* d, int e, unsigned char f)
{
    LOGTRACE(LCF_HACKS);
    return orig::ujob_schedule_parallel_for_internal(x, y, z, a, b, c, d, e, f);
}

// static std::mutex unity_job_mutex;
// static std::condition_variable unity_job_condition;
// static int running_jobs = 0;

static void ujobs_add_to_lane_and_wake_one_thread(ujob_control_t* x, ujob_job_t* y, ujob_lane_t* z)
{
    LOGTRACE(LCF_HACKS);
    // std::unique_lock<decltype(unity_mutex)> lock(unity_mutex);
    // int current_running_jobs = running_jobs;
    // running_jobs++;

    orig::ujobs_add_to_lane_and_wake_one_thread(x, y, z);

    // unity_job_condition.wait(lock, [current_running_jobs] {return (running_jobs == current_running_jobs);});
}

static void ujob_participate(ujob_control_t* x, ujob_handle_t y, ujob_job_t** z, int* a, ujob_dependency_chain const* b)
{
    LOGTRACE(LCF_HACKS);
    orig::ujob_participate(x, y, z, a, b);
}

static int job_completed(ujob_control_t* x, ujob_lane_t* y, ujob_job_t* z, ujob_handle_t a)
{
    LOGTRACE(LCF_HACKS);
    int ret = orig::job_completed(x, y, z, a);
    
    // running_jobs--;
    // unity_job_condition.notify_all();
    return ret;
}

static int JobsUtility_CUSTOM_CreateJobReflectionData(ScriptingBackendNativeObjectPtrOpaque* x, ScriptingBackendNativeObjectPtrOpaque* y, ScriptingBackendNativeObjectPtrOpaque* z, ScriptingBackendNativeObjectPtrOpaque* a, ScriptingBackendNativeObjectPtrOpaque* b)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobsUtility_CUSTOM_CreateJobReflectionData(x, y, z, a, b);
}

static int JobsUtility_CUSTOM_Schedule(JobScheduleParameters& x, JobFence& y)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobsUtility_CUSTOM_Schedule(x, y);
}

static long ScheduleBatchJob(void* x, ujob_handle_t y)
{
    LOGTRACE(LCF_HACKS);
    return orig::ScheduleBatchJob(x, y);
}

static void JobQueue6_ScheduleGroups(JobQueue *t, JobBatchHandles* x, int y)
{
    LOGTRACE(LCF_HACKS);
    return orig::JobQueue6_ScheduleGroups(t, x, y);
}

static void worker_thread_routine(void* x)
{
    LOGTRACE(LCF_HACKS);
    ThreadInfo* thread = ThreadManager::getCurrentThread();
    thread->unityThread = true;
    return orig::worker_thread_routine(x);
}

#define FUNC_CASE(FUNC_ENUM, FUNC_SYMBOL) \
case FUNC_ENUM: \
    hook_patch_addr(reinterpret_cast<void*>(address), reinterpret_cast<void**>(&orig::FUNC_SYMBOL), reinterpret_cast<void*>(FUNC_SYMBOL)); \
    break;


void UnityHacks::patch(int func, uint64_t addr)
{
    setUnity();
    
    uintptr_t address = static_cast<uintptr_t>(addr);
    switch(func) {
        // FUNC_CASE(UNITY_FUTEX_WAIT, futexWait)
        FUNC_CASE(UNITY6_UJOB_EXECUTE, ujob_execute_job)
        FUNC_CASE(UNITY6_UJOB_SCHEDULE, ujob_schedule_job_internal)
        // FUNC_CASE(UNITY6_UJOB_SCHEDULE_PARALLEL, ujob_schedule_parallel_for_internal)
        FUNC_CASE(UNITY6_UJOB_ADD, ujobs_add_to_lane_and_wake_one_thread)
        FUNC_CASE(UNITY6_UJOB_PARTICIPATE, ujob_participate)
        FUNC_CASE(UNITY6_JOB_COMPLETED, job_completed)
        FUNC_CASE(UNITY6_JOB_REFLECTION, JobsUtility_CUSTOM_CreateJobReflectionData)
        FUNC_CASE(UNITY6_JOB_SCHEDULE, JobsUtility_CUSTOM_Schedule)
        FUNC_CASE(UNITY6_BATCH_JOB, ScheduleBatchJob)
        FUNC_CASE(UNITY6_JOBQUEUE_SCHEDULE_GROUPS, JobQueue6_ScheduleGroups)
        FUNC_CASE(UNITY6_WORKER_THREAD_ROUTINE, worker_thread_routine)
        FUNC_CASE(UNITY_JOBQUEUE_SCHEDULE_JOB, JobQueue_ScheduleJob)
        FUNC_CASE(UNITY_JOBQUEUE_COMPLETE_ALL_JOBS, JobQueue_CompleteAllJobs)
        FUNC_CASE(UNITY_JOBQUEUE_SCHEDULE_JOB_MULTIPLE, JobQueue_ScheduleJobMultipleDependencies)
        FUNC_CASE(UNITY_JOBQUEUE_CREATE_JOB_BATCH, JobQueue_CreateJobBatch)
        FUNC_CASE(UNITY_JOBQUEUE_SCHEDULE_GROUPS, JobQueue_ScheduleGroups)
        FUNC_CASE(UNITY_JOBQUEUE_WAIT_JOB_GROUP, JobQueue_WaitForJobGroupID)
        FUNC_CASE(UNITY_JOBQUEUE_EXECUTE, JobQueue_ExecuteOneJob)
        FUNC_CASE(UNITY_JOBQUEUE_SCHEDULE_GROUP, JobQueue_ScheduleGroup)
        FUNC_CASE(UNITY_JOBQUEUE_SCHEDULE_GROUP_INTERNAL, JobQueue_ScheduleGroupInternal)
        FUNC_CASE(UNITY_JOBQUEUE_PROCESS, JobQueue_ProcessJobs)
        FUNC_CASE(UNITY_JOBQUEUE_EXEC, JobQueue_Exec)
        FUNC_CASE(UNITY_JOBQUEUE_EXECUTE_QUEUE, JobQueue_ExecuteJobFromQueue)
        FUNC_CASE(UNITY_JOBQUEUE_SCHEDULE_DEPENDENCIES, JobQueue_ScheduleDependencies)
        FUNC_CASE(UNITY_BACKGROUND_JOBQUEUE_SCHEDULE, BackgroundJobQueue_ScheduleJobInternal)
        FUNC_CASE(UNITY_BACKGROUND_JOBQUEUE_SCHEDULE_MAIN, BackgroundJobQueue_ScheduleMainThreadJobInternal)
        FUNC_CASE(UNITY_BACKGROUND_JOBQUEUE_EXECUTE, BackgroundJobQueue_ExecuteMainThreadJobs)
    }
}

}
