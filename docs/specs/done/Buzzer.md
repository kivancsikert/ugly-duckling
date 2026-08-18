# Buzzer Support (MK12)

## Background

MK12 adds a passive buzzer on GPIO 23 (BUZZER). The buzzer and the two
DRV8848 motor channels share a common load-enable pin, LOADEN (GPIO 22).
LOADEN must be driven HIGH whenever **any** of the three actuators — motor A,
motor B, or the buzzer — is active.

Today, `Drv8833Driver` manages LOADEN internally with ad-hoc logic: each
inner `Drv8833MotorDriver` tracks a `sleeping` flag, and the outer driver
drives the pin LOW only when both motors are sleeping. This doesn't extend to
a third participant (the buzzer), and the sleep-management code is duplicated
in a different form across the other motor drivers (`Drv8874Driver`,
`Drv8801Driver`).

## Step 1 — SharedEnable

Extract the shared-resource pattern into a reusable, thread-safe abstraction.

### Concept

A `SharedEnable` manages a binary output with multiple independent clients.
The output is **active** when at least one client has acquired it, and
**inactive** when every client has released.

Each client holds a `Handle` obtained from the `SharedEnable`. The handle
provides `acquire()` and `release()`, both idempotent — acquiring an
already-acquired handle or releasing an already-released handle is a no-op.
A handle's destructor releases it as a safety net.

### Interface sketch

```cpp
class SharedEnable : public std::enable_shared_from_this<SharedEnable> {
public:
    class Handle {
    public:
        Handle(Handle&&);
        Handle& operator=(Handle&&);
        ~Handle();                   // releases if acquired

        void acquire();              // idempotent
        void release();              // idempotent
        bool isAcquired() const;

    private:
        friend class SharedEnable;
        // back-pointer to parent + acquired flag
    };

    // Factory methods
    static std::shared_ptr<SharedEnable> noOp();
    static std::shared_ptr<SharedEnable> forActiveHighPin(const PinPtr& pin);
    static std::shared_ptr<SharedEnable> forActiveLowPin(const PinPtr& pin);

    Handle createHandle();

private:
    void update();                   // called by handles on acquire/release
    std::function<void(bool)> actuate;
    std::vector<Handle*> handles;    // non-owning, for polling
    std::mutex mutex;
};
```

`actuate(true)` is called on the transition from zero to one acquired
handles; `actuate(false)` on the transition from one to zero.

### Actuation modes

| Factory                 | `actuate(true)` | `actuate(false)` | Use case                  |
| ----------------------- | --------------- | ---------------- | ------------------------- |
| `noOp()`                | nothing         | nothing          | Drivers with no sleep pin |
| `forActiveHighPin(pin)` | pin → HIGH      | pin → LOW        | LOADEN on MK10–12         |
| `forActiveLowPin(pin)`  | pin → LOW       | pin → HIGH       | (future, if needed)       |

### Motor driver integration

#### DRV8833 / DRV8848

`Drv8833Driver::create()` changes signature: replaces the `sleepPin`
parameter with a `std::shared_ptr<SharedEnable>`.

- The outer `Drv8833Driver` no longer owns the sleep pin or the
  `updateSleepState()` / `setSleepState()` logic.
- Each inner `Drv8833MotorDriver` receives a `SharedEnable::Handle`
  (created by `Drv8833Driver::initMotors()` via `enable->createHandle()`).
- `wakeUp()` → `handle.acquire()`; `sleep()` → `handle.release()`.
- The `canSleep` constructor flag goes away — a `noOp()` SharedEnable
  provides the same behavior.

If no `sleepPin` was passed (MK4/MK5 devices where `sleepPin == nullptr`),
the device code passes `SharedEnable::noOp()` instead.

#### DRV8874

Replaces direct `sleepPin->digitalWrite(…)` calls with a
`SharedEnable::Handle`. The device code creates a single-client
`SharedEnable::forActiveHighPin(sleepPin)` and passes it to the driver.

#### DRV8801

Same treatment as DRV8874. The `sleepPin` manipulation moves into a
`SharedEnable`; the separate `enablePin` stays as-is (it's a per-phase
signal, not a shared resource).

## Step 2 — BuzzerDriver

### Hardware

A passive buzzer requires a square wave at an audible frequency to produce
sound. Volume is maximum at 50 % duty cycle. A typical resonant frequency
for small passive buzzers is 2–4 kHz; the exact value can be tuned for the
specific component on MK12.

### Interface sketch

```cpp
class BuzzerDriver {
public:
    BuzzerDriver(
        const std::shared_ptr<PwmManager>& pwm,
        const InternalPinPtr& buzzerPin,
        std::shared_ptr<SharedEnable> enable);

    void buzz(std::chrono::milliseconds duration, double duty = 0.5);

private:
    static constexpr uint32_t BUZZER_FREQ = 4000;       // Hz, tunable
    static constexpr ledc_timer_bit_t BUZZER_RES = LEDC_TIMER_8_BIT;

    PwmPin& channel;
    SharedEnable::Handle enableHandle;
    esp_timer_handle_t stopTimer;

    void stop();
};
```

### Behavior

`buzz(duration)`:

1. `enableHandle.acquire()` — ensures LOADEN is HIGH.
2. `channel.write(maxValue * duty)` — set the requested duty cycle.
3. Starts a one-shot `esp_timer` for `duration`.
4. When the timer fires, calls `stop()`.

`stop()`:

1. `channel.write(0)` — silence.
2. `enableHandle.release()` — allows LOADEN to go LOW if nothing else
   needs it.

If `buzz()` is called while a previous buzz is still playing, the timer
is restarted with the new duration (extends or shortens the current buzz).

### Lifecycle

The `BuzzerDriver` owns its `esp_timer` and cleans it up on destruction.
The PWM channel stays registered (PwmManager doesn't support
deallocation), but with duty 0 it consumes no power.

## MK12 wiring

In `UglyDucklingMk12::registerDeviceSpecificPeripheralFactories()`:

```cpp
// Shared load enable for motors and buzzer
auto loadEnable = SharedEnable::forActiveHighPin(LOADEN);

// Motors (LOADEN shared)
auto motorDriver = Drv8848Driver::create(
    services.pwmManager,
    DAIN1, DAIN2, DBIN1, DBIN2,
    NFAULT, loadEnable);

// Buzzer (LOADEN shared)
auto buzzer = std::make_shared<BuzzerDriver>(
    services.pwmManager, BUZZER, loadEnable);
```

MK10 and MK11 don't have a buzzer, but their motor driver call changes
from passing `LOADEN` as a raw pin to wrapping it in a `SharedEnable`:

```cpp
auto loadEnable = SharedEnable::forActiveHighPin(LOADEN);
auto motorDriver = Drv8848Driver::create(
    services.pwmManager,
    DAIN1, DAIN2, DBIN1, DBIN2,
    NFAULT, loadEnable);
```

## Future work

- **Tones**: `buzz(frequency, duration)` — change the PWM frequency per
  call. Requires either reconfiguring the LEDC timer or pre-allocating
  timers for common frequencies.
- **Melodies / sequences**: a small DSL or note array that the driver
  plays asynchronously (chain of esp_timer callbacks or a dedicated task).
- **Volume control**: vary duty cycle below 50 % for quieter output.
- **Peripheral integration**: expose the buzzer as an `IPeripheral` so it
  can be triggered via MQTT commands and telemetry events.
