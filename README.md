# Coroutine Packet Tracer

This is a small C++20 experiment for comparing three ways of handling slow,
blocking disk writes while packets are being produced.

It is not a real network sniffer yet. The producer generates repeatable fake
Ethernet-sized packets, and the program writes them to a classic PCAP file. The
interesting part is what happens to the event loop while each write is taking
place.

## The three modes

| Mode | What happens to a packet write |
| --- | --- |
| `sync` | The event-loop thread writes the packet itself and remains blocked until the write finishes. |
| `thread` | The producer submits each write to the thread pool and immediately continues. No writer coroutine is involved. |
| `coro` | The producer pushes packets through a channel. A writer coroutine waits for each pool job without blocking the event loop. |

Each run uses the same random seed, so the generated packet sequence starts the
same way. The program is time-based, however, so faster modes normally produce
more packets during the capture window.

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

The executable is created at `build/netio`.

## Run one mode

The default mode is `coro`, with a five-second capture window:

```bash
./build/netio
```

A shorter explicit run looks like this:

```bash
./build/netio \
  --mode coro \
  --disk-us 1000 \
  --pool 2 \
  --win-sec 1 \
  --out out.pcap
```

## Compare all modes

Use `--mode all` to run the same setup in sync, thread, and coroutine mode from
one command:

```bash
./build/netio \
  --mode all \
  --disk-us 1000 \
  --pool 2 \
  --win-sec 1 \
  --out comparison.pcap
```