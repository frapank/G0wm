# Contributing to g0wm

Patches are welcome, and the way to send one is a pull request. It helps to
talk first: for anything big, open an issue and describe what you have in
mind, so nobody spends a weekend on something that does not fit.

The rules below keep the project easy to read and to review. A pull request
that ignores them may be sent back. They are not sacred, though: if you have a
good reason to bend one, say so in the pull request.

## Sending a change

Any workflow you already like is fine. If you do not have one, this works well:
keep your fork's `main` untouched and do every change on its own branch, cut
from upstream.

Once, to set things up:

1. Fork the repository on GitHub
2. `git clone https://github.com/username/G0wm && cd G0wm`
3. `git remote add upstream https://github.com/frapank/G0wm`

Then, for each change:

1. `git fetch upstream`
2. `git checkout -b short-name-of-the-change upstream/main`
3. Write the code and commit it
4. `git push -u origin short-name-of-the-change`
5. Open the pull request from that branch

The commits should explain the change by themselves. In the pull request,
add a short test plan: how to see the problem on `main`, if there is one, and
how to check that your branch fixes it or that the new feature works. List the
edge cases you know about, and run through the plan once yourself before you
ask anyone else to.

### Checklist

- `./configure --debug && make` builds with no new warnings.
- `make format` changes nothing. CI runs `make format-check` and fails on a
  wrong format.
- `make test` passes.
- `reuse lint` passes, if you added a file.
- The optional parts still build when turned off, with `--disable-xwayland`
  and `--no-integrated-background`. CI tries both.

## Writing commits

One change per commit. If the title needs an "and", it is probably two commits.
A commit that only moves or reformats code stays on its own, and its hash goes
in `.git-blame-ignore-revs` so `git blame` skips it.

### The title

A short imperative sentence, at most 50 characters, with no prefix and no
trailing period. It should finish the sentence "When applied, this commit
will...":

    Open files from the runner
    Fix the tray icon size check
    Speed up the runner fuzzy match

Not `runner: open files.` and not `Opened files from the runner`. `git log` has
more examples.

### The body

Optional, and separated from the title by one blank line. A trivial change does
not need it. Anything that is not obvious from the diff does: why the change
is needed, which issue it closes, what is subtle about the approach. Months
from now, someone will run `git blame` on a line they do not understand, and
the body is where they will look for the answer.

If you would write it in the pull request, you can write it in the commit
instead. Wrap lines at 72 columns.

## Pull request messages

The description of a pull request is the first thing the reviewer reads, so
write it for someone who has not seen your code yet. It must say:

- what the pull request does, in a sentence or two;
- everything it adds, changes or removes, one point each: new settings,
  actions, files, protocols, changes to the config format;
- why it is needed, and the issue it closes, if there is one;
- the test plan, as described in [Sending a change](#sending-a-change).

If the commits already say all this, the description can be short, but it
cannot be empty.

### AI disclosure

Every pull request must state whether AI was used in it, and the statement
goes in the description. This applies when the answer is no as well: a pull
request without the statement is not reviewed until it is added. Write it as
the last paragraph, for example:

    AI disclosure: none, the code and this description were written by hand.

or:

    AI disclosure: Claude helped write the runner history code and its commit
    messages. I read, ran and edited all of it.

If AI was used, say which tool and for which parts: code, commit messages, the
description, the tests. Using it is not a reason for rejection. Hiding that you
used it is.

### You answer for your code

Whoever opens the pull request must understand every line of it, and be able
to explain why it is written that way. In the review you are expected to
answer questions about the code, to justify your choices and to fix what is
found, without having to ask anyone or anything else. The description, the
commits and the answers in the review must agree with each other and with what
the code really does.

A pull request where this is missing, with code its author cannot explain,
answers that contradict the diff, or a description that does not match the
change, will be closed, and the reason will be written in the closing comment.

## How a review goes

The maintainer reads every pull request. Small ones may be merged right away,
bigger ones get questions and requests for changes, so do not take it
personally. The steps are:

1. Look at the commits and the test plan, and check that CI is green.
2. Read the code, looking for style and naming problems, buffer overflows,
   leaks, logic errors and missing license headers. A change to the config
   file or to the behavior users see gets a closer look.
3. Run the test plan.
4. Merge once the reviewer is happy.
5. Open follow-up issues for what was left out.

## Code style

g0wm is C23 (GNU) on top of wlroots, written close to the kernel style.
`.clang-format` is the reference and `make format` applies it, so there is no
need to count columns by hand. What follows is what it cannot tell you.

### Formatting

- Four spaces, no tabs. Only the `Makefile`, `*.mk` and `configure` use tabs.
  See `.editorconfig`.
- Lines stay under 100 columns. Break them where it reads well, and align
  continued arguments with the opening bracket.
- The brace of a function goes on its own line, the one of `if`, `for` and
  `while` stays on the same line. A single statement does not need braces.
- Declare a variable where you first use it, and a loop counter inside the
  `for`.

```c
static void function(void)
{
    if (condition1)
        do_thing1();

    if (condition2) {
        do_thing2();
    } else {
        do_thing3();
    }
}
```

### Naming

- Functions and file scope variables begin with the name of their module, the
  static ones too: `bar_draw()`, `runner_buffer`. The exception is
  `src/core/dbus.c`, which uses `bus_` because libdbus owns `dbus_`.
- Listener callbacks are named after the event: `monitor_handle_new_output()`.
- `src/core/g0wm/` is the core and its names have no prefix: `seat`,
  `selected_monitor`, `spawn()`.
- A setting is named after what it holds, `border_width`, while its key in the
  config file keeps its own name, `borderpx`.
- Types are `CamelCase`, constants and macros `UPPER_CASE`.
- Prefer a descriptive name to a short one: `monitor`, not `m`.

### Where things go

`src/` and `include/` mirror each other, with one directory per area: `core`,
`input`, `output`, `window`, `render` and `desktop`. When a module is too big
for one file it gets a directory of its own, like `src/desktop/bar/`, and its
files share private declarations through an `internal.h` next to them.

Every new file begins with the SPDX header the others have.

A new setting touches five files: `src/core/settings/config.h`,
`include/core/settings.h`, `src/core/settings/read.c`,
`src/core/settings/write.c` and `docs/settings.md`. It is not finished until
all five are done.

### Comments and macros

Comments are short and say why, not what. The code already shows what it does.

- Use `/* ... */`. `//` is only for the IWYU pragmas and the short notes beside
  a value in `config.h`.
- A comment that fits on one line stays on one line, with no final period. It
  goes above the code it explains, or after it on the same line for a field or
  a variable.
- A longer one continues with ` * ` on each line and closes with `*/` at the
  end of the last line, not on a line of its own:

```c
/* create an xdg-activation token stamped as ours and return its name.
 * Handed to a client when the user acts on its notification, so that
 * the focus request it sends back can be told apart from the others */
```

- Do not leave commented out code behind. Mark real loose ends with `TODO` or
  `FIXME` and say what is missing.

Reach for a function before a macro, and if a macro is really needed, keep it
next to its only user.

### A sample

```c
/* a window activating with one of these was asked for by the user */
int activation_token_ours(const struct wlr_xdg_activation_token_v1* token)
{
    return token && token->data == &activation_token_tag;
}
```

## Reporting a bug

Give us the output of `g0wm -v`, your wlroots version, your GPU, and the log
from `g0wm -d`. The bug report template asks for the same things.
