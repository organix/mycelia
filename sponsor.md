# Sponsored Actor Configurations

A _Configuration_ is a collection of Actors
that form a failure-domain.
A _Sponsor_ is the entity responsible for
managing Actor resources.
All the resources an Actor uses;
including processor time, memory storage, and communications;
are controlled by a Sponsor.
Depending on the design of the system,
the relationship between Configurations and Sponsors
may be 1-to-1, 1-to-Many, Many-to-1, or Many-to-Many.

Each message/event processed by an Actor
within a Configuration
constitutes a transactional update
to the Configuration.
The _Effects_ produced by the Actor
are applied in an all-or-nothing manner.
The Configuration is in a consistent state
between message/event dispatches.
This can provide a stable checkpoint
for persistence, suspension, migration, and restart.

If the Actor cannot obtain enough resources
from the Sponsor
to finish handling the message/event,
the transaction must abort,
consuming the message/event
but causing no Effects.
The Sponsor and/or Configuration is notified
when an Actor message/event transaction aborts.
A Sponsor may determine that it has insufficient
resource reserves to handle a message/event
before dispatching it to the Actor for processing
without changing the semantics.

## Configuration/Sponsor Life-Cycle

For simplicity, we will consider a 1-to-1 Configuration/Sponsor design.
A Configuration/Sponsor is created with an initial pool of resources
and a _Controller_ to whom it reports life-cycle events.
From the perspective of the Controller,
the new Configuration/Sponsor is a _Peripheral_.

The Controller can send the following events to a Periperal:

  * Initiate
  * Terminate
  * Suspend
  * Resume
  * Add-Resources
  * Query-Resources

The Peripheral can send the following events to its Controller:

  * Completed
  * Exhausted
  * Report-Resources

### Initiate

The Controller sends work to the Peripheral
in the form of a _Behavior_.
The Behavior is described the same way it would be
when designated as the Behavior of an Actor.

### Terminate

The Controller can cancel Peripheral work-in-progress.
The Peripheral responds with a Completed or Exhausted event.

### Suspend

The Controller can pause Peripheral work-in-progress
without destroying Peripheral resources.
The Peripheral responds with a Completed or Exhausted event.

### Resume

The Controller can continue Peripheral work-in-progress (if any).

### Add-Resources

The Controller can provide additional resources for the Peripheral to use.
The Peripheral responds with a Report-Resources event.

### Query-Resources

The Controller can request a snapshot of Peripheral resources.
The Peripheral responds with a Report-Resources event.

### Completed

The Peripheral can report to the Controller
that there is no more work-in-progress in the Peripheral.

### Exhausted

The Peripheral can report to the Controller
that there _is_ work-in-progress in the Peripheral,
but the Peripheral does not have sufficient resources
to complete the work.

### Report-Resources

The Peripheral can report to the Controller
a snapshot of the resources managed by the Peripheral.
Note that by the time the Controller receives this event,
the available resources in the Peripheral may have changed.
