#ifdef KERNEL_TESTS

#include <log/log.hpp>
#include <scheduler/policy/scheduler_policy.hpp>
#include <test/test.hpp>

#include <cstdint>

namespace test_round_robin {

// RoundRobinScheduler never dereferences the process pointers it's given
// (see enqueue()/pick_next() in round_robin.cpp), so these tests use fake,
// distinct non-null pointers as opaque identity tokens instead of
// constructing real process::Process objects.
process::Process* fake_process(std::uintptr_t id)
{
    return reinterpret_cast<process::Process*>(id);
}

void test_pick_next_empty_returns_null()
{
    scheduler::policy::RoundRobinScheduler s;
    test::assert_null(s.pick_next(), "pick_next() on an empty scheduler returns nullptr");
}

void test_enqueue_then_pick_next_returns_it()
{
    scheduler::policy::RoundRobinScheduler s;
    process::Process* a = fake_process(1);

    s.enqueue(a);

    test::assert_eq(s.pick_next(), a, "pick_next() returns the only enqueued process");
}

void test_pick_next_removes_from_queue()
{
    scheduler::policy::RoundRobinScheduler s;
    process::Process* a = fake_process(1);

    s.enqueue(a);
    s.pick_next();

    test::assert_null(s.pick_next(), "pick_next() does not return the same process twice");
}

void test_fifo_order()
{
    scheduler::policy::RoundRobinScheduler s;
    process::Process* a = fake_process(1);
    process::Process* b = fake_process(2);
    process::Process* c = fake_process(3);

    s.enqueue(a);
    s.enqueue(b);
    s.enqueue(c);

    test::assert_eq(s.pick_next(), a, "fifo order: first enqueued is picked first");
    test::assert_eq(s.pick_next(), b, "fifo order: second enqueued is picked second");
    test::assert_eq(s.pick_next(), c, "fifo order: third enqueued is picked third");
    test::assert_null(s.pick_next(), "fifo order: queue is empty after all picks");
}

void test_reenqueue_after_pick_rotates_to_back()
{
    scheduler::policy::RoundRobinScheduler s;
    process::Process* a = fake_process(1);
    process::Process* b = fake_process(2);

    s.enqueue(a);
    s.enqueue(b);

    process::Process* first = s.pick_next();
    s.enqueue(first); // simulate preempt(): still runnable, goes to the back

    test::assert_eq(first, a, "round robin: a runs first");
    test::assert_eq(s.pick_next(), b, "round robin: b runs next, since a moved to the back");
    test::assert_eq(s.pick_next(), a, "round robin: a runs again after being re-enqueued");
}

void test_enqueue_after_draining_queue()
{
    scheduler::policy::RoundRobinScheduler s;
    process::Process* a = fake_process(1);
    process::Process* b = fake_process(2);

    s.enqueue(a);
    s.pick_next();

    test::assert_null(s.pick_next(), "queue is empty after draining");

    s.enqueue(b);

    test::assert_eq(s.pick_next(), b, "a process enqueued after draining is still picked up");
}

void run()
{
    log::info("Running round robin scheduler tests...");

    test_pick_next_empty_returns_null();
    test_enqueue_then_pick_next_returns_it();
    test_pick_next_removes_from_queue();
    test_fifo_order();
    test_reenqueue_after_pick_rotates_to_back();
    test_enqueue_after_draining_queue();
}

}

#endif // KERNEL_TESTS
