# EECE 4811 HW2

## Names: Tim,Nick

# Q1 Lock Analysis

No, it is not correct. The guard makes sure only one thread touches the flag or queue at a time. Without it, two threads can change them at once.

### Execution 1: Two threads get the lock

Lock starts free, `flag = 0`.

| step | Thread A  | Thread B  | flag |
| ---- | --------- | --------- | ---- |
| 1    | sees 0    |           | 0    |
| 2    | stopped   |           | 0    |
| 3    |           | sees 0    | 0    |
| 4    |           | sets 1, enters CS | 1    |
| 5    | sets 1, enters CS | in CS     | 1    |

Both threads are in the critical section, so mutual exclusion is broken.

### Execution 2: A waiting thread gets lost

The lock is held, so A and B both go to add themselves to the queue. A reads the end of the queue and gets stopped. B reads the same end, adds itself, and sleeps. A starts again and adds itself using the old end, which overwrites B's spot.

B is asleep but nothing in the queue points to it, so it never gets woken up. This breaks progress and B starves.

# Q2 Lock Analysis II

No, it is not correct. `flag = flag - 1` is a read, a subtract, and a store, not one step. The original `flag = 0` is a single store.

Following the hint, T3 wrongly calls `unlock()` on a free lock.

| step | T1        | T2        | T3        | flag |
| ---- | --------- | --------- | --------- | ---- |
| 1    |           |           | reads 0, stopped | 0    |
| 2    | enters CS |           |           | 1    |
| 3    | in CS     |           | stores 0 - 1 | -1   |
| 4    | in CS     | sees -1, enters CS |           | 1    |

T1 and T2 are both in the critical section, so mutual exclusion is broken. With the original `flag = 0`, an extra unlock on a free lock just writes 0 and nothing breaks.

# Q3 Lock Analysis III

Without `setpark()`, T1 holds the lock. T2 adds itself to the queue and sets `guard = 0`, then gets stopped right before `park()`. T1 unlocks and calls `unpark(T2)`, but T2 isn't asleep yet so the wakeup is lost. T2 then calls `park()` and sleeps forever, and the lock stays at `flag = 1` so everyone else gets stuck too.

With `setpark()` before `guard = 0`, T2 tells the OS it is about to park. If `unpark(T2)` happens before `park()`, the OS remembers it and `park()` returns right away. It has to go before `guard = 0` because the unlocker can't reach `unpark()` until it gets the guard.

# Q4 Comparing Lock Algorithms

## Implementation

Everything is in `main.c`, written in C with pthreads. No mutexes.

`FetchAndAdd` and `CompareAndSwap` work like the book's and use C11 `<stdatomic.h>`.

**Ticket lock (Fig 28.7)** Each thread takes a number with `FetchAndAdd` and spins until `turn` matches it. Unlock adds 1 to `turn`. Threads get the lock in order.

**CAS lock (28.9)** Spins until `CompareAndSwap` flips the flag from 0 to 1. Unlock sets it back to 0. No order, whoever wins the CAS gets the lock.

## How to compile and run

Needs Linux or WSL with gcc.

```text
gcc -o main main.c -Wall -pthread
./main
```

## Dependencies

None besides the C library and pthreads. Tested with gcc 13.3.0 on Ubuntu 24.04 in WSL2.

## Benchmark Design

We change how many threads fight over the lock, 1, 2, 4, 8, and 16. Each thread runs this loop 10,000 times.

```text
t0 = now
lock()
wait time = now - t0
counter++ and some work
unlock()
some work outside the lock
```

Wait time is from right before `lock()` to when it returns. Both locks run the same loop, every trial uses a fresh lock, all threads start together, and each setup runs 5 times and gets averaged. We report average, median, p99, and max. p99 means 99% of waits were shorter than this, so it shows the slowest waits.

The counter checks the lock works. If two threads were ever in the critical section together, increments would get lost. It matched threads × 10,000 every time.

## Results

20 logical cores, WSL2. Times in ns, averaged over 5 trials.

| lock   | threads |   avg | median |    p99 |       max |
| ------ | ------: | ----: | -----: | -----: | --------: |
| ticket |       1 |    20 |     19 |     25 |       212 |
| cas    |       1 |    20 |     19 |     22 |       242 |
| ticket |       2 |   113 |    108 |    139 |    53,744 |
| cas    |       2 |   109 |    105 |    128 |    38,756 |
| ticket |       4 |   701 |    660 |  1,008 |    83,240 |
| cas    |       4 |   575 |    385 |  2,640 |    63,880 |
| ticket |       8 | 2,197 |  1,869 |  9,951 |   392,144 |
| cas    |       8 | 2,241 |  1,395 | 12,095 |   139,158 |
| ticket |      16 | 9,360 |  4,784 | 60,156 | 3,138,690 |
| cas    |      16 | 7,345 |  3,432 | 54,043 | 1,635,962 |

We also tried 32 threads. The ticket lock never finished after 5+ minutes, so it is not in the table.

## Analysis

At 1 and 2 threads both locks are about the same since barely anyone is waiting. As threads go up, wait time grows fast for both.

The main difference is fairness. At 4 threads CAS had a lower median, 385 vs 660 ns, but a worse p99, 2,640 vs 1,008 ns. Same at 8 threads. CAS has no order, so a thread that just released the lock can grab it again while others keep losing. The ticket lock goes in order, so wait times are more even.

At 16 threads the ticket lock was worse overall, 9,360 vs 7,345 ns average. If the thread whose turn is next gets paused by the OS, everyone behind it has to wait. CAS doesn't care who is next, so any running thread can take the lock. At 32 threads, more than our cores, this happened constantly and the ticket lock got stuck.

Overall, CAS is faster most of the time and keeps working with lots of threads, but it is unfair. The ticket lock is fair but falls apart when there are more threads than cores.

# Sources

OSTEP Chapter 28 Locks, Figures 28.6, 28.7, 28.9 and Section 28.9

Lecture slides OS 3 Thread and Lock, Spin Lock Performance and Ticket Lock sections

C11 stdatomic.h reference, https://en.cppreference.com/w/c/atomic

# AI Use

We used an AI assistant to help debug the code, organize the README, and help with printing the results to the screen.