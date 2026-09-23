// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 NatureSense

// The actor framework's **host test** — the framework's own semantics, asserted on this machine.
//
// What it proves, and why each one is worth a test:
//
//   1. a message posted to a ref is **handled**, on the actor's own task, and both messages of a
//      pair arrive — the whole point of a mailbox, and the thing a compile check cannot show;
//   2. an actor that refuses in `init()` **starts nothing**: no mailbox, no task, and a system that
//      says so rather than running half-built;
//   3. an actor can **send to another actor** through a ref it was constructed with, with two
//      different message types — which is the classical pattern and the reason refs are typed by
//      message rather than by actor;
//   4. a full mailbox **refuses instead of blocking** the sender — the design decision that makes a
//      mailbox actor worth its own task;
//   5. an actor can **post to itself**, which is what `self()` is for.
//
// Underneath it is a `std::thread` per actor and a real queue per mailbox (see `freertos_fake.cpp`),
// so "it dispatches" is observed rather than asserted.

#include <actors/actor.hpp>
#include <actors/scheduler.hpp>

#include <freertos_fake.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

/// Poll a condition for up to two seconds, then ask it once more — so a slow machine is not a
/// failure and a genuine hang is.
template <typename Predicate>
bool wait_for(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

// --- 1. a message reaches the actor -------------------------------------------------------------

struct Add {
    int amount = 0;
};

class Counter final : public spire::Actor<Add> {
public:
    explicit Counter(std::atomic<int>& total) : total_(total) {}

protected:
    void on_message(const Add& message) override { total_ += message.amount; }

private:
    std::atomic<int>& total_;
};

void a_message_reaches_the_actor() {
    std::atomic<int> total{0};
    spire::Scheduler scheduler;

    auto counter = scheduler.spawn<Add, Counter>("counter", 4096, 5, tskNO_AFFINITY, total);
    check(counter.valid(), "a spawned actor has a live ref");
    check(scheduler.size() == 1, "and the scheduler owns it");

    check(scheduler.start(), "the system starts");
    check(scheduler.running(), "and says so");
    check(counter.send(Add{3}), "the first message is accepted");
    check(counter.send(Add{4}), "and the second");
    check(wait_for([&] { return total.load() == 7; }), "both were handled, on the actor's own task");

    scheduler.stop();
    check(!scheduler.running(), "the system stops");
    spire_fake_join_tasks();

    // The mailbox is shared with the ref, so a ref outlives the actor safely: invalid, not dangling.
    check(!counter.valid(), "a ref goes invalid after stop");
    check(!counter.send(Add{1}), "and a send after stop refuses rather than crashing");
}

// --- 2. an actor that refuses starts nothing ----------------------------------------------------

class Refuser final : public spire::Actor<Add> {
public:
    explicit Refuser(std::atomic<bool>& asked) : asked_(asked) {}

protected:
    bool init() override {
        asked_ = true;
        return false;
    }
    void on_message(const Add&) override {}

private:
    std::atomic<bool>& asked_;
};

void a_refused_actor_starts_nothing() {
    std::atomic<bool> asked{false};
    spire::Scheduler scheduler;

    auto refuser = scheduler.spawn<Add, Refuser>("refuser", 4096, 5, tskNO_AFFINITY, asked);
    check(!asked.load(), "nothing runs until the system is started");

    check(!scheduler.start(), "a refused actor refuses the start");
    check(asked.load(), "and `init()` was the reason");
    check(!refuser.valid(), "a refused actor has no mailbox to post into");
    check(!refuser.send(Add{1}), "so nothing can be sent to it");
    check(scheduler.size() == 1, "the scheduler still owns it, to be cleaned up");

    scheduler.stop();
    spire_fake_join_tasks();
}

// --- 3. one actor sends to another --------------------------------------------------------------

struct Reading {
    int value = 0;
};

struct Alarm {
    int value = 0;
};

/// The far end: remembers the last alarm.
class Sink final : public spire::Actor<Alarm> {
public:
    explicit Sink(std::atomic<int>& last) : last_(last) {}

protected:
    void on_message(const Alarm& alarm) override { last_ = alarm.value; }

private:
    std::atomic<int>& last_;
};

/// The near end: consumes readings and forwards each as an alarm — an actor that talks to an actor,
/// through a ref it was *constructed* with rather than one it looks up.
class Sampler final : public spire::Actor<Reading> {
public:
    Sampler(spire::ActorRef<Alarm> next, std::atomic<int>& seen) : next_(next), seen_(seen) {}

protected:
    void on_message(const Reading& reading) override {
        seen_ += reading.value;
        next_.send(Alarm{reading.value});
    }

private:
    spire::ActorRef<Alarm> next_;
    std::atomic<int>& seen_;
};

void an_actor_can_send_to_another() {
    std::atomic<int> last{0};
    std::atomic<int> seen{0};
    spire::Scheduler scheduler;

    auto sink = scheduler.spawn<Alarm, Sink>("sink", 4096, 5, tskNO_AFFINITY, last);
    auto sampler = scheduler.spawn<Reading, Sampler>("sampler", 4096, 5, tskNO_AFFINITY, sink, seen);
    check(scheduler.size() == 2, "two actors, two mailboxes");

    check(scheduler.start(), "the system starts");
    check(sampler.send(Reading{42}), "a reading goes in at one end");
    check(wait_for([&] { return last.load() == 42; }), "and comes out at the other");
    check(seen.load() == 42, "having been handled by the actor in between");

    scheduler.stop();
    spire_fake_join_tasks();
}

// --- 4. a full mailbox refuses rather than blocks -----------------------------------------------

class Blocking final : public spire::Actor<Add> {
public:
    explicit Blocking(std::atomic<bool>& gate) : gate_(gate) {}

protected:
    void on_message(const Add&) override {
        while (!gate_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    std::atomic<bool>& gate_;
};

void a_full_mailbox_refuses_rather_than_blocks() {
    std::atomic<bool> gate{false};
    spire::Scheduler scheduler;

    auto blocking = scheduler.spawn<Add, Blocking>("blocking", 4096, 5, tskNO_AFFINITY, gate);
    check(scheduler.start(), "the system starts");

    // One message is taken and the handler sits on it; the rest fill the mailbox, and then there is
    // no room. Which of the 32 lands where is a race, so the assertions are about both outcomes
    // existing rather than about which index failed.
    int accepted = 0;
    int refused = 0;
    for (int i = 0; i < 32; ++i) {
        if (blocking.send(Add{1})) {
            ++accepted;
        } else {
            ++refused;
        }
    }
    check(accepted > 0, "the mailbox took what it had room for");
    check(refused > 0, "and refused the rest: a full mailbox is a refusal, not a wait");

    gate.store(true);
    check(wait_for([&] { return blocking.waiting() == 0; }), "the actor drains once it is let go");

    scheduler.stop();
    spire_fake_join_tasks();
}

// --- 5. an actor can post to itself -------------------------------------------------------------

struct Kick {
    int remaining = 0;
};

class Tickler final : public spire::Actor<Kick> {
public:
    explicit Tickler(std::atomic<int>& deliveries) : deliveries_(deliveries) {}

protected:
    void on_message(const Kick& kick) override {
        ++deliveries_;
        if (kick.remaining > 0) {
            self().send(Kick{kick.remaining - 1});
        }
    }

private:
    std::atomic<int>& deliveries_;
};

void an_actor_can_post_to_itself() {
    std::atomic<int> deliveries{0};
    spire::Scheduler scheduler;

    auto tickler = scheduler.spawn<Kick, Tickler>("tickler", 4096, 5, tskNO_AFFINITY, deliveries);
    check(scheduler.start(), "the system starts");
    check(tickler.send(Kick{3}), "one kick goes in");
    check(wait_for([&] { return deliveries.load() == 4; }),
          "and the actor re-posts to itself until the kick is spent");
    check(tickler.waiting() == 0, "with nothing left in its mailbox");

    scheduler.stop();
    spire_fake_join_tasks();
}

}  // namespace

int main() {
    a_message_reaches_the_actor();
    a_refused_actor_starts_nothing();
    an_actor_can_send_to_another();
    a_full_mailbox_refuses_rather_than_blocks();
    an_actor_can_post_to_itself();

    spire_fake_join_tasks();
    if (failures != 0) {
        std::printf("%d case(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
