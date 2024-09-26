#include <iostream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <cpptrace/cpptrace.hpp>
#include <algorithm>
#include <sstream>

typedef int MutexId;
typedef std::atomic<MutexId> AtomicMutexId;

namespace DeadlockDetection
{
    class ThreadSafeStringStream
    {
    public:
        template <typename T>
        ThreadSafeStringStream &operator<<(T value)
        {
            // std::lock_guard<std::mutex> lock(mtx); // Lock the mutex for thread safety
            std::cout << value;
            std::cout.flush();
            return *this;
        }

        // add support for std::endl
        ThreadSafeStringStream &operator<<(std::ostream &(*manip)(std::ostream &))
        {
            std::lock_guard<std::mutex> lock(mtx); // Lock the mutex for thread safety
            std::cout << manip;
            return *this;
        }

        // Provide access to the str() method to retrieve the string
        std::string str()
        {
            std::lock_guard<std::mutex> lock(mtx); // Lock the mutex for thread safety
            return ss.str();
        }

        // Optionally, allow resetting or clearing the stringstream
        void clear()
        {
            std::lock_guard<std::mutex> lock(mtx); // Lock the mutex for thread safety
            ss.str("");                            // Clear the string buffer
            ss.clear();                            // Clear any error flags
        }

    private:
        std::stringstream ss; // Underlying stringstream
        std::mutex mtx;       // Mutex for synchronization
    };
    // Forward declarations
    class CentralAuthority;
    class FakeMutex;

    ThreadSafeStringStream logStream;

    // Class to capture and log stack traces
    class StackTracer
    {
    public:
        void capture()
        {
            m_stacktrace = cpptrace::generate_trace();
        }

        void log() const
        {
            m_stacktrace.print();
            DeadlockDetection::logStream << "===============================" << std::endl;
        }

    private:
        cpptrace::stacktrace m_stacktrace;
    };

    // Class representing a fake mutex provided by the central authority
    class FakeMutex
    {
        static AtomicMutexId s_currentId;

    public:
        static constexpr MutexId s_invalidId = 0;
        FakeMutex()
        {
            this->m_id.store(AtomicMutexId(s_currentId.fetch_add(1, std::memory_order_relaxed)));
        }
        FakeMutex(FakeMutex &other)
        {
            m_id.store(AtomicMutexId(other.m_id.load(std::memory_order_relaxed)));
        }
        FakeMutex(FakeMutex &&other)
        {
            m_id.store(AtomicMutexId(other.m_id.load(std::memory_order_relaxed)));
        }
        FakeMutex(const MutexId &id)
        {
            m_id.store(AtomicMutexId(id));
        }
        void lock();
        void unlock();
        bool try_lock();
        MutexId getId() const { return m_id.load(std::memory_order_relaxed); }
        // operator ==
        bool operator==(const FakeMutex &other) const
        {
            return m_id.load(std::memory_order_relaxed) == other.m_id.load(std::memory_order_relaxed);
        }
        bool operator==(const FakeMutex &&other) const
        {
            return m_id.load(std::memory_order_relaxed) == other.m_id.load(std::memory_order_relaxed);
        }
        bool operator==(const MutexId &other) const
        {
            return m_id.load(std::memory_order_relaxed) == other;
        }
        const FakeMutex &operator=(const DeadlockDetection::FakeMutex &other)
        {
            m_id.store(AtomicMutexId(other.m_id.load(std::memory_order_relaxed)));
            return *this;
        }

    private:
        AtomicMutexId m_id;
    };
    AtomicMutexId FakeMutex::s_currentId{1};

    // Central authority class managing all fake mutexes and deadlock detection
    class CentralAuthority
    {
    public:
        CentralAuthority() = default;
        bool lockMutex(FakeMutex &mutex);
        void unlockMutex(FakeMutex &mutex);
        bool tryLockMutex(FakeMutex &mutex);

        // singletons
        static CentralAuthority &getInstance();

