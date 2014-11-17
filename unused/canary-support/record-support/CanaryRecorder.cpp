/*
 * Developed by Qingkai Shi
 * Copy Right by Prism Research Group, HKUST and State Key Lab for Novel Software Tech., Nanjing University.  
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <set>

#include <boost/unordered_map.hpp>

//#define DEBUG
//#define NO_TIME_CMD
//#define LDEBUG
//#define CACHE_ENABLED


#include "SignalRoutine.h"
#include "../CanaryThread.h"

/*
 * Define global log variables, each shared var has one
 */
static std::vector<g_llog_t *> llogs; // <g_llog_t *> size is not fixed, and should be determined at runtime
static g_flog_t flog;
static g_mlog_t mlog;

/*
 * write versions, each shared var has a version
 */
static unsigned* write_versions;

/*
 * number of shared/local variables
 */
static unsigned num_shared_vars = 0;
static unsigned num_local_vars = 0;

/*
 * locks for synchronization, each shared var has one
 */
static pthread_mutex_t* locks;
static pthread_mutex_t fork_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_init_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * mutex_t-> mutex_id hashmap
 * we must use mutex_id instead of mutex_ht.size(), because a mutex may be generated using the address of a destroyed one.
 */
static unsigned mutex_id = 0;
static boost::unordered_map<pthread_mutex_t *, unsigned> mutex_ht;

static boost::unordered_map<pthread_t, canary_thread_t*> thread_ht;

/*
 * set of active threads, not the original tid
 * 
 * static std::set<unsigned> active_threads;
 * 
 * cannot use it!!
 */

static bool start = false;
static bool forking = false;
static bool main_started = false;

#ifdef NO_TIME_CMD
static struct timeval tpstart, tpend;
#endif

static inline void lock(unsigned svId) {
    pthread_mutex_lock(&locks[svId]);
}

static inline void unlock(unsigned svId) {
    pthread_mutex_unlock(&locks[svId]);
}

static inline void forkLock() {
    pthread_mutex_lock(&fork_lock);
}

static inline void forkUnlock() {
    pthread_mutex_unlock(&fork_lock);
}

static inline void mutexInitLock() {
    pthread_mutex_lock(&mutex_init_lock);
}

static inline void mutexInitUnlock() {
    pthread_mutex_unlock(&mutex_init_lock);
}

