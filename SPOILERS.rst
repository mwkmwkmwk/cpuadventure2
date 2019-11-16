The machine
===========

The machine runs on a balanced ternary system — the "bits" can have a value of
1, 0, and -1 (written as ``-``, ``0`` and ``+`` in the ``nb`` files), and have
weights equal to ``3**bit_index``.  The bytes on this machine are 9 bits long.

The machine has the following registers (all are 9 bits long):

- ``@-4``, ``@-3``, ``@-2``, ``@-1``, ``@1``, ``@2``, ``@3``, ``@4``: general purpose registers
- ``@0``: general purpose register hardwired to 0
- ``@PC``: the program counter, points to the currently executed instruction
- ``@PSW`` (special reg 0): program status word, with the following bits:

  - 0: ``SF`` — the sign flag (set to the sign of the last arithmetic operation)
  - 1: ``CF`` — carry flag (set to the carry out of the highest bit)
  - 2: ``TL`` — trap level, set as follows:

    - 0: no exception is being handled
    - 1: an exception or syscall handler is running
    - -1: a double fault handler is running

  - 3: ``XM`` — execution mode:

    - 0: supervisor mode (can executed privileged operations and read/write special regs)
    - 1: user mode
    - -1: user mode with access to the TVC opcode (terminal calls)

  - 4: ``WM`` — write mode, same values as ``XM``, but is used when checking privilege
    in the store instruction (allows the kernel to access user memory safely)
  - 5: ``RM`` — read mode, like above, but for load instructions
  - 6: ``XP`` — execution paging (used for fetching instructions):

    - 0: no paging (virtual addresses are physical addresses)
    - 1: paging enabled, use page table address from ``@PGTP``
    - -1: paging enabled, use page table address from ``@PGTN``

  - 7: ``WP`` — write paging, same as ``XP``, but used by store instructions
  - 8: ``RP`` — read paging, same as ``XP``, but used by load instructions

- ``@PGTP`` (special reg 1) and ``PGTN`` (special reg -1): page table pointers, contain bits 2-11 of the base physical
  address of the page tables
- ``@EPC`` (special reg 2): saved ``@PC`` on an exception
- ``@EPSW`` (special reg 3): saved ``@PSW`` on an exception
- ``@EDATA`` (special reg 4): set to some useful data on an exception
- ``@SCRN``, ``@SCRZ``, ``@SCRP`` (special regs -4, -3, -2): scratch registers (for exception handlers)

The only registers that non-supervisor mode can access are general purpose registers,
``@PC``, and the low 2 bits of ``@PSW``.

The machine has ``3**12`` bytes of physical memory, with all 12-bit addresses being valid
(thus half the memory space is at negative addresses).  It also has ``3**9`` bytes of virtual
memory space, split into 27 ``3**6``-byte pages.  The translation works as follows:

1. Split the virtual address into page index (top 3 bits) and page offset (bottom 6 bits)
2. Look up the page index in the page table, by concatenating the page index with the appropriate ``@PGT*`` register,
   getting the physical address of the PTE (note that this means that ``@PGT*`` points to the *middle* of the page table,
   since page indices can be negative)
3. Decode the PTE as follows:

   - bit 0: execution permissions:

     - 0: page is not executable
     - 1: page executable by both user and supervisor
     - 2: page executable by supervisor only

   - bit 1: write permission (as above)
   - bit 2: read permission (as above)
   - bits 3-8: frame index

4. Check permissions, trigger page fault if access not allowed
5. The final physical address is the page offset concatenated with the frame index.

Note that all fields can have negative values — the address 0 is actually the *middle* of page 0.

When the machine starts, all registers are set to 0 (thus the machine starts at address 0, in
supervisor mode with paging disabled).

The following exceptions exist:

- -4: page fault on execute (EDATA is faulting virtual address)
- -3: page fault on write (EDATA is faulting virtual address)
- -2: page fault on read (EDATA is faulting virtual address)
- -1: double fault (exception triggered with TL=1, EDATA is the original exception code)
- 0: [reserved for reset vector]
- 1: divide by 0 (EDATA is the divided value)
- 2: privileged instruction (EDATA is the opcode)
- 3: illegal opcode (EDATA is the opcode)
- 4: syscall (EDATA is the opcode)