    private:
        struct ThreadData
        {
            std::vector<MutexId> lockedMutexes;
            FakeMutex mutexWaitingFor = FakeMutex::s_invalidId;
            StackTracer waitStack;
            std::atomic<bool> waitFlag{false};
        };
        bool checkForDeadlock(const std::thread::id &startThread, const FakeMutex &mutexToLock);
        std::vector<FakeMutex> getOwnedMutexes(const std::vector<std::thread::id> &threadIds) const;
        std::vector<std::thread::id> getWaitingThreads(const std::vector<FakeMutex> &mutexes) const;
        void reportDeadlock(const std::vector<std::thread::id> &cycle);
        std::vector<std::thread::id> buildDeadlockCycleFromCurrentThread() const;
        void registerWaintingForMutex(ThreadData &threadData, FakeMutex &mutex);
        void doWaitForMutex(ThreadData &threadData, FakeMutex &mutex);
        void continueAfterWaitForMutex(ThreadData &threadData, FakeMutex &mutex);
        std::mutex m_globalMutex;
        std::unordered_map<std::thread::id, ThreadData> m_threadData;
        std::unordered_map<MutexId, std::thread::id> m_mutexOwners;
    };
    CentralAuthority &CentralAuthority::getInstance()
    {
        static CentralAuthority instance;
        return instance;
    }
    // Implementation of FakeMutex methods
    void FakeMutex::lock()
    {
        CentralAuthority::getInstance().lockMutex(*this);
    }
    void FakeMutex::unlock()
    {
        CentralAuthority::getInstance().unlockMutex(*this);
    }
    bool FakeMutex::try_lock()
    {
        return CentralAuthority::getInstance().tryLockMutex(*this);
    }
    // Implementation of CentralAuthority methods
    bool CentralAuthority::lockMutex(FakeMutex &mutex)
    {
        auto mid = mutex.getId();
        logStream << "thread " << std::this_thread::get_id() << " is locking mutex " << mid << std::endl;
        std::lock_guard<std::mutex> lock(m_globalMutex);
        auto thisThread = std::this_thread::get_id();
        logStream << "Got this thread id " << thisThread << std::endl;
        auto &threadData = m_threadData[thisThread];
        logStream << "Will check if mutex is already owned by any thread" << std::endl;
        // Check if the mutex is already owned
        auto it = m_mutexOwners.find(mutex.getId());
        if (it != m_mutexOwners.end() && it->second != thisThread)
        {
            registerWaintingForMutex(threadData, mutex);
            // Check for deadlock
            if (checkForDeadlock(thisThread, mutex))
            {
                return false; // Deadlock detected
            }
            // Mutex is owned by another thread, we need to wait
            doWaitForMutex(threadData, mutex);
            continueAfterWaitForMutex(threadData, mutex);
            return true; // Successfully acquired after waiting
        }
        // Mutex is free, lock it
        m_mutexOwners[mutex.getId()] = thisThread;
        threadData.lockedMutexes.push_back(mutex.getId());
        logStream << "Thread " << thisThread << " locked mutex " << mutex.getId() << std::endl;
        return true;
    }

    void CentralAuthority::unlockMutex(FakeMutex &mutex)
    {
        logStream << "unlocking mutex " << mutex.getId() << std::endl;

        std::lock_guard<std::mutex> lock(m_globalMutex);

        auto thisThread = std::this_thread::get_id();
        auto &threadData = m_threadData[thisThread];

        // Remove mutex from thread's locked mutexes
        auto it = std::find(threadData.lockedMutexes.begin(), threadData.lockedMutexes.end(), mutex.getId());
        if (it != threadData.lockedMutexes.end())
        {
            threadData.lockedMutexes.erase(it);
        }

        // Remove mutex from global owners map
        m_mutexOwners.erase(mutex.getId());

        logStream << "Thread " << thisThread << " unlocked mutex " << mutex.getId() << std::endl;

        // Check if any thread is waiting for this mutex
        for (auto &pair : m_threadData)
        {
            if (pair.second.mutexWaitingFor == mutex)
            {
                pair.second.waitFlag.store(false, std::memory_order_release);
                break;
            }
        }
    }

    bool CentralAuthority::tryLockMutex(FakeMutex &mutex)
    {
        std::lock_guard<std::mutex> lock(m_globalMutex);

        auto thisThread = std::this_thread::get_id();
        auto &threadData = m_threadData[thisThread];

        // Check if the mutex is already owned
        auto it = m_mutexOwners.find(mutex.getId());
        if (it != m_mutexOwners.end())
        {
            return false; // Mutex is already owned
        }

        // Mutex is free, lock it
        m_mutexOwners[mutex.getId()] = thisThread;
        threadData.lockedMutexes.push_back(mutex.getId());
        logStream << "Thread " << thisThread << " try_locked mutex " << mutex.getId() << std::endl;
        return true;
    }

