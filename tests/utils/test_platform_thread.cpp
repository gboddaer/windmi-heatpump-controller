/**
 * @file tests/utils/test_platform_thread.cpp
 * @brief Platform threading tests: Thread, Mutex, ConditionVariable, UniqueLock
 *
 * Tests the platform abstraction layer for threading primitives.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "utils/Platform.hpp"

using namespace windmi;

// ─── Thread Tests ───

TEST(ThreadTest, CreateWithCallable) {
    std::atomic<bool> executed{false};
    Thread t([&executed]() {
        executed = true;
    });
    t.join();
    EXPECT_TRUE(executed.load());
}

TEST(ThreadTest, JoinAfterStart) {
    std::atomic<bool> done{false};
    Thread t([&done]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        done = true;
    });
    t.join();
    EXPECT_TRUE(done.load());
}

TEST(ThreadTest, DetachMakesNotJoinable) {
    Thread t([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    EXPECT_TRUE(t.joinable());
    t.detach();
    EXPECT_FALSE(t.joinable());
    // Give detached thread time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST(ThreadTest, MultipleThreadsConcurrent) {
    std::atomic<int> counter{0};
    std::vector<Thread> threads;

    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&counter]() {
            counter++;
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(counter.load(), 4);
}

TEST(ThreadTest, MoveSemanticsPreservesExecution) {
    std::atomic<bool> executed{false};
    Thread t1([&executed]() { executed = true; });
    Thread t2(std::move(t1));
    t2.join();
    EXPECT_TRUE(executed.load());
}

TEST(ThreadTest, MoveAssignmentToNewThread) {
    std::atomic<bool> executed{false};
    Thread t1([&executed]() { executed = true; });
    // Move into a new thread (avoid default-constructed Thread with uninitialized impl_)
    Thread t2(std::move(t1));
    t2.join();
    EXPECT_TRUE(executed.load());
}

// ─── Mutex Tests ───

TEST(MutexTest, LockGuardRAII) {
    Mutex m;
    {
        LockGuard lock(m);
        // Lock is held here
    }
    // Lock released here - should be able to re-lock
    {
        LockGuard lock(m);
        (void)lock;
    }
}

TEST(MutexTest, ConcurrentCounterIncrement) {
    Mutex m;
    std::atomic<int> counter{0};

    auto increment = [&m, &counter]() {
        for (int i = 0; i < 1000; i++) {
            LockGuard lock(m);
            counter++;
        }
    };

    Thread t1(increment);
    Thread t2(increment);
    t1.join();
    t2.join();

    EXPECT_EQ(counter.load(), 2000);
}

// ─── ConditionVariable Tests ───

TEST(ConditionVariableTest, NotifyOneWakesSingleWaiter) {
    Mutex m;
    ConditionVariable cv;
    std::atomic<bool> ready{false};

    Thread t([&m, &cv, &ready]() {
        {
            UniqueLock ul(m);
            cv.wait(ul, [&ready]() { return ready.load(); });
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ready = true;
    cv.notify_one();
    t.join();
}

TEST(ConditionVariableTest, NotifyAllWakesAllWaiters) {
    Mutex m;
    ConditionVariable cv;
    std::atomic<int> notified_count{0};

    auto waiter = [&m, &cv, &notified_count]() {
        UniqueLock ul(m);
        cv.wait(ul);
        notified_count++;
    };

    Thread t1(waiter);
    Thread t2(waiter);
    Thread t3(waiter);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        LockGuard lock(m);
        cv.notify_all();
    }

    t1.join();
    t2.join();
    t3.join();

    EXPECT_EQ(notified_count.load(), 3);
}

TEST(ConditionVariableTest, WaitWithPredicateHandlesSpuriousWakeups) {
    Mutex m;
    ConditionVariable cv;
    std::atomic<bool> ready{false};
    std::atomic<int> wakeup_count{0};

    Thread t([&m, &cv, &ready, &wakeup_count]() {
        UniqueLock ul(m);
        cv.wait(ul, [&ready, &wakeup_count]() {
            wakeup_count++;
            return ready.load();
        });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ready = true;
    cv.notify_one();
    t.join();

    EXPECT_GT(wakeup_count.load(), 0);
}

// ─── UniqueLock Tests ───

TEST(UniqueLockTest, OwnsLockOnConstruction) {
    Mutex m;
    UniqueLock ul(m);
    EXPECT_TRUE(ul.owns_lock());
    EXPECT_NE(ul.mutex(), nullptr);
}

TEST(UniqueLockTest, UnlockReleasesLock) {
    Mutex m;
    UniqueLock ul(m);
    ul.unlock();
    EXPECT_FALSE(ul.owns_lock());
    // Should be able to re-lock
    ul.lock();
    EXPECT_TRUE(ul.owns_lock());
}
