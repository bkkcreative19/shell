Here is the README rewritten cleanly in **pure Markdown**:

---

# **myshell — A Minimal Unix-Style Shell (C++)**

This project implements a small, educational Unix-like shell in modern C++17.
It supports **tokenization**, **quoting**, **pipelines**, **I/O redirection**, and **built-in commands**, using POSIX system calls such as `fork`, `execvp`, `pipe`, `dup2`, and `open`.

---

## **Features**

### **Tokenization**

* Splits input into **words** and **operators** (`|`, `<`, `>`, `>>`, `&`, `;`)
* Supports:

  * `'single quotes'`
  * `"double quotes"`
  * Backslash escaping
  * Escaping inside double quotes (`\"`, `\\`)

### **Pipelines**

Example:

```sh
ls -l | grep cpp | wc -l
```

Each stage is properly connected using pipes.

### **I/O Redirection**

Supported forms:

* `cmd < infile`
* `cmd > outfile`
* `cmd >> outfile` (append)

### **Built-in Commands**

Executed in the parent process:

* `cd [dir]`
* `pwd`
* `exit`
* `echo` (supports `$VAR` expansion)

### **External Commands**

Everything else is executed via:

```cpp
execvp(argv[0], argv.data());
```

---

## **Build Instructions**

### **Requirements**

* Linux, macOS, or WSL
* C++17 compiler: `g++` or `clang++`
* POSIX environment

### **Build**

```sh
g++ -std=c++17 -Wall -Wextra -O2 -o myshell main.cpp
```

### **Run**

```sh
./myshell
```

---

## **Usage Examples**

### Basic commands

```sh
myshell> ls
myshell> echo hello world
```

### Environment variable expansion (echo only)

```sh
myshell> echo $HOME
```

### Directory control

```sh
myshell> cd /tmp
myshell> pwd
```

### Pipelines

```sh
myshell> ps aux | grep ssh | wc -l
```

### Redirections

```sh
myshell> ls > files.txt
myshell> cat < files.txt
myshell> echo "more" >> files.txt
```

### Pipelines + redirection

```sh
myshell> cat < input.txt | grep foo | sort > out.txt
```

### Exit

```sh
myshell> exit
```

---

## **Architecture Overview**

### **Tokenizer**

Converts input into a `std::vector<Token>` where each token is either:

* `Word`
* `Op`

Handles:

* whitespace
* quotes
* escaping
* multi-character operators (`>>`)

### **Pipeline Splitter**

`splitIntoPipeline()` transforms tokens into:

```cpp
vector< vector<string> >
```

Each subvector represents a stage of a pipeline.

### **Redirection Parser**

`extractRedirections()` extracts:

* `< infile`
* `> outfile`
* `>> outfile`

And returns:

* cleaned argument vector
* `RedirectionInfo` struct

### **Builtins**

Handled without forking:

* `cd`
* `pwd`
* `echo`
* `exit`

### **Executor**

`Executor::runPipeline()`:

* creates pipes
* forks for each pipeline stage
* sets up `stdin`/`stdout` with `dup2`
* applies redirections
* calls `execvp`
* parent waits on all children

---

## **Important Implementation Details**

### Move + clear pattern

```cpp
pipeline.push_back(std::move(current));
current.clear();
```

Efficiently transfers ownership of collected command words and resets `current` for reuse.

### Safe argv building

The shell builds `char*` arrays safely after `fork()`, just before `execvp()`.

### Error handling

Errors for `open`, `pipe`, `fork`, `execvp`, `dup2`, and `waitpid` produce readable messages.

---

## **Limitations**

This shell does *not* yet support:

* background jobs (`&`)
* subshells (`(...)`)
* command substitution (`$(...)`)
* variable assignment
* globbing (`*`, `?`)
* job control
* signal handling

---

## **License**

Provided for educational use.
Modify and redistribute freely.