    // get all the mutexes owned by a thread
    std::vector<FakeMutex> CentralAuthority::getOwnedMutexes(const std::vector<std::thread::id> &threadIds) const
    {
        std::vector<FakeMutex> result;
        for (const auto &threadId : threadIds)
        {
            auto it = m_threadData.find(threadId);
            if (it != m_threadData.end())
            {
                for (const auto &mutexId : it->second.lockedMutexes)
                {
                    result.push_back((const MutexId &)mutexId);
                }
            }
        }
        return result;
    }
    // get waiting threads for a mutex
    std::vector<std::thread::id> CentralAuthority::getWaitingThreads(const std::vector<FakeMutex> &mutexes) const
    {
        std::vector<std::thread::id> result;
        for (const auto &mutex : mutexes)
        {
            for (const auto &pair : m_threadData)
            {
                if (pair.second.mutexWaitingFor == mutex.getId())
                {
                    result.push_back(pair.first);
                }
            }
        }
        return result;
    }
    std::vector<std::thread::id> CentralAuthority::buildDeadlockCycleFromCurrentThread() const
    {
        std::vector<std::thread::id> cycle;
        auto thisThread = std::this_thread::get_id();
        auto &threadData = m_threadData.at(thisThread);
        auto waitingThread = thisThread;
        do
        {
            cycle.push_back(waitingThread);
            if (! m_threadData.count(waitingThread))
            {
                return cycle;
            }
            auto& waitingThreadData = m_threadData.at(waitingThread);
            auto& waitingMutex = waitingThreadData.mutexWaitingFor;
            if (m_mutexOwners.count(waitingMutex.getId()) == 0)
            {
                return cycle;
            }
            auto& mutexOwner = m_mutexOwners.at(waitingMutex.getId());
            waitingThread = mutexOwner;
        } while (waitingThread != std::this_thread::get_id());
        return cycle;
    }
    bool CentralAuthority::checkForDeadlock(const std::thread::id &startThread, const FakeMutex &mutexToLock)
    {
        logStream << "checkForDeadlock" << std::endl;
        // is locking this mutex a potential deadlock?
        // deadlock = cycle of waiting threads in which each thread is waiting for a mutex that is already owned by the next thread in the cycle
        // this new lock can be a deadlock only if the mutex is already acquired by a thread
        // before we start waiting for a mutex to be unlocked we need to make sure that the owner is not also waiting
        // there is a tree to repsent this :
        // root = this thread
        // child = the mutex that is being locked by this thread + mutexToLock
        // grandchildend = the threads that is currently waiting for those mutexes
        // great-grandchildend = the threads that are waiting for those mutexes
        // great-great-grandchildend = the mutexes that are owned by each of those threads
        // and so on...
        // this tree should end with this thread being only in the root of the tree and not in any other node
        // to detect deadlock we get all the mutexes owned by this thread
        // then for each mutex we get all the thread waiting for it
        // then for each thread we get all the mutexes owned by that thread
        // then for each mutex we get all the thread waiting for it
        // and so on...
        std::vector<std::thread::id> cycle;
        std::unordered_set<std::thread::id> visited;
        auto currentThread = startThread;
        auto ownedMutexes = getOwnedMutexes({startThread});
        ownedMutexes.push_back(mutexToLock.getId());
        auto waitingThreads = getWaitingThreads(ownedMutexes);
#if 0
        logStream << "owned mutexes ( this thread mutexes + mutexToLock ): " << std::endl;
        for (const auto &mutex : ownedMutexes)
        {
            logStream << "mutex " << mutex.getId() << std::endl;
        }
        logStream << "waiting threads for owned mutexes : " << std::endl;
        for (const auto &thread : waitingThreads)
        {
            logStream << "thread " << thread << std::endl;
        }
        logStream << "beginning tree traversal for deadlock detection " << std::endl;
#endif
        do
        {
            logStream << "listing waiting threads for owned mutexes : " << std::endl;
            for (const auto &thread : waitingThreads)
            {
                logStream << "thread " << thread << std::endl;
            }
            auto threadsMutexes = getOwnedMutexes(waitingThreads);
            logStream << "listing owned mutexes for waiting threads : " << std::endl;
            for (const auto &mutex : threadsMutexes)
            {
                logStream << "mutex " << mutex.getId() << std::endl;
            }
            if (std::find(waitingThreads.begin(), waitingThreads.end(), currentThread) != waitingThreads.end())
            {
                // We've found a cycle
                logStream << "Cycle detected" << std::endl;
                reportDeadlock(buildDeadlockCycleFromCurrentThread());
                return true; // Deadlock detected
            }
            else
            {
                logStream << "No cycle detected in this step" << std::endl;
            }
            logStream << "stepping" << std::endl;
            waitingThreads = getWaitingThreads(getOwnedMutexes(waitingThreads));
            // implementaion goes here
        } while (waitingThreads.size() > 0);
        logStream << "No cycle detected" << std::endl;
        return false; // No deadlock detected
    }