When an exception happens, PSW is set to 0, except TL which is set to 1 (or -1 if entering double fault handler),
and PC is set to the exception index multiplied by 3.


The instruction encoding
------------------------

Instructions can be one byte, or two bytes.  The instruction set is otherwise RISC-ish.
The opcodes are:

- ``-----bbaa`` imm: ``AND ra, rb, imm``
- ``----0bbaa`` imm: ``ADD ra, rb, imm``
- ``----+bbaa`` imm: ``XOR ra, rb, imm``
- ``---0-bbaa`` imm: ``LD ra, [rb + imm]``
- ``---00ccaa`` imm: ``J<cc> ra, imm``
- ``---0+bbaa`` imm: ``ST ra, [rb + imm]``
- ``---+-bbaa`` imm: ``DIV ra, rb, imm``
- ``---+0bbaa`` imm: ``MUL ra, rb, imm``
- ``---++bbaa`` imm: ``MOD ra, rb, imm``
- ``--0ccbbaa``: ``AND ra, rb, rc``
- ``--+ccbbaa``: ``XOR ra, rb, rc``
- ``-0-ccbbaa``: ``DIV ra, rb, rc``
- ``-00ccbbaa``: ``MUL ra, rb, rc``
- ``-0+ccbbaa``: ``MOD ra, rb, rc``
- ``-+-ccbbaa``: ``SHL ra, rb, rc``
- ``-+0ccbbaa``: ``SHR ra, rb, rc``
- ``0--ccbbaa``: ``LD ra, [rb + rc--]``
- ``0-0ccbbaa``: ``LD ra, [rb + rc]``
- ``0-+ccbbaa``: ``LD ra, [rb + rc++]``
- ``00-ccbbaa``: ``J<cc> ra, rb``
- ``0000-bbaa``: ``GETF ra, fb`` — move PSW bit value to a register
- ``00000-000``: ``ERET`` — return from exception (privileged instruction, restores ``@PC`` and ``@PSW`` from ``@EPC`` and ``@EPSW``)
- ``00000-+aa``: ``RNG ra`` — get a true random value
- ``00000+iaa``: ``SETF fa, imm`` — set a PSW bit to a given value (privileged instruction except ``SF`` and ``CF``)
- ``0000+-xxx``: ``TVC x`` — terminal call (requires being in mode -1)
- ``0000+0xxx``: ``SVC x`` — system call
- ``0000++xxx``: ``HVC x`` — hypervisor call (privileged instruction)
- ``000+-bbaa``: ``S2R ra, sb``: move from a special register (privileged instruction)
- ``000++bbaa``: ``R2S sa, rb``: move to a special register (privileged instruction)
- ``0+-ccbbaa``: ``ST ra, [rb + rc--]``
- ``0+0ccbbaa``: ``ST ra, [rb + rc]``
- ``0++ccbbaa``: ``ST ra, [rb + rc++]``
- ``+--ccbbaa``: ``SUBX ra, rb, rc``
- ``+-0ccbbaa``: ``SUB ra, rb, rc``
- ``+-+ccbbaa``: ``SUBC ra, rb, rc``
- ``+0iiibbaa``: ``SHL ra, rb, imm``
- ``++-ccbbaa``: ``ADDX ra, rb, rc``
- ``++0ccbbaa``: ``ADD ra, rb, rc``
- ``+++ccbbaa``: ``ADDC ra, rb, rc``

Some instructions have non-obvious semantics:

- for ``GETF`` and ``SETF``, PSW bits are counted from -4 (``SF``) to 4 (``RP``)
- ``AND`` does a bit-wise multiplication (``1 AND -1 == 1, -1 AND -1 == 1``)
- ``XOR`` does a bit-wise addition modulo 3 (``1 XOR 1 == -1, -1 XOR -1 == 1, 1 XOR -1 == 0``)
- ``J<cc>`` is conditional jump with link: if the branch is taken, the address of the next instruction is stored in the first argument.  The condition codes are:

  - ``--``: ``JGE`` — jumps if ``SF`` is 0 or -1
  - ``-0``: ``JNZ`` — jumps if ``SF`` is 1 or -1
  - ``-+``: ``JLE`` — jumps if ``SF`` is 0 or 1
  - ``0-``: ``JNC`` — jumps if ``CF`` is 0
  - ``00``: ``J`` — jumps always
  - ``0+``: ``JC`` — jumps if ``CF`` is 1 or -1
  - ``+-``: ``JL`` — jumps if ``SF`` is -1
  - ``+0``: ``JZ`` — jumps if ``SF`` is 0
  - ``++``: ``JG`` — jumps if ``SF`` is 1

