  Deadlock Detection Library 

Deadlock Detection Library
==========================

This is a debugging library designed to detect deadlocks in multi-threaded applications. It uses hook-based mutexes that can be toggled on or off in debug mode to catch potential deadlocks.
( This was initially a part of Umbrella game engine )
Features
--------

*   Detects deadlocks by managing mutex ownership and wait cycles.
*   Provides detailed stack trace logging for easier debugging.
*   Can be enabled or disabled using compile-time flags.
*   Works with a variety of threading patterns, with minimal changes to your existing code.

How it Works
------------

The library replaces regular mutexes with a custom `HookMutex` class that interacts with the `CentralAuthority` to track which threads are waiting for which locks.

### Key Components

*   `HookMutex`: A custom mutex class used for lock management.
*   `CentralAuthority`: Manages all mutexes and detects potential deadlock cycles.
*   `StackTracer`: Captures stack traces when threads enter a wait state, aiding in debugging deadlocks.
*   `ThreadSafeStringStream`: A thread-safe logging mechanism to output debug information.

Getting Started
---------------

### Installation

This repo contains a Code::Blocks project that can be used to build and run a demo application. To use the library in your own project, you will have to copy the algorithm manually into your project.
    

### Usage

In your code, replace `std::mutex` with the custom `DeadlockDetection::HookMutex` to enable deadlock detection in debug mode.

    
    DeadlockDetection::HookMutex mutex;
    mutex.lock();
    // Critical section
    mutex.unlock();
    

In release mode, you can turn off deadlock detection by disabling the `DETECT_DEADLOCKS` macro.

### Deadlock Demo

You can use the provided `runDeadlockDemo()` function to simulate and test deadlock detection:

    
    void runDeadlockDemo() {
        DeadlockDetection::HookMutex mutex1, mutex2;
    
        std::thread t1([&]() {
            mutex1.lock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mutex2.lock();
            mutex2.unlock();
            mutex1.unlock();
        });
    
        std::thread t2([&]() {
            mutex2.lock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mutex1.lock();
            mutex1.unlock();
            mutex2.unlock();
        });
    
        t1.join();
        t2.join();
    }
    

Configuration
-------------

The library's behavior can be configured by modifying the `DeadlockDetection::CentralAuthority` class. Key configurable aspects include:

*   Logging verbosity
*   Deadlock resolution strategy
*   Performance tuning parameters

Performance Considerations
--------------------------

While this library provides powerful deadlock detection capabilities, it does introduce some overhead due to the central management of all mutex operations. Consider the following when using this library:

*   Use in debug builds or controlled testing environments.
*   Be aware of potential performance impact in high-concurrency scenarios.
*   Consider disabling for release builds in performance-critical applications.

Contributing
------------

Contributions to improve the library are welcome. Please follow these steps:

1.  Fork the repository
2.  Create a new branch for your feature
3.  Commit your changes
4.  Push to your branch
5.  Create a new Pull Request

License
-------

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

Contact
-------

For questions or support, please open an issue on the GitHub repository or contact [hassenmohamedhia@gmail.com](mailto:hassenmohamedhia@gmail.com).