extern "C" {

    void* OnMethodStart() {
        if (!main_started)
            return NULL;

        pthread_t self = pthread_self();
        bool contains = thread_ht.count(self);
        while (!contains && forking) {
            sched_yield();
            contains = thread_ht.count(self);
        }

        if (contains) {
            canary_thread_t* st = thread_ht[self];
            if (st->onexternal == 0)
                return st;
        }

        return NULL;
    }

    void OnStartTimer() {
#ifdef NO_TIME_CMD
        gettimeofday(&tpstart, NULL);
#endif
    }

    void OnInit(unsigned svsNum, unsigned lvsNum) {
        printf("[INFO] OnInit-Record (canary record)\n");
        num_shared_vars = svsNum;
        num_local_vars = lvsNum;
        initializeSigRoutine();

        write_versions = new unsigned[svsNum];
        memset(write_versions, 0, sizeof (unsigned) * svsNum);
        locks = new pthread_mutex_t[svsNum];

        for (unsigned i = 0; i < svsNum; i++) {
            pthread_mutex_init(&locks[i], NULL);
        }

        // main thread.
        canary_thread_t* st = new canary_thread_t(num_shared_vars, num_local_vars, thread_ht.size());
        thread_ht[pthread_self()] = st;

        main_started = true;
        start = false;
    }

    void OnExit() {
#ifdef NO_TIME_CMD
        gettimeofday(&tpend, NULL);
        double timeuse = 1000000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec;
        timeuse /= 1000;
        printf("[INFO] Wall time is %lf ms\n", timeuse);
#endif
        start = false;

        printf("[INFO] OnExit-Record (canary record)\n");

        // dump, delete/free is not needed, because program will exit.
        {//flog
#ifdef DEBUG
            printf("dumping flog...\n");
#endif
            FILE * fout = fopen("fork.dat", "w");
            flog.dump(fout);
            fclose(fout);
        }

        {//mlog, llogs
#ifdef DEBUG
            printf("dumping mlog, llog...\n");
#endif
            FILE * fout = fopen("mutex.dat", "w");
            mlog.dump(fout);

            unsigned size = llogs.size();
#ifdef LDEBUG
            fprintf(fout, "Size: %u \n", size);
#else
            fwrite(&size, sizeof (unsigned), 1, fout);
#endif
            for (unsigned i = 0; i < size; i++) {
                g_llog_t * llog = llogs[i];
                llog->dump(fout);
            }
            fclose(fout);
        }
        {
#ifdef DEBUG
            printf("dumping thread local log...\n");
#endif      
            FILE * file_read = fopen("read.dat", "w");
            FILE * file_lread = fopen("lread.dat", "w");
            FILE * file_write = fopen("write.dat", "w");
            FILE * file_add = fopen("addressmap.dat", "w");

            boost::unordered_map<pthread_t, canary_thread_t*>::iterator lit = thread_ht.begin();
            while (lit != thread_ht.end()) {
                canary_thread_t* st = lit->second;
                st->dump(file_read, file_lread, file_write, file_add);

                delete st;

                lit++;
            }

            fclose(file_read);
            fclose(file_lread);
            fclose(file_write);
            fclose(file_add);
        }

        printf("[INFO] Threads num: %d\n", thread_ht.size());
        thread_ht.clear();

        // zip
        system("if [ -f canary.zip ]; then rm canary.zip; fi");
        system("zip -9 canary.zip ./fork.dat ./mutex.dat ./addressmap.dat ./write.dat ./read.dat ./lread.dat");
        system("rm -f ./fork.dat ./mutex.dat ./addressmap.dat ./write.dat ./read.dat ./lread.dat");
        system("echo -n \"[INFO] Log size (Byte): \"; echo -n `du -sb canary.zip` | cut -d\" \" -f 1;");
    }

    void OnPreExternalCall(void* st) {
        if (st != NULL)
            ((canary_thread_t*) st)->onexternal++;
    }

    void OnExternalCall(long value, int lvid, void* st) {
        if (st == NULL)
            return;
        unsigned oe = ((canary_thread_t*) st)->onexternal;
        ((canary_thread_t*) st)->onexternal = oe - 1;

        if (oe == 1 && lvid != -1) {
            ((canary_thread_t*) st)->lrlog[lvid].logValue(value);
        }
    }

    void OnAddressInit(void* value, size_t size, size_t n, int type, void* st) {
        if (st == NULL) {
            return;
        }

#ifdef DEBUG
        printf("OnAddressInit\n");
#endif

        l_addmap_t * mlog = ((canary_thread_t*) st)->addlog;

        if (type == 1000) { // a stack address
            if (mlog->stack_tag) {
                return;
            } else {
                mlog->stack_tag = true;
            }
        }

        mem_t m;
        m.address = value;
        m.range = size*n;
        m.type = type;
        mlog->adds.push_back(m);
    }

    void OnLoad(int svId, int lvId, long value, void* address, void* st, int debug) {
        if (st == NULL)
            return;

        bool success = false;

#ifdef CACHE_ENABLED
        success = ((canary_thread_t*) st)->cache->query(address, value);
        if (!success)
            ((canary_thread_t*) st)->cache->add(address, value);
#endif

        if ((!start && lvId != -1) || (start && svId == -1)) {
            if (!success)
                ((canary_thread_t*) st)->lrlog[lvId].logValue(value);
        } else {
            if (!success)
                ((canary_thread_t*) st)->rlog[svId].VAL_LOG.logValue(value);
            ((canary_thread_t*) st)->rlog[svId].VER_LOG.logValue(write_versions[svId]);
        }

#ifdef DEBUG
        printf("OnLoad === 1\n");
#endif
    }

    unsigned OnPreStore(int svId, void* st, int debug) {
        if (!start || st == NULL) {
            return 0;
        }

        lock(svId);
#ifdef DEBUG
        printf("OnPreStore: %d [%d]\n", svId, debug);
#endif
        unsigned version = write_versions[svId];
        write_versions[svId] = version + 1;

        return version;
    }

    void OnStore(int svId, unsigned version, long value, void* address, void* st, int debug) {
        if (!start || st == NULL) {
            return;
        }
#ifdef DEBUG
        printf("OnStore\n");
#endif
        unlock(svId);
        ((canary_thread_t*) st)->wlog[svId].logValue(version);

#ifdef CACHE_ENABLED
        ((canary_thread_t*) st)->cache->add(address, value);
#endif
    }

    void OnLock(pthread_mutex_t* mutex_ptr, void* st) {
        if (!start || st == NULL) {
            return;
        }

        // if map does not contain it, it must be a bug
        // because lock and init have a race.
        if (!mutex_ht.count(mutex_ptr)) {
            fprintf(stderr, "[ERROR] Program bug- mutex is not initialized before lock()!\n");
            OnExit();
            exit(1);
        }

        // here, the same address mutex_ptr is impossible to init a new lock
        // because the lock has been locked
        // therefore mutex_ht[mutex_ptr] is safe
        g_llog_t* llog = llogs[mutex_ht[mutex_ptr]];
        llog->logValue(((canary_thread_t*) st)->tid); // exchange to _tid at last

#ifdef DEBUG
        printf("OnLock --> t%d\n", thread_ht[pthread_self()]);
#endif
    }

    void OnWait(pthread_cond_t* cond_ptr, pthread_mutex_t* mutex_ptr, void* st) {
        if (!start || st == NULL) {
            return;
        }
#ifdef DEBUG
        printf("OnWait\n");
#endif

        if (!mutex_ht.count(mutex_ptr)) {
            fprintf(stderr, "[ERROR] Program bug- mutex is not initialized before wait()!\n");
            OnExit();
            exit(1);
        }
        g_llog_t* llog = llogs[mutex_ht[mutex_ptr]];
        llog->logValue(((canary_thread_t*) st)->tid);
    }

    void OnPreMutexInit(bool race, void* st) {
        if (st != NULL && race) {
            mutexInitLock();
        }
    }

    void OnMutexInit(pthread_mutex_t* mutex_ptr, bool race, void* st) {
        if (st == NULL) {
            return;
        }

        // if mutex_ht contains mutex_ptr, it means that the original one may be
        // destroyed; No matther mutex_ht contains it or not, it is a new lock
        mutex_ht[mutex_ptr] = mutex_id++;
        g_llog_t * llog = new g_llog_t;
        llogs.push_back(llog);


        if (race) {
            mlog.logValue(((canary_thread_t*) st)->tid);

            mutexInitUnlock();
        }
    }

    void OnPreFork(bool race, void* st) {
        if (st == NULL) {
            return;
        }

        if (!start)
            start = true;

#ifdef DEBUG
        printf("OnPreFork\n");
#endif
        if (race) forkLock();
        forking = true; // I am forking new thread.
    }

    void OnFork(pthread_t* forked_tid_ptr, bool race, void* st) {
        if (st == NULL) {
            return;
        }
#ifdef DEBUG
        printf("OnFork\n");
#endif

        pthread_t ftid = *(forked_tid_ptr);
        if (!thread_ht.count(ftid)) {
            canary_thread_t* st = new canary_thread_t(num_shared_vars, num_local_vars, thread_ht.size());
            thread_ht[ftid] = st;
        }

        forking = false; // Forking finishes.

        if (race) {
            flog.logValue(((canary_thread_t*) st)->tid);

            forkUnlock();
        }
    }
}

/* ************************************************************************
 * Signal Process
 * ************************************************************************/

void sigroutine(int dunno) {
    printSigInformation(dunno);

    OnExit();
    exit(dunno);
}
