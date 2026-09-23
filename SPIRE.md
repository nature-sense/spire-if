# spire-idf

<!--
  What this library provides, and **how it is intended to be used**.

  This file is the contract between whoever wrote this library and the model that will build against
  it — a component added here, or an application built on it. It is where an *architecture* lives, and
  the tool knows nothing about actors, tasks, mailboxes or protocols. Keep it true as components
  arrive; an invented rule is worse than a missing one.
-->

## Provides

**The framework** — three components, already here, and not mandatory:

- **`ramen`** — the **dataflow** framework (upstream `Zubax/ramen`, MIT, C++20): typed ports
  (`ramen::Pusher` / `Pushable` / `Puller` / `Pullable`), `>>` to wire them, and topics. Pushing is
  **synchronous and inline**: a producer's `Task` runs every wired behaviour before its `Pusher`
  returns. There is no scheduler, no mailbox and no runtime — one task runs a whole chain.
- **`actors`** — the **classical actor** framework: a **scheduler** that owns the actors and stops them
  in reverse, typed **refs** to send to, and `on_message`. An actor is a mailbox (a FreeRTOS queue), a
  task of its own, and a message type; a sender holds an `ActorRef<Message>`, never the actor, so a
  slow actor cannot stall a fast one and the order between two actors is a queue's order:

  ```cpp
  struct Add { int amount = 0; };

  class Counter final : public spire::Actor<Add> {
  public:
      explicit Counter(std::atomic<int>& total) : total_(total) {}
  protected:
      void on_message(const Add& message) override { total_ += message.amount; }
  private:
      std::atomic<int>& total_;
  };

  spire::Scheduler scheduler;                                    // owns them, stops them in reverse
  auto counter = scheduler.spawn<Add, Counter>("counter", 4096, 5, tskNO_AFFINITY, total);
  scheduler.start();                                             // nothing runs before this
  counter.send(Add{3});
  scheduler.stop();
  ```

  Spawn everything and wire it **before** `start()` — a message posted early waits in its mailbox,
  which is what makes wiring order irrelevant. An actor that refuses in `init()` starts nothing, and
  a full mailbox refuses the sender rather than blocking it.
- **`toolkit`** — `spire::Task`: one loop, on a task of its own, with a stated stack, priority and
  core. It is the **only** thing the two frameworks share: neither requires the other, and a component
  requires neither.

**Components** — the payload. Add lines here as they arrive:

<!-- One line per component: what it is, and what it is for. -->

## Choosing a framework

The two are alternatives, not layers, and the choice is made **per application**:

- **Streaming → `ramen`.** Frames, samples, readings, a pipeline: a value moves through stages.
  Inline dispatch is simpler and faster than a queue, and one task runs the whole chain — so give the
  producer's task a stack that holds its consumers.
- **Many interacting agents → `actors`.** A unit that reacts to commands, timers and events from
  several sources, holding state of its own: post a message and get on with something else.

Neither is required. A library of plain components that uses neither is a valid library — and a
component must never assume which framework it is used under.

## How to use it

**A component holds a device or an algorithm, and nothing else.** It takes a bus handle, or its
inputs, and it knows no pins, no addresses, no `Task` and no framework — that is what makes it
reusable and what makes it testable on a host.

**An application holds the composition**: the board facts (pins, bus, addresses), the concrete
`spire::Task`s, the wiring, and the **actors**. An actor is not in a component and does not have to
be: it is the few lines that wrap a component, add ports or a mailbox, and own a loop.

**The lifecycle is the same in both frameworks**, so an application reads the same either way:

```
constructed with its configuration   // the board facts go here, and only here
bool init();                         // takes no arguments; false refuses
void shutdown();                     // releases what it holds
```

Start in the order actors were added (a consumer exists before its producer pushes), stop in reverse.

**Wire before starting.** RAMEN links at the point of `>>`, so a `Pusher` connected after a producer's
task begins silently drops everything pushed before the link was made — which reads as a sensor that
never reports.

### Build requirements a component cannot state itself

- **C++20**, because `ramen` does not compile without it. Nothing needs adding for a chip build —
  ESP-IDF compiles C++ at `-std=gnu++2b` for chip targets — but anything built **outside** IDF (a host
  test, a host tool) has to set it itself.
- Anything else this library needs that no component can state: a config symbol, a compiler flag, a
  partition-table entry. Say it here, because nothing else in the tree carries it.

## Not provided

- **A scheduler, a message bus, an event loop.** A `Task` and a `Pusher`, or a `Task` and a mailbox,
  are the whole of the concurrency story, deliberately.
- **A container, a registry, or a composition helper.** The application owns its own: the actors, the
  order they start in, and what is wired to what.
- **Devices and boards.** A driver is its own component and a board support package belongs to the
  *application* — this library stays off the shelf.
