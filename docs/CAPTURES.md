# Captures

A capture is a text file with everything that went between the pc and a device, in order. Their kept in `tests/captures/` because they let the tests run the code against a device that isnt plugged in, or that nobody working on this even owns.

## Making one

Any cli command can be recorded by putting `capture FILE` in front of it:

```bash
rockcat-cli capture mine.cap info
```

The library appends to the file, so several commands in a row make one long recording. Delete the file first if you want a fresh one.

For devices that arent supported yet there are ready made scripts in `tools/capture/` that read out everything the device has stored. They are READ ONLY, the only thing they send is the "show me profile N" select:

```bash
tools/capture/kova2016.sh kova.cap
```

```bash
tools/capture/ryos-mk-fx.sh ryos.cap
```

Own a ROCCAT device that isnt on the list in the README yet? A capture of it is the single most usefull thing you can send to me. `rockcat-cli raw list` shows which hidraw node has the reports, `rockcat-cli raw get ID LEN` reads one.

> Note : have a look through the file before you post it anywhere. USB serial numbers are never written into it, but macros are your keystrokes, so dont record those if there is a password in one (the scripts skip macros for exactly that reason).

## Playing one back

```bash
ROCKCAT_REPLAY=tests/captures/kone-aimo-remastered-synthetic.cap rockcat-cli info
```

No hardware gets touched. A replay is strict: the code has to send exactly what the recording has, in the same order, and gets the recorded answers back. The moment it sends something else the replay complains on stderr with the line number and every call after that fails. Thats the whole point, it is how one can notice that a refactor changed what goes over the wire.

It also means a replay is not a simulated device. You cant click around in the gui with one, it only knows the one conversation that was recorded. For clicking around there is `ROCKCAT_SIM=kone-aimo-remastered`, a pretend Kone AIMO Remastered that answers whatever you ask it.

## The cli test

`tests/cli_golden.sh` runs about 40 cli commands against the simulated mouse and compares both what they print and what they send with `tests/cli_golden.txt`. If you change the cli and that test goes red, read the diff: a changed message is fine (update the file), a changed `S` line means the mouse now gets told something different, and thats only fine if you meant it.

## The format

```
# rockcat capture v1
D 1e7d 2cee 0100 0 ROCCAT Kova
S 04 00 80 @5
G 04 3 : 04 01 00 @160
G 06 28 : 06 1c 00 00 06 ff ... dd 03 @165
```

- `#` is a comment. The first line has to be exactly the one above.
- `D vid pid release interface name` says what was recorded. Once per file.
- `S bytes` is a feature report thats sent. First byte is the report id.
- `G id length : bytes` is a feature report that was asked for (id and how many bytes) and what came back.
- `R bytes` is an input report the device sent by itself (mouse moved, key pressed, profile switched).
- `W bytes` is an output report that was sent (only the Suora uses those so far). First byte is the report id, 0 when the device has none.
- `!` instead of bytes means the transfer failed when it was recorded, the replay fails it the same way.
- `@number` at the end is milliseconds since the device was opened. Only there for humans, the replay ignores it.

All bytes are hex, everything else is decimal. One transfer per line no matter how long (a macro chunk is 1026 bytes, yes that line is long, no, not wrapping it).

## Whats in tests/captures

- `kone-aimo-remastered-synthetic.cap`: NOT from a real mouse. Its the library talking to a pretend Kone AIMO Remastered (`tests/kone_sim.h`) that was written from `docs/protocol/kone-aimo.md`. It proves the library keeps sending the same bytes, it does not prove a real mouse likes them. If you own that mouse and want to fix this: `rockcat-cli capture kone.cap backup test.rswb`
- `kova2016-gray-readonly.cap`: a real Kova 2016 Gray, firmware 1.18, made with `tools/capture/kova2016.sh`
- `ryos-mk-fx-readonly.cap`: a real Ryos MK FX (swiss layout), made with `tools/capture/ryos-mk-fx.sh`
