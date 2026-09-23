// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 NatureSense

#pragma once

#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <actors/actor.hpp>

namespace spire {

/// The **scheduler**: it owns the actors, and therefore owns the thing that has to be stopped.
///
/// This is the part that makes an actor *system* rather than a base class with a queue. It holds
/// every actor it spawned — the object, the mailbox and the task — starts them in the order they
/// were spawned, and stops them in reverse. Nothing else in the framework needs to know how many
/// there are or in what order they should go away, which is the point: the only thing that reliably
/// knows the lifecycle of a system is the thing that created it.
///
/// **Start after spawning, never before.** Until `start()` nothing runs — no task exists — so a
/// message sent to a ref before the system starts waits in its mailbox and is handled on the first
/// iteration. That is deliberate: it makes wiring order irrelevant, and it means a half-built system
/// cannot run half-built.
///
/// **Lifecycle.** Construct → `spawn` (as many as the product has) → `start` → run → `stop`. A
/// scheduler is not restartable: `stop()` releases the actors and closes the mailboxes, and refs
/// held past it go invalid rather than dangling, which is what `ActorRef::valid()` is for.
///
/// The type-erased half — the `Entry` that actually holds an actor, its mailbox and its task — lives
/// in `src/scheduler.cpp`, because everything interesting in it is a FreeRTOS call or a `Task` call
/// and neither belongs in a header. What has to be here is the part that is a template.
class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// Create an actor, give it a mailbox, and return the address to send to.
    ///
    /// `Message` is the actor's message type, `A` the actor's type, and the rest is its constructor's
    /// arguments — so an actor that needs a peer's ref or a board constant is constructed with it
    /// rather than reaching for a global. `spawn` is a no-op returning an invalid ref once the system
    /// has started: starting one actor in the middle of a running system is a lifecycle nobody asked
    /// for, and refusing is how a caller finds out.
    template <typename Message, typename A, typename... Args>
    ActorRef<Message> spawn(const char* name,
                            std::uint32_t stack_bytes,
                            UBaseType_t priority,
                            BaseType_t core,
                            Args&&... args) {
        using Base = Actor<Message, A::mailbox_depth>;
        static_assert(std::is_base_of_v<Base, A>,
                      "an actor is a subclass of spire::Actor<Message>, with the same message type "
                      "`spawn` was told about");
        auto* actor = new A(std::forward<Args>(args)...);
        if (actor == nullptr) {
            return ActorRef<Message>{};
        }
        auto mailbox = attach(name, stack_bytes, priority, core, Base::mailbox_depth,
                              sizeof(Message), actor, &Base::template hook_init<A>,
                              &Base::template hook_message<A>, &Base::template hook_shutdown<A>,
                              &Scheduler::destroy_of<A>);
        if (!mailbox) {
            // Nothing to delete here: `attach` took ownership of `actor` and released it on the way
            // out. An invalid ref is a refusal a caller can see — `valid()` is false and `send()`
            // fails — rather than a crash waiting to happen.
            return ActorRef<Message>{};
        }
        ActorRef<Message> ref(mailbox);
        // Through the base pointer: the naming class for this private access is the base, which is
        // the class that grants the friendship.
        static_cast<Base*>(actor)->self_ = ref;
        return ref;
    }

    /// Start every actor, in the order it was spawned.
    ///
    /// All or nothing: if one refuses (`init()` says no, or FreeRTOS could not give it a task), the
    /// ones already started are stopped in reverse before `false` is returned — a refused start must
    /// not leave half a system running, because that is the state nobody can diagnose.
    bool start();

    /// Stop every actor, in **reverse** the order it was spawned, then release them. Idempotent.
    void stop();

    /// Whether the system has been started and not stopped.
    bool running() const;

    /// How many actors were spawned.
    std::size_t size() const;

private:
    struct Entry;

    /// The non-template half of `spawn`: everything that does not depend on the message type, and
    /// therefore everything that can live in a translation unit.
    std::shared_ptr<Mailbox> attach(const char* name,
                                    std::uint32_t stack_bytes,
                                    UBaseType_t priority,
                                    BaseType_t core,
                                    std::size_t depth,
                                    std::size_t message_size,
                                    void* actor,
                                    bool (*init)(void*),
                                    void (*dispatch)(void*, const void*),
                                    void (*shutdown)(void*),
                                    void (*destroy)(void*));

    template <typename A>
    static void destroy_of(void* actor) {
        delete static_cast<A*>(actor);
    }

    std::vector<std::unique_ptr<Entry>> entries_;
    bool started_ = false;
};

}  // namespace spire
