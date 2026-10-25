# My Tinker Board Project: Three Operating Systems on One Small Computer

*A beginner's guide to how this project was built, step by step, from the first boot to the end.*

---

## Before you start

This guide tells the whole story of the project, from the very first boot problem to the finished
system. You don't need to be an expert to read it. Every new word is explained the first time it
appears, and there's a word list at the end ([Chapter 14](#chapter-14--word-list)).

If you want the exact technical details (register addresses, full patch listings, every hash), they
are in `TECHNICAL-REFERENCE.md` in the same folder. This guide is the friendly version.

**How to read it:**
- Chapters 1–3 explain the ideas and the equipment. Read these first.
- Chapters 4–9 tell the story in order. Each step answers four questions:
  *What did we want? What went wrong? Why? How did we fix it?*
- Chapters 10–13 are for using the finished system and getting out of trouble.

---

## Contents

1. [The big idea](#chapter-1--the-big-idea)
2. [The equipment](#chapter-2--the-equipment)
3. [The building blocks, explained simply](#chapter-3--the-building-blocks-explained-simply)
4. [Setting up the build PC](#chapter-4--setting-up-the-build-pc)
5. [Part 1 – Getting Xen to start](#chapter-5--part-1--getting-xen-to-start)
6. [Part 2 – Making it a real system](#chapter-6--part-2--making-it-a-real-system)
7. [Part 3 – Adding Android](#chapter-7--part-3--adding-android)
8. [Part 4 – Polishing it](#chapter-8--part-4--polishing-it)
9. [Part 5 – The car project](#chapter-9--part-5--the-car-project)
10. [How the finished system works](#chapter-10--how-the-finished-system-works)
11. [How to build, flash and use it](#chapter-11--how-to-build-flash-and-use-it)
12. [Every problem we hit, in plain words](#chapter-12--every-problem-we-hit-in-plain-words)
13. [What doesn't work yet, and how to recover](#chapter-13--what-doesnt-work-yet-and-how-to-recover)
14. [Word list](#chapter-14--word-list)

---

## Chapter 1 – The big idea

### What we built

We took one small, cheap computer board, the **ASUS Tinker Board R2.0**, and made it run
**three operating systems at the same time**:

1. **Linux** (made with Yocto). This is the "boss". It controls everything else.
2. **Android 7**. The same system phones use. Here it runs without a screen.
3. **FreeRTOS**. A tiny "real-time" system that must react on time, every time.

They all run together, on the same board, and they can't disturb each other.

### Why would anyone do that?

Think about a modern car. It has a computer for the music and maps, a computer for the
dashboard, and computers for safety (like the brakes). Car makers now want to put all of those on
**one chip** to save money and space. But the safety part must never be slowed down or crashed by
the music app.

The trick is a program called a **hypervisor**. It sits underneath all the operating systems and
gives each one its own share of the computer, like walls between flats in a building. We used the
hypervisor called **Xen**.

```
     Linux (boss)        Android (apps)      FreeRTOS (safety, on time)
   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
   │              │    │              │    │              │
   └──────────────┘    └──────────────┘    └──────────────┘
   ┌────────────────────────────────────────────────────────┐
   │           Xen hypervisor (the walls between them)      │
   └────────────────────────────────────────────────────────┘
   ┌────────────────────────────────────────────────────────┐
   │      Tinker Board: 4 CPU cores, 2 GB memory            │
   └────────────────────────────────────────────────────────┘
```

### What you get at the end

You put the SD card in the board and switch it on. Without pressing anything:

- Xen starts, then Linux, then Android and FreeRTOS start by themselves.
- FreeRTOS runs a task exactly every 10 milliseconds. We measured it: it's never more than
  0.04 ms early or late, even while Android is busy.
- A pretend car sends its speed and engine speed (rpm) 100 times a second. FreeRTOS checks every
  message and warns about speeding, about the engine revving too high, and about the signal going
  missing.
- If you plug in an HDMI monitor, a car dashboard shows the speed, rpm and warning lights.

The project took four days, 2026-10-22 to 2026-10-25.

---

## Chapter 2 – The equipment

### What you need

- **ASUS Tinker Board R2.0.** It has a Rockchip **RK3288** chip with 4 CPU cores and 2 GB of memory.
- **A micro-SD card**, 32 GB. The whole system lives on this card.
- **A USB-to-serial adapter** (3.3 V). This lets the PC read the board's text messages and type
  into it, even before anything has started. We call this the **console** or **UART**.
- **A good power supply**: 5 V, 3 A. A weak one causes strange freezes (we saw two).
- **A Linux PC.** We used Ubuntu. It builds the software and writes the SD card.
- Optional: an HDMI monitor, an Ethernet cable, a USB CAN adapter (for reading a real car).

### Connecting the serial adapter (important!)

This was the first thing that went wrong, so take care here. The board has a row of 40 pins. The
console is on pins **32, 33 and 34**, not on pins 8 and 10 as many websites suggest for other boards.

```
   Board pin 32 (sends)    ───────►  adapter RX (receives)
   Board pin 33 (receives) ◄───────  adapter TX (sends)
   Board pin 34 (ground)   ────────  adapter GND
```

Notice that "send" goes to "receive" and the other way round.

On the PC, open the console with:

```
picocom -b 115200 /dev/ttyUSB0
```

`115200` is the speed. Press `Ctrl-a` then `Ctrl-x` to quit picocom.

> **Remember:** when the board starts, it shows "Hit any key to stop autoboot" for a moment. If you
> press a key then, it stops and waits. So after switching on, **don't touch the keyboard** until
> you see `login:`.

---

## Chapter 3 – The building blocks, explained simply

Here are all the pieces, from the bottom up, the order in which they start.

### The boot ROM and U-Boot (the "starter")

When you switch on, a tiny program built into the chip (the **boot ROM**) looks at the SD card and
loads **U-Boot**. U-Boot is a **bootloader**: its only job is to prepare the hardware and start the
next program. Think of it as the starter motor of a car.

### Xen (the hypervisor)

Xen is started by U-Boot. It takes over the CPU cores and memory and shares them out.

One thing is important to know. An ARM processor has different "levels of power", called **modes**.
Normal programs run at the lowest level. Operating systems run one level higher. A hypervisor needs
an even higher level, called **HYP mode**. U-Boot has to hand the CPU over *in HYP mode*, or Xen
refuses to start. This turned out to be our biggest problem (Chapter 5).

### Domains (the virtual machines)

Xen calls each operating system it runs a **domain**.

- **Domain-0 ("dom0")** is the boss domain. It's Linux, and it can create, stop and watch the
  others. It is the only one that touches the real hardware (SD card, network, USB).
- The other domains are **guests**. Ours are `android1` (Android) and `rtos1` (FreeRTOS).

### Yocto (the "factory" that builds Linux)

You don't download Linux for this board as a ready file. You **build** it with a tool called
**Yocto**. Yocto reads "recipes" (small text files) that say where to download each program, how to
compile it and where to put it. At the end it produces one file you can write to the SD card: the
**image** (`.wic` file).

Recipes are grouped into **layers**. We didn't change anyone else's layers. All our own work is in
one layer of our own called **`meta-tinker-xen`**. That keeps everything tidy: if you delete our
layer, you're back to the original.

Words you'll see:
- **recipe** (`.bb` file): how to build one program.
- **bbappend** (`.bbappend` file): "take that other recipe, and also do this". We use it to add our
  fixes to U-Boot, Xen and Linux without editing their files.
- **patch** (`.patch` file): a small list of changes to source code ("in this file, replace these
  lines with those lines").
- **bitbake**: the command that runs the factory, e.g. `bitbake tinker-xen-test-image`.

### Real-time and FreeRTOS

"Real-time" doesn't mean "fast". It means **on time, every time**. An airbag must fire within a few
milliseconds; being fast "on average" isn't good enough. **FreeRTOS** is a very small operating
system designed for this. Ours runs a task every 10 ms and measures how accurate it is.

### Schedulers (who gets the CPU when)

When several programs want the same CPU, a **scheduler** decides who goes next. Xen has several:

- **credit2**: fair sharing. Good for Linux and Android.
- **RTDS**: real-time. You say "this guest gets 200 microseconds of CPU in every 1 millisecond,
  guaranteed", and Xen keeps that promise.

We gave FreeRTOS its **own CPU core** (core number 3) with RTDS, and the other three cores to Linux
and Android with credit2. A group of cores with its own scheduler is called a **cpupool**.

### CAN bus

Cars send messages between their computers on a two-wire network called **CAN**. Each message has
a number (its ID) and up to 8 bytes of data. Linux can use CAN like a network card. It even has a
*pretend* one, called **vcan**, that we used for testing.

---

## Chapter 4 – Setting up the build PC

These steps happen on the PC, not on the board.

**Step 1 – Download Yocto and the layers it needs.** All on the "kirkstone" version:

```
mkdir ~/yocto-rk3288 && cd ~/yocto-rk3288
git clone -b kirkstone git://git.yoctoproject.org/poky
git clone -b kirkstone https://github.com/openembedded/meta-openembedded
git clone -b kirkstone git://git.yoctoproject.org/meta-virtualization
git clone -b kirkstone git://git.yoctoproject.org/meta-arm
git clone -b kirkstone git://git.yoctoproject.org/meta-rockchip
```

What each one is:
- `poky`: Yocto itself.
- `meta-openembedded`: lots of extra programs.
- `meta-virtualization`: Xen.
- `meta-arm`: the ARM compiler.
- `meta-rockchip`: support for the Tinker Board's chip.

**Step 2 – Make a build folder and tell Yocto about the layers:**

```
source poky/oe-init-build-env build
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-arm/meta-arm-toolchain ../meta-arm/meta-arm
bitbake-layers add-layer ../meta-rockchip
bitbake-layers add-layer ../meta-openembedded/meta-filesystems
bitbake-layers add-layer ../meta-openembedded/meta-python
bitbake-layers add-layer ../meta-openembedded/meta-networking
bitbake-layers add-layer ../meta-virtualization
bitbake-layers add-layer ../meta-tinker-xen
```

**Step 3 – Tell Yocto which board and which features.** Open `build/conf/local.conf` and make sure
it has these two lines:

```
MACHINE = "tinker-board"
DISTRO_FEATURES:append = " virtualization xen"
```

**Step 4 – Build:**

```
bitbake tinker-xen-test-image
```

The first build takes several hours, because it compiles everything from scratch. Later builds only
redo what changed and take minutes. When it works, the last line says something like
`all succeeded`. The SD card file ends up in
`build/tmp/deploy/images/tinker-board/tinker-xen-test-image-tinker-board.wic`.

> **Tip:** before changing anything, we saved a copy of the original, working image in
> `diagnostics/baseline/`. If everything goes wrong, you can always go back to it.

---

## Chapter 5 – Part 1 – Getting Xen to start

This was the hardest part. Xen had never run on this chip, and seven different things stood in the
way. We removed them one by one.

### Step 1 – The "freeze" that wasn't our image

**What we saw:** the board stopped after a line listing the SD card's partitions
(`mmcblk0: p1 … p9`).

**Why:** we counted. Our image has 7 partitions, but the message showed 9, and the bootloader
version was different too. The card held a completely different, older project, not our Yocto
image. That old project had a hang ~2 seconds into Linux, which we met again (and fixed) in Step 5.

**Fix:** write our own image to the card.

### Step 2 – Typing did nothing

**What we saw:** the board's messages appeared on the PC, but nothing we typed reached the board.

**Why:** the adapter's wires were on pins 8 and 10, which is a different serial port.

**Fix:** move the wires to pins 32, 33 and 34 (Chapter 2). Then we could log in.

### Step 3 – "Xen must be entered in NS Hyp mode"

**What we saw:** Linux on its own showed this line:

```
CPU: All CPU(s) started in SVC mode.
```

and Xen, started by hand from U-Boot, stopped immediately:

```
- Xen must be entered in NS Hyp mode -
- Please update the bootloader -
- Boot failed -
```

**Why:** remember the "levels of power" from Chapter 3. U-Boot handed over the CPU in "SVC mode"
(the operating-system level) and in "secure" state. Xen needs "HYP mode" in "non-secure" (NS)
state. U-Boot can do this switch for some chips, but nobody had ever written that code for the
RK3288.

**Fix:** we wrote it ourselves, in a U-Boot patch
(`0001-rockchip-rk3288-allow-booting-in-non-secure-HYP-mode.patch`). It does four things:

1. It tells U-Boot that this chip *can* do the switch.
2. It tells U-Boot where the interrupt controller (the GIC) is.
3. It opens up the chip's devices to "non-secure" programs. Normally many devices only listen to
   "secure" programs, so Xen and Linux would be locked out.
4. It **keeps the old behaviour as the default**. The switch only happens when our Xen start script
   asks for it, so plain Linux still boots exactly as before.

> **Safety note:** a broken bootloader can stop a board from starting. So we tested every new U-Boot
> by sending it over the serial cable straight into memory (`loady`) and starting it from there.
> Nothing was written to the card until it worked. Since the board starts from the SD card, the worst
> case is always "write the card again".

### Step 4 – Silence after "Starting kernel"

**What we saw:** after the patch, the board printed `Starting kernel ...` and then nothing at all.

**Why:** after a lot of detective work (we made the code print a letter at each step and saw it stop
at "E"), we found the cause. When the CPU enters HYP mode, it uses a settings register that nobody
had set, so it contained random leftovers. Those leftovers switched on features that made the very
first instruction crash.

**Fix:** set that register to a clean, known value just before switching (`0x30c50830`). Added to
the same U-Boot patch.

### Step 5 – Xen didn't know the processor

**What we saw:** `Missing processor info: 410fc0d1`.

**Why:** Xen keeps a list of processors it knows. The RK3288's CPU type (Cortex-A12/A17) wasn't on
the list, not even in the newest Xen.

**Fix:** a Xen patch (`0001`) that adds it, set up the same way as its close cousin, the Cortex-A15.

### Step 6 – Linux (dom0) crashed in two places

**Crash 1:** Linux crashed while reading a cache-control register called L2CTLR. Under Xen, reading
that register is "caught" by Xen, and Xen didn't know what to answer, so it reported an error.
**Fix:** Xen patch `0002`. Xen now answers with the real value, the same way the KVM hypervisor
does.

**Crash 2:** Linux crashed when touching a DMA controller (a helper that copies memory). It was
using the controller's "secure-only" address. **Fix:** our start script tells Linux to use the
controller's normal address instead.

### Step 7 – The famous 2-second hang

**What we saw:** Linux always stopped about 2 seconds after starting. This was the same hang the old
project on the card had.

**Why:** this one took the longest. Xen printed its internal clock, and the number was **negative**.
Time was going backwards! We found that Linux, while starting, **resets a hardware timer** (timer7),
and on this chip that timer drives the main system clock. Linux didn't mind, but Xen's clock jumped
back to zero, so every "wake me up in 1 second" alarm in Xen would never ring. Everything waited
forever.

**Fix:** a small Linux patch: "if timer7 is already running, leave it alone". U-Boot always starts
it, so Linux now never resets it.

### Result of Part 1

```
/sys/hypervisor/type  →  xen
```

Linux was running **on top of Xen** for the first time on this board.

---

## Chapter 6 – Part 2 – Making it a real system

### Step 8 – The first guest

We installed Xen's control tools (`xl`) and created a small test Linux as a guest. Commands:

```
xl create /etc/xen/guest1.cfg     # start a guest
xl list                           # see all domains
xl console guest1                 # talk to it (Ctrl-] to come back)
xl destroy guest1                 # switch it off
```

It worked. (We removed this test guest later, when Android arrived.)

Once, the board restarted by itself the moment we started a guest. It never happened again. The
reset reason said "power-on", so it was most likely a weak moment of the power supply.

### Step 9 – A network between the domains

We built a **bridge**, a virtual network switch inside dom0, called `xenbr0`, with the address
`192.168.100.1`. Each guest gets a virtual network cable plugged into it. A ping between Linux and
the guest worked in both directions, in under 1 millisecond.

### Step 10 – FreeRTOS as a guest

**Why FreeRTOS and not Zephyr?** Zephyr's Xen support only works on 64-bit ARM, and our chip is
32-bit.

We wrote the small "glue" FreeRTOS needs to live inside Xen. Xen gives every guest a virtual
interrupt controller and a virtual timer, and our code uses them to make the 1 ms "tick" that drives
FreeRTOS. The guest prints its results as text.

The first runs showed three bugs:

| What we saw | Why | Fix |
|---|---|---|
| The same text line printed again and again | memory caches were off, so Xen read old data | switch the caches on |
| Each 10 ms period was really 10.15 ms | each tick was timed from "now", so small delays added up | time each tick from a fixed schedule instead |
| FreeRTOS used 100 % of its CPU | when idle, it spun in a loop | when idle, it now sleeps (`WFI` instruction) |

After that: **10 ms, accurate to about 0.008 ms**.

### Step 11 – The real-time scheduler

With everything sharing one core, FreeRTOS was sometimes 19 ms late while the other guest started.
That's terrible for real-time. So we switched FreeRTOS to the **RTDS** scheduler and gave it a
guarantee: *200 µs in every 1 ms*.

```
xl sched-rtds -d rtos1 -v all -p 1000 -b 200 -e 1
#   -p period 1000 µs, -b budget 200 µs, -e may use spare time too
```

Results:

| Situation | Before (credit2) | After (RTDS) |
|---|---|---|
| Nothing else happening | ±0.008 ms | ±0.01–0.02 ms |
| Another guest starting | up to 19.9 ms late | mostly under ±0.115 ms |
| Linux 100 % busy | – | ±0.007 ms |

### Step 12 – `reboot` didn't work

**What we saw:** typing `reboot` printed `Xen: Platform reset did not work properly!` forever.

**Why:** Xen didn't know how to reset this particular chip.

**Fix:** Xen patch `0003` teaches Xen about the RK3288. To reset, it writes the magic value `0xfdb9`
into the chip's reset register, the same thing Linux and U-Boot do. The first version couldn't find
that register. Our board is the "RK3288**W**" variant, and U-Boot renames the device on it, so the
patch now accepts both names.

### Step 13 – Starting Xen automatically

Until now we started Xen by typing commands into U-Boot. To make it automatic, we used something
U-Boot already does: at power-on it looks on the card's boot partition for a file called
`/boot.scr` and runs it. So we:

- put our Xen start script there as `/boot.scr`;
- made sure the image doesn't contain the file U-Boot would otherwise prefer
  (`/extlinux/extlinux.conf`);
- kept a "plain Linux without Xen" option in `/native/extlinux.conf`, used if Xen is missing.

We never used `saveenv`, so U-Boot's own settings were never changed.

### Step 14 – Using all 4 CPU cores

**What we saw:** `Failed to bring up CPU 1 (error -19)`. Xen used only one core.

**Why:** Xen starts the other cores by asking the firmware through a standard interface called
**PSCI** ("please switch on core 2, and start it at this address"). This board had no PSCI.

**Fix:** we added PSCI to U-Boot (patch `0002`). U-Boot leaves a tiny piece of itself in memory, and
when Xen asks to switch on a core, that piece:

1. powers the core up,
2. writes the start address and a magic number (`0xdeadbeaf`) into a "mailbox" the chip's boot ROM
   watches,
3. wakes the core.

**Problem on the first try:** only 2 cores came up. All the sleeping cores watch the *same*
mailbox, so the first request woke all of them at once. **Fix:** switch the extra cores fully off
first, so each request wakes exactly one.

Result:

```
(XEN) Using PSCI v1.0
(XEN) Brought up 4 CPUs
```

---

## Chapter 7 – Part 3 – Adding Android

### Step 15 – Why Android 7, and why a special kernel

Google publishes ready-made Android images for 32-bit ARM only up to Android 7. Building Android
yourself needs about 250 GB of disk, more than our PC had free.

Android 7 talks to its kernel through a part called **binder**, using an old 32-bit format. That old
format was removed from Linux after version 4.18, and our dom0 Linux is 5.15. So Android gets **its
own kernel, Linux 4.14**, built by the recipe `linux-android-guest`.

### Step 16 – Adapting Android to Xen

The downloaded image was made for the Android phone emulator. We changed a few things:

- **Disks:** Android's `/system` and `/data` live on two extra SD card partitions (p8 and p9). Xen
  passes them to Android as virtual disks.
- **Startup file:** a new `init.xen.rc` tells Android how to start inside Xen (mount the disks,
  start the network, enable adb).
- **Graphics:** the emulator's graphics files were removed, and Android uses its built-in software
  drawing.
- **Network:** Android gets the address `192.168.100.3`, and dom0 is its gateway to the outside.

You talk to Android from dom0 with **adb** (Android Debug Bridge):

```
adb connect 192.168.100.3:5555
adb shell
```

### Step 17 – Android kept crashing at start

Android's main program (`system_server`) crashed and restarted again and again. We fixed it in
three rounds:

1. **Error:** `Could not register INotify for /dev/input`.
   **Why:** there's no keyboard or touchscreen, so the input folder was never created.
   **Fix:** create the folder at startup (`mkdir /dev/input` in `init.xen.rc`).

2. **Error:** a `NullPointerException` in the alarm service.
   **Why:** Android 7 sets its alarms in a way that needs a special permission. Android only got
   that permission in version 8, and the old way (a `/dev/alarm` device) doesn't exist in Linux
   4.14.
   **Fix:** a small patch to the Android kernel that allows alarms for Android's "system" user group
   (number 1000).

3. **Error:** the USB hub kept resetting while adb was running.
   **Why:** the board's USB hub falls asleep to save power and fails to wake up properly.
   **Fix:** a rule that keeps the hub awake.

### Step 18 – A Xen crash, and a clever workaround

**What we saw:** with the RTDS scheduler on all 4 cores, Xen crashed while Android started:

```
Assertion '!unit_on_replq(svc)' failed at rt.c:587
```

**Workaround:** use RTDS only where we really need it. Cores 0–2 got the normal credit2 scheduler
(for Linux and Android), and core 3 got its own pool, called `rt`, with RTDS just for FreeRTOS. This
is actually a nicer design anyway. (The real cause of this crash came back later and was properly
fixed in Step 30.)

**Result:** Android started fully (`sys.boot_completed = 1`) with zero crashes, and FreeRTOS stayed
accurate to ±0.06 ms while Android was busy.

---

## Chapter 8 – Part 4 – Polishing it

### Step 19 – Small but important improvements

- **Quieter FreeRTOS.** It printed 3 lines per second on the shared console, so you couldn't type.
  Now it prints one summary line every 10 seconds.
- **Faster, final Xen.** We switched from the "debug" Xen (extra checks, lots of messages) to the
  normal "release" Xen. The release build left out the real-time scheduler by default, so we turned
  it back on (`CONFIG_SCHED_RTDS=y`).
- **Temperature sensor.** Under Xen, the temperature driver waited forever and gave no readings.
  That also meant no automatic shutdown if the chip overheated. The cause was a link to "CPU cooling"
  that can't exist under Xen. **Fix:** the start script removes that link. Now the readings work
  (about 35–42 °C).
- **FreeRTOS gets a proper console.** Instead of a debug-only trick, it now uses a shared memory
  "ring" like the other guests, so `xl console rtos1` works with the release Xen.
- **Hardware watchdog.** A **watchdog** is a timer that restarts the board unless somebody "pats" it
  regularly. Dom0 now pats it every 5 seconds. If the system freezes, nobody pats it, and the board
  restarts itself after about 45 seconds. We tested it: it restarted after 43 seconds.
- **Guests start by themselves.** A startup script (`xen-guests`) starts FreeRTOS and Android at
  boot, and stops them cleanly at shutdown.
- **No more console spam.** Linux tried to put a login prompt on the serial port, which belongs to
  Xen, and complained `respawning too fast`. Now it doesn't try.
- **Android network service refused to start.** Android's security system (SELinux) said
  `Service xen-net does not have a SELinux domain defined`. **Fix:** tell it which security label
  to use (`seclabel u:r:su:s0`).

### Step 20 – Final check of Part 4

We restarted, powered off and powered on several times. Every time:

1. Xen started with 4 cores.
2. Both guests started on their own.
3. Android was ready about 76 seconds later.
4. FreeRTOS stayed between 9961 and 10038 µs (the target is 10000).

### Step 21 – Trying a screen and touch for Android (switched off again)

We tried to give Android a screen and touch:

- dom0 showed Android's screen on the HDMI monitor;
- a USB touchscreen was passed through to Android;
- sound worked in dom0.

Some of it worked: Android drew its screen and received taps.

**But:** Android's apps need a 3D graphics feature (OpenGL ES 2.0) that this setup doesn't have. We
could force apps to draw without it, but then Android **froze after 1–2 minutes**. We traced it to
Android's timer interrupt getting stuck, but couldn't solve it.

**Decision (yours, option A):** go back to the stable, screen-less Android. The screen and touch
code stays in the project, switched off.

---

## Chapter 9 – Part 5 – The car project

We moved toward a real-time **car system**. To keep it safe, we followed one simple rule:

> **The board never sends anything to a car.** We use a *pretend* car. If it's ever connected to a
> real car, it only *listens*.

### Step 22 – A pretend car

The program **`vehicle-sim`** acts like a car and sends two CAN messages every 10 ms on the pretend
CAN bus `vcan0`:

- message `0x0C0`: speed (in steps of 0.01 km/h);
- message `0x0C1`: engine speed in rpm.

It "drives" a 60-second loop:

```
0–20 s   speed up from 0 to 135 km/h (changing gears)
20–35 s  cruise at 135 km/h  (too fast on purpose: the limit is 120)
35–50 s  brake to a stop
50–60 s  stand still (engine idling at 800 rpm)
```

It shifts late in 2nd gear on purpose, so the engine goes over 6000 rpm.

### Step 23 – Getting the messages into FreeRTOS

FreeRTOS has no CAN driver of its own, so a small dom0 program, **`can2rtos`**, passes the messages
over:

1. It **only receives** CAN messages, and only the two IDs we care about.
2. For each speed message, it writes one simple text line into FreeRTOS's console, like
   `V 1234 13500 4590` (message number, speed, rpm).

### Step 24 – FreeRTOS checks every message

A new FreeRTOS task, **`vVehicleTask`**, runs every 5 ms. It reads the lines and checks:

| Check | Warning when | All-clear when |
|---|---|---|
| Overspeed | above 120 km/h | below 115 km/h |
| Engine redline | above 6000 rpm | below 5700 rpm |
| Signal lost | no message for 100 ms | messages return |

The all-clear is a bit lower than the warning on purpose. This "hysteresis" stops a warning from
flickering on and off when the value sits right at the limit.

If the signal is lost, FreeRTOS marks the speed as invalid instead of showing an old value. That's
the "fail-safe" idea: when in doubt, say "I don't know" rather than show something wrong.

Example of what it prints:

```
[vehicle] ALERT overspeed: 135 km/h (limit 120)
[vehicle] ALERT engine over redline: 6293 rpm (limit 6000)
[vehicle] redline cleared: 5694 rpm
[vehicle] SIGNAL LOST: no frame for 100 ms, speed/rpm invalid
[vehicle] SIGNAL OK again (seq 52011)
[vehicle] 10s: frames 1000, lost 0, bad 0, max gap 25000 us, speed 135 km/h, rpm 4590; ...
```

**Test result:** 1000 messages every 10 s, **0 lost**. All warnings came on and went off at the
right moments, and pausing the pretend car triggered "SIGNAL LOST".

### Step 25 – The dashboard

The program **`cluster-hdmi`** draws a car dashboard on the HDMI monitor:

- big speed numbers and rpm with a bar;
- four warning lamps: OVERSPEED, REDLINE, SIGNAL, RTOS;
- a status line at the bottom.

It reads the CAN messages itself (listen only), and it reads FreeRTOS's warnings from FreeRTOS's log
file.

**Problem:** the RTOS lamp had no data. **Why:** the folder for that log file
(`/var/log/xen/console`) didn't exist after a restart. **Fix:** create it automatically at every
boot.

No monitor was available, so we tested it by drawing into memory and saving a picture. This is the
real picture from the board:

![Dashboard picture taken on the board](images/cluster-dashboard-board.png)

### Step 26 – Messages arriving in bursts

**What we saw:** sometimes FreeRTOS waited **105 ms** between messages. That's dangerously close to
its 100 ms "signal lost" limit, even though the pretend car never paused. On the dom0 side, the
gaps were at most 29 ms.

**Why:** we read the source code of Xen's console program (`xenconsoled`). To protect itself from
noisy guests, it only listens to a guest's "I'm ready for more" signal 30 times per 0.2 seconds. Our
FreeRTOS sent that signal after *every* read, up to 200 times a second. So xenconsoled kept
ignoring it, and the messages piled up and then arrived in a burst.

**Fix:** FreeRTOS now only sends the signal when its input buffer is nearly full. xenconsoled keeps
filling a buffer that has room anyway, without being asked.

**Result:** the biggest gap dropped from 105 ms to **20–35 ms**, still with 0 lost messages.

### Step 27 – Ignition (switched off until wired)

In a real car, the computer should switch itself off cleanly when you turn the key off. The car's
"ACC" wire has 12 V while the key is on.

**`ignition-watch`** reads that signal on board pin 7:

- ACC off for 10 seconds → it shuts the board down cleanly (guests first).
- ACC back on within 10 seconds → it cancels the shutdown. Starting the engine can make the voltage
  dip for a moment.
- It does nothing until it has seen ACC "on" at least once. Without this, a board on your desk with
  nothing connected could switch itself off.

> **Danger:** 12 V would destroy the board's 3.3 V pin. It must go through an **optocoupler** or a
> voltage divider. That's why the service is **switched off** until it's wired correctly. Details
> are in the layer's README.

### Step 28 – A Xen crash after flashing

**What we saw:** right after writing the new card, about one minute after start, Xen crashed:

```
(XEN) CPU3: Unexpected Trap: Data Abort
(XEN) PC:     0023ae60 rt.c#repl_timer_handler+0x1cc/0x2ac
(XEN)      R0: 00100100 ...
(XEN) Panic on CPU 3:
(XEN) Reboot in five seconds...
```

**Detective work:**

1. We looked up the address `0023ae60` in Xen's symbol file. It's inside the RTDS scheduler, in a
   list of "who gets their time budget refilled next".
2. The number `00100100` is a special marker Xen writes into list entries that have been *removed*.
   So Xen was walking through a list that contained an entry it had already thrown away: the list
   was broken.
3. This is the **same bug** as the assertion in Step 18 (`rt.c:587`), so the workaround there had
   only hidden it.
4. We searched Xen's history and found that the Xen developers had fixed exactly this in 2022. When
   a guest is *paused* for a moment (Xen's own tools do that), the scheduler forgot to remove it
   from this list. When the guest resumed, it was added a second time, and the list broke. Our Xen
   (4.16.1) was older than the fix.
5. To prove it, we paused and resumed FreeRTOS in a loop on the old Xen. It crashed in under 250
   rounds, with exactly the same message.

**Fix:** we added the two official fixes (Xen patches `0004` and `0005`).

**Proof:** on the new card we ran the same loop **3000 times without a crash**. Afterwards
everything was still normal (1000 messages per 10 s, 0 lost, FreeRTOS 9958–10040 µs).

> **A mistake we made (no harm done):** while checking the board, we pressed Enter during U-Boot's
> "Hit any key" moment. U-Boot stopped and tried to run our Linux commands, which it didn't
> understand (`Unknown command`). None of them changes anything, and typing `boot` continued
> normally. Lesson: after switching on, only watch until `login:` appears.

---

## Chapter 10 – How the finished system works

### Who runs where

```
   Core 0      Core 1      Core 2     │    Core 3
  ┌──────────────────────────────────┐│  ┌──────────────────────┐
  │ Pool-0, fair scheduler (credit2) ││  │ Pool "rt", real-time │
  │                                  ││  │ scheduler (RTDS)     │
  │  Linux dom0   (1 core, 512 MB)   ││  │                      │
  │  Android      (2 cores, 768 MB)  ││  │  FreeRTOS (16 MB)    │
  │                                  ││  │  200 µs every 1 ms   │
  └──────────────────────────────────┘│  └──────────────────────┘
```

### What happens when you switch on

```
 1. The chip's boot ROM loads U-Boot from the SD card.
 2. U-Boot finds /boot.scr on the card and runs it.
 3. The script loads Xen, Linux and the board description, fixes a few
    settings, switches the CPU to HYP mode and starts Xen.
 4. Xen switches on all 4 cores (through PSCI) and starts Linux (dom0).
 5. Linux starts its services in this order:
      watchdog → network bridge → Xen tools → guests (FreeRTOS, Android)
      → pretend car + gateway → dashboard → ignition watcher (off)
 6. You see:  tinker-board login:
```

If Xen can't start, the script automatically starts plain Linux instead, so you're never stuck.

### How the car data flows

```
 vehicle-sim ──(CAN, every 10 ms)──► vcan0 ──► can2rtos ──(text lines)──► FreeRTOS
   (pretend car)                        │      (listen only)                   │
                                        │                                  checks and
                                        └──────────► cluster-hdmi ◄──── warnings in its log
                                                     (dashboard on HDMI)
```

### What's on the SD card

```
 part 1     loader     the first tiny starter program
 part 4     U-Boot     our patched bootloader
 part 6     boot       xen, zImage (Linux), board description, boot.scr
 part 7     root       Linux dom0's files
 part 8     android-system   Android's programs
 part 9     android-data     Android's data
 (2, 3, 5 are reserved and empty)
```

### Where our work lives (the layer)

```
meta-tinker-xen/
├── recipes-bsp/          U-Boot fixes (HYP mode, PSCI) and the Xen start script
├── recipes-extended/
│   ├── xen/              Xen fixes 0001–0005 and its settings
│   ├── xenbr0/           the virtual network switch
│   ├── xen-guests/       start guests at boot, watchdog, USB hub rule
│   ├── vehicle-bench/    pretend car + gateway
│   ├── cluster-hdmi/     dashboard
│   ├── ignition-watch/   key-off shutdown (off)
│   └── android-hdmi/     Android screen on HDMI (off)
├── recipes-kernel/       Linux dom0 fix (timer7) and extra features (bridge, CAN)
├── recipes-rtos/         FreeRTOS guest
├── recipes-android/      Android and its 4.14 kernel
├── recipes-core/         the image recipe; login-prompt fix
├── recipes-devtools/     QEMU (only needed for the Android screen)
└── wic/                  SD card layout
```

---

## Chapter 11 – How to build, flash and use it

### Build (on the PC)

```
cd ~/yocto-rk3288
source poky/oe-init-build-env build
bitbake tinker-xen-test-image
```

### Write the SD card (on the PC)

> **Careful:** this erases the whole card. Make sure you pick the card and not your PC's own disk!

1. Put the card in the PC and find its name. Look for the size, about 29.7G:
   ```
   lsblk
   ```
2. Check that the name is really a device. The line must start with the letter **b**:
   ```
   ls -l /dev/sda
   ```
3. Unmount it and write the image:
   ```
   sudo umount /dev/sda?*
   sudo bmaptool copy ~/yocto-rk3288/build/tmp/deploy/images/tinker-board/tinker-xen-test-image-tinker-board.wic /dev/sda
   ```

Always write to the whole card (`/dev/sda`), never to one partition (`/dev/sda1`).

> **Something that happened to us:** once the card wasn't really connected, and the write went into
> an ordinary 3.2 GB *file* called `/dev/sda` instead. You can recognise this when `ls -l /dev/sda`
> starts with `-` instead of `b`. Only in that case is it safe to delete it (`sudo rm /dev/sda`) and
> re-insert the card.

### First start

1. Put the card in the board and connect the serial adapter.
2. Start `picocom -b 115200 /dev/ttyUSB0` on the PC.
3. Switch on the board. **Don't press keys** until you see `tinker-board login:`.
4. Type `root` and press Enter (there's no password).

### Everyday commands (on the board)

| I want to… | Type |
|---|---|
| see all running systems | `xl list` |
| see how cores are shared | `xl cpupool-list` |
| watch FreeRTOS | `xl console rtos1` (leave with `Ctrl-]`) |
| use Android's shell | `xl console android1`, or `adb connect 192.168.100.3:5555` then `adb shell` |
| stop / start the guests | `/etc/init.d/xen-guests stop` / `start` |
| see the raw car messages | `candump vcan0` |
| see FreeRTOS's car checks | `grep vehicle /var/log/xen/console/guest-rtos1.log \| tail` |
| test "signal lost" | `/etc/init.d/vehicle-bench pause` (run it again to resume) |
| read the temperature | `cat /sys/class/thermal/thermal_zone1/temp` (in thousandths of °C) |
| restart / switch off | `reboot` / `poweroff` |

What `xl list` should show:

```
Name                                        ID   Mem VCPUs	State	Time(s)
Domain-0                                     0   512     1     r-----     423.3
rtos1                                        1    16     1     -b----       8.9
android1                                     2   767     2     r-----     458.9
```

### Reading a real car (later, listen only)

1. Plug in a USB CAN adapter.
2. Edit `/etc/default/vehicle-bench` and set `IFACE=can0`, `SIM=0`, `BITRATE=500000`.
3. Run `/etc/init.d/vehicle-bench restart`.

The adapter is put in "listen-only" mode, so it never sends anything. Real cars use their own
message numbers, so `can2rtos` would need to learn them first. **Never connect the pretend car
program to a real car.**

---

## Chapter 12 – Every problem we hit, in plain words

| # | What we saw | What was really wrong | How we fixed it |
|---|---|---|---|
| 1 | Linux stopped after listing partitions | the card had an old, different project on it | wrote our image |
| 2 | typing didn't reach the board | wires on the wrong serial pins | pins 32/33/34 |
| 3 | "Xen must be entered in NS Hyp mode" | U-Boot couldn't hand over in hypervisor mode | U-Boot patch 0001 |
| 4 | silence after "Starting kernel" | a CPU setting held random leftovers | set it to a clean value |
| 5 | non-secure programs locked out | chip devices were "secure only" | open them in U-Boot |
| 6 | "Missing processor info" | Xen didn't know this CPU | Xen patch 0001 |
| 7 | Linux crashed reading L2CTLR | Xen didn't answer that register | Xen patch 0002 |
| 8 | Linux crashed at a DMA address | it used the secure-only address | start script fixes the address |
| 9 | Linux hung after 2 seconds | Linux reset the clock timer, so Xen's time ran backwards | Linux patch: leave timer7 alone |
| 10 | board restarted once at `xl create` | probably a power dip | use a 5 V / 3 A supply |
| 11 | FreeRTOS repeated old lines | caches off | caches on |
| 12 | FreeRTOS period 10.15 ms | small delays added up | fixed schedule |
| 13 | FreeRTOS used 100 % CPU | busy waiting | sleep when idle |
| 14 | `reboot` never finished | Xen couldn't reset this chip | Xen patch 0003 |
| 15 | "Cannot find the RK3288 CRU node" | RK3288W uses a different name | accept both names |
| 16 | only 1 CPU core used | no PSCI firmware | PSCI in U-Boot (patch 0002) |
| 17 | only 2 cores came up | one mailbox woke all cores | switch them off first |
| 18 | Android crash: no /dev/input | no input devices | create the folder |
| 19 | Android alarm crash | missing permission in Android 7 | small kernel patch |
| 20 | USB hub kept resetting | hub's power saving | keep it awake |
| 21 | Xen assertion rt.c:587 | Xen scheduler bug (see 32) | RTDS only on core 3 |
| 22 | release Xen had no real-time scheduler | turned off by default | turn it on |
| 23 | no temperature readings | "CPU cooling" link impossible under Xen | remove the link |
| 24 | Android network service refused | no security label | add `seclabel` |
| 25 | "respawning too fast" spam | login prompt on Xen's port | don't start it there |
| 26 | "Device or resource busy" when flashing | card partition still mounted | unmount again |
| 27 | `/dev/sda` was a file, not the card | wrote while the card was missing | delete only if it starts with `-` |
| 28 | Android screen: "no suitable EGLConfig" | wrong colour depth | 16-bit colour patch |
| 29 | Android froze with the screen on | no 3D graphics; timer got stuck | not solved → screen off |
| 30 | dashboard RTOS lamp empty | log folder missing | create it at boot |
| 31 | car messages arrived in 105 ms bursts | FreeRTOS signalled too often and got ignored | signal only when needed |
| 32 | Xen crash in `repl_timer_handler` | guest pause broke a scheduler list (old Xen bug) | official Xen fixes 0004 + 0005 |
| 33 | U-Boot said "Unknown command" | Enter pressed during the countdown | type `boot`; wait for login |
| 34 | board went silent twice | probably the power supply | power-cycle; better supply |
| 35 | Android's home screen keeps restarting | needs 3D graphics | accepted (Android is screen-less) |

---

## Chapter 13 – What doesn't work yet, and how to recover

### Not working, or not tested yet

- **Android has no screen.** It works over adb, but its home screen and status bar keep
  restarting because there's no 3D graphics. The screen experiment froze Android, so it's off.
- **Not tested, because the equipment is missing:** internet for the guests (no Ethernet cable),
  the dashboard on a real monitor, the ignition wire, a real CAN adapter.
- **Not for a real car's controls.** This is a test bench and a listen-only monitor. It isn't
  certified for safety (ISO 26262) and must never control anything in a car.
- **The car data travels through Linux** on its way to FreeRTOS, so delivery can lag by up to about
  35 ms. Only FreeRTOS's own checks are truly real-time.
- **The CPU stays at 500 MHz**, because this Xen version can't change the CPU speed.
- **Security was opened up** for bring-up: U-Boot makes everything "non-secure". That's fine for a
  test board, not for a product.
- **License:** the Android image is for development only and mustn't be given away.

### If something goes wrong

| Problem | What to do |
|---|---|
| a guest misbehaves | `xl destroy <name>`, then `/etc/init.d/xen-guests start` |
| the whole system freezes | wait ~45 s: the watchdog restarts it. Otherwise switch off and on |
| Xen doesn't start | press a key at "Hit any key", then type `sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf` to start Linux without Xen |
| Android's data is broken | write the card again |
| the card doesn't start at all | write the card again from the PC. The original working image is in `diagnostics/baseline/` |

The board's own built-in storage is never touched. Everything lives on the SD card, so writing the
card again always fixes it.

> **Never type `saveenv` at the U-Boot prompt.** It permanently changes U-Boot's settings.

### The rules we followed the whole time

1. Only you write the SD card. The PC never does it automatically.
2. No commands that permanently change the bootloader or erase disks.
3. Every new bootloader or Xen was first tested from memory only.
4. Nothing counts as "tested" without a log file from the board. They're all in `diagnostics/`.
5. Nothing ever sends messages to a car.

---

## Chapter 14 – Word list

| Word | Simple meaning |
|---|---|
| **adb** | a tool to control Android from another computer |
| **ACC** | the car wire that has power while the key is turned on |
| **bitbake** | the command that runs the Yocto build |
| **boot ROM** | a tiny program inside the chip that runs first |
| **bootloader / U-Boot** | the program that prepares the board and starts the next one |
| **bridge** | a virtual network switch inside Linux |
| **CAN bus** | the network cars use between their computers |
| **console / UART / serial** | the text connection to the board through the USB adapter |
| **core / CPU** | one "brain" of the processor (this board has 4) |
| **cpupool** | a group of cores with its own scheduler |
| **credit2** | Xen's fair-share scheduler |
| **dom0 / Domain-0** | the boss Linux that controls Xen |
| **domain / guest** | one operating system running on Xen |
| **fail-safe** | when unsure, choose the safe answer ("data invalid") |
| **flash / write the card** | copy the image onto the SD card |
| **HYP mode** | the CPU level a hypervisor needs |
| **hypervisor** | software that runs several operating systems at once, kept apart |
| **hysteresis** | the warning switches off a bit below where it switched on, to avoid flicker |
| **image (.wic)** | one file containing the whole SD card |
| **kernel** | the core of an operating system |
| **layer** | a folder of Yocto recipes; ours is `meta-tinker-xen` |
| **listen-only** | a CAN mode where the board never sends anything |
| **patch** | a file describing small changes to source code |
| **PSCI** | a standard way to ask firmware to switch CPU cores on and off |
| **real-time** | always on time, not just fast on average |
| **recipe / bbappend** | Yocto files: how to build something / extra changes to another recipe |
| **RTDS** | Xen's real-time scheduler (guaranteed time slices) |
| **scheduler** | the part that decides which program uses a core next |
| **SELinux** | Android's security system |
| **vcan** | a pretend CAN bus inside Linux, for testing |
| **watchdog** | a timer that restarts the board if the system stops responding |
| **xl** | the command to control Xen (list, create, stop guests) |
| **Yocto** | the "factory" that builds a custom Linux from recipes |

---

*Exact measurements, full code listings, addresses and image checksums are in
`TECHNICAL-REFERENCE.md`. The full engineering log is `diagnostics/REPORT.md`, sections A–Z. Every
board test has its own log file in `diagnostics/`.*