- for ``SHL`` and ``SHR``, the shift count is signed — shifting left by ``-x`` is the same as shifting right by ``x`` (this is why there is no ``SHR`` instruction with immediate — it would be redundant)
- there is no ``SUB`` instruction with immediate — an ``ADD`` with negated immediate can be used instead
- there is no ``CMP`` instruction — a ``SUB`` instruction with ``@0`` destination can be used instead
- the following variants of ``ADD`` and ``SUB`` instructions exist:

  - ``ADD``/``SUB``: plain addition/subtraction, the ``CF`` is set to the extra bit of the result (assuming both sources are 0-extended)
  - ``ADDX``/``SUBX``: add/subtract with carry flag update — extends second argument with 0, concatenates the first argument with current ``CF``, does a 10-bit operation, writes back the highest bit of the 10-bit result back to ``CF``
  - ``ADDC``/``SUBC``: add/subtract with carry — computes ``A + B + CF`` or ``A - B + CF``, writes the extra bit of the result to ``CF`` (both sources are 0-extended)

The hypercalls are as follows:

- ``HVC 0``: ``exit`` — exits the emulator process with exit code from ``@1``
- ``HVC 1``: ``log`` — writes a log message to the screen. ``@1`` is a pointer to a 0-terminated string, ``@2`` is the log level.  The string is read in the same way as an ``LD`` instruction would with current ``@PSW``.  The log levels are:

  - ``-1``: DEBUG (not printed by default)
  - ``0``: INFO
  - ``1``: WARNING
  - ``2``: ERROR
  - ``3``: FATAL

- ``HVC 2``: ``open_nb`` — opens a ``.nb`` file.  ``@1`` is the file name (a 0-terminated string).  Returns file length in bytes in ``@1``, or ``-1`` for error.
- ``HVC 3``: ``read`` — reads from a currently open file.  ``@1`` is a pointer to the data buffer (written as if ``ST`` instruction was used), ``@2`` is number of bytes to read, ``@3`` is file position to read from.  Returns ``0`` in ``@1`` for success, ``-1`` for failure.
- ``HVC 4``: ``open_txt`` — opens a text file.  Arguments and returns like in ``open_nb``.

The terminal calls are as follows:

- ``TVC -1``: ``bell`` — rings the terminal bell.
- ``TVC 0``: ``putc`` — puts a character at a given position.

  - ``@1``: the x coordinate (from -40 to 40 — 0 is the center of the screen)
  - ``@2``: the y coordinate (from -13 to 14)
  - ``@3``: the character code
  - ``@4``: the character attributes:

    - bits 0-1: foreground color:

      - -4: black
      - -3: red
      - -2: green
      - -1: yellow
      - 0: black
      - 1: blue
      - 2: pink
      - 3: cyan
      - 4: white

    - bits 2-3: background color
    - bit 4: bold if non-zero

- ``TVC 1``: ``getc`` — reads a single character from keybaord, returns it in ``@1``.

Whenever text is involved, it is represented as a sequence of Unicode codepoints.
On input, out of range codepoints are taken modulo ``3**9``.  On output, negative
numbers have ``3**9`` added to them to obtain a codepoint.


The game
========

The game is split into four non-binaries:

- ``game.nb``: the operating system, running in supervisor mode, and
  implementing the game logic
- ``pc.nb``: the Player Character, running in user mode with terminal access,
  implementing the game UI
- ``valis.nb``: the valis NPC, running in user mode
- ``enemy.nb``: all the dragon NPCs, running in user mode

Each character on the board is run in its own process, with its own private
memory and address space.


The operating system interface
------------------------------

Every process is started with its own pid in ``@-1`` register.

The following syscalls exist:

- -4: ``iterate_floor`` — starts iterating items on the floor at the caller's position
- -3: ``iterate_next`` — returns an item from the floor/inventory, moves to the next item
  on the list.  Item code is returned in ``@1``, item count in ``@2``.  If no more items,
  returns 0s.