    void CentralAuthority::reportDeadlock(const std::vector<std::thread::id> &cycle)
    {
        logStream << "Deadlock detected!" << std::endl;
        logStream << "Cycle of threads length " << cycle.size() << std::endl;
        logStream << "Cycle of threads : " << std::endl;
        for (const auto &threadId : cycle)
        {
            auto &threadData = m_threadData[threadId];
            logStream << "Thread " << threadId << " is waiting for mutex " << threadData.mutexWaitingFor.getId() << std::endl;
            logStream << "wait stack : " << std::endl;
            threadData.waitStack.log();
        }
        // exit 
        exit(1);
    }

    void CentralAuthority::registerWaintingForMutex(ThreadData &threadData, FakeMutex &mutex)
    {
        threadData.mutexWaitingFor = mutex;
        threadData.waitStack.capture();
        threadData.waitFlag.store(true, std::memory_order_release);
        logStream << "Thread " << std::this_thread::get_id() << " waiting for mutex " << mutex.getId() << std::endl;
        // Release the global mutex while waiting
        m_globalMutex.unlock();
    }
    void CentralAuthority::doWaitForMutex(ThreadData &threadData, FakeMutex &mutex)
    {
        m_globalMutex.unlock();
        while (threadData.waitFlag.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }
    void CentralAuthority::continueAfterWaitForMutex(ThreadData &threadData, FakeMutex &mutex)
    {
        // Reacquire the global mutex
        m_globalMutex.lock();
        // Update thread data
        threadData.mutexWaitingFor = FakeMutex::s_invalidId;
        m_mutexOwners[mutex.getId()] = std::this_thread::get_id();
        threadData.lockedMutexes.push_back(mutex.getId());
        logStream << "Thread " << std::this_thread::get_id() << " acquired mutex " << mutex.getId() << " after waiting" << std::endl;
    }

} // namespace DeadlockDetection

// Demo function to test the deadlock detection system
void runDeadlockDemo()
{
    DeadlockDetection::CentralAuthority authority;

    auto mutex1 = DeadlockDetection::FakeMutex();
    auto mutex2 = DeadlockDetection::FakeMutex();

    std::thread t1([&]()
                   {
        while (true) {
            mutex1.lock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mutex2.lock();
            // Critical section
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mutex2.unlock();
            mutex1.unlock();
        } });

    std::thread t2([&]()
                   {
        while (true) {
            mutex2.lock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mutex1.lock();
            // Critical section
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mutex1.unlock();
            mutex2.unlock();
        } });

    t1.join();
    DeadlockDetection::logStream << "Thread 1 finished" << std::endl;
    t2.join();
    DeadlockDetection::logStream << "Thread 1 finished" << std::endl;
}

int main()
{

    bool exceptionThrown = false;
    try
    {
        runDeadlockDemo();
    }
    catch (...)
    {
        exceptionThrown = true;
        std::cout << DeadlockDetection::logStream.str() << std::endl;
    }
    if (!exceptionThrown)
    {

        std::cout << DeadlockDetection::logStream.str() << std::endl;
        std::cout << "No exception thrown" << std::endl;
    }

    return 0;
}
