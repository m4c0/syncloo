# syncloo

Simple file sync tool.

## Example of usage

```
mkfifo /tmp/syncloo-1.fifo /tmp/syncloo-2.fifo
syncloo --from mylocal/.git       > /tmp/syncloo-1.fifo < /tmp/syncloo-2.fifo &
syncloo --to ~/backup/mylocal.git < /tmp/syncloo-1.fifo > /tmp/syncloo-2.fifo 
```

Assuming the system has a `mkfifo` capable of creating a named pipe, this
creates two processes. The "from" process lists all files in the `mylocal/.git`
directory and pass them through the standard output to the "to" process, which
will store all files in `~/backup/mylocal.git`. That would be an ineffective
way to backing up a local GIT repository as a "bare" copy, but it shows the
usage in a clear way.

## Protocol
 
The protocol is textual for simplicity and debugging. Example of an
hypothetical session:

```
from> MTIM009hello.cpp
to  > mtim00000000
from> DATA0000000e007hello.cpp
from> int main() {}
from>
to  > data
from> MTIM009hello.cpp
to  > mtim6951462d
```

It is a simple request-response protocol, driven by the "from" process, focused
on being predictable enough to always be `read` with a byte count.

Messages follows these rules:
* They start with an ID of four alpha characters;
* They end with an end-of-line character (`\r\n` or `\n`);
* All request should have a response;
* Request IDs are uppercase and their corresponding response IDs are lowercase;
* Numbers are encoded in hex with a fixed size;
* Variable-length strings are always prefixed with their length, encoded in
  three bytes.

Messages so far:

```
from> MTIM<str-file-name>
to  > mtim<u64-timestamp>
```

`MTIM` requests the modification time of a file in the remote end. The response
SHOULD contain the file modification as a timestamp measured in seconds from
Unix Epoch, or zero if the file does not exist.

On exotic systems with filesystem-dependant timestamps, these should be
converted to Unix Epoch timestamp before being transferred.

```
from> DATA<u64-file-size><str-file-name>
from> <file-contents>
from>
to  > data
```

`DATA` sends the file data in binary form (i.e. it does not convert end-of-line
characters). Like all messages, it should end with a new-line.

The response message contains no data.

## Design reasoning

This is a high-level protocol, designed to be easy to craft and parse messages
(even by hand) and transferred over any stack, as long as a bi-directional
communication pipe can be established.

Roughly speaking, its design is agnostic to how user connects the source and
target processes.

Any specifics of how to enrich that communication - such as adding
authentication, authorization, network transfer, etc - can be achieved by
wrapping each end in another protocol. Example: a simple SSH tunnel can add all
those requirements without adding dependencies to this project.

Portability is also a key design principle - both in terms of source code and
binary compatibility. In theory, it should be possible to take files from a
Windows desktop and transfer to an embedded device via a serial interface. This
specific scenario is not supported directly, but it should not require much
effort to implement.