- -2: ``iterate_inv`` — starts iterating items in caller's inventory
- -1: ``look`` — returns information about a given board square.  The square is given in ``@1`` (bits 0-3 are X coordinate, 4-6 are Y coordinate).  The results are:

  - ``@1``: item type (negative), character type (positive), or nothing (zero)
  - ``@2``: character pid, if applicable (0 otherwise)

- 0: ``yield`` — returns to the game loop, waiting for the next message
- 1: ``log`` — same arguments as the ``log`` hypercall
- 2: ``pidinfo`` — returns information about a process whose pid is given in ``@1``:

  - ``@1``: the character type (or 0 if no such process)
  - ``@2``: the character's position (same format as in ``look``)

- 3: ``flag`` — reads the caller's flag into a buffer.  The buffer is specified in ``@1``.
  This is ``flag1.txt`` for the PC, ``flag2.txt`` for valis.

- 4: ``exit`` — calls the ``exit`` hypercall, ending the game immediately.

When ``yield`` is called, it eventually returns a message from the game loop.  The message code is in ``@1``:

- ``-3``: bump notification (a move action was performed into a wall or into a square occupied by another character):

  - ``@2``: the pid that performed the move
  - ``@3``: the pid that was bumped into (or 0 if bumped into a wall)

- ``-2``: croak notification (a character was killed — the killed process gets it as well as its final notification):

  - ``@2``: the pid that killed the process
  - ``@3``: the pid that was killed

- ``-1``: attack notification (a character was attacked, but not killed):

  - ``@2``: the pid that performed the attack
  - ``@3``: the pid that was attacked
  - ``@4``: the remaining hp of the target process
   
- ``0``: pick an action.  The process should pick an action, and call ``yield`` with the selected action in registers.

- ``1``: square change notification — sent when a square changes contents

  - ``@2``: square coordinates (same format as in ``look``)
  - ``@3``: occupant type (same as in ``look``)
  - ``@4``: occupant pid (same as in ``look``)

- ``2``: item give notification — sent when an item was given to this process

  - ``@2``: the pid that gave use the item
  - ``@3``: the item type
  - ``@4``: the item count

- ``3``: item use notification — sent when an item was successfully used by this process

  - ``@2``: the item code
  - ``@3``: our new HP
  - ``@4``: our new drunk counter

When ``yield`` is called in response to a pick action notification, ``@1`` selects the action to be performed:

- -1: give an item to another process

  - ``@2``: direction to give to (the target has to be adjacent):

    - bit 0: the X coordinate delta
    - bit 1: the Y coordinate delta

  - ``@3``: the item type
  - ``@4``: the item count

- 0: move

  - ``@2``: direction (same as in give action)

- 1: attack

  - ``@2``: direction (same as in give action)

- 2: drop an item

  - ``@2``: item type
  - ``@3``: item count

- 3: pick up an item

  - ``@2``: item type
  - ``@3``: item count

- 4: use an item

  - ``@2``: item type


Flag 1: The user interface
==========================

The ``gets`` function has a buffer underflow — the backspace handling doesn't check
whether it's already at the beginning of the input buffer.  You can just backspace
all the way through the data segment into the code, and overwrite eg. ``gets_end``
with your own shellcode.


Flag 2: valis
=============

There are three bugs that can be combined to get RCE:

- the kernel doesn't verify item counts in the drop/pickup/give commands
  (allowing you to give more of an item than you have, or a negative amount)
- the give notification handler has broken stack cleanup (allocates three bytes
  for a stack frame, but only drops two), with the final byte being the item count,
  allowing you to upload arbitrary contiguous bytes to memory
- the give notification handler for a redbull uses a jump table to pick a message,
  but doesn't anticipate negative item counts, allowing you to jump to an arbitrary
  address


Flag 3: The operating system
============================

The ``log`` system call has a plain old buffer overflow — the log message read from
the userspace is clipped to 242 characters, but the buffer where the log line is built
is only 242 characters long as well, and it also has to include the process name and id.
This bug is not exploitable from ``pc.nb`` because the name is too short (you can only
overwrite ``exc_regs``, which is not useful), but from ``valis.nb`` you can reach the
``processes`` array, allowing you to inject your own process structure.  Since the
process structure has a ``PSW`` field, you can just create a new process that runs
in unpaged supervisor mode and will execute your shellcode.
