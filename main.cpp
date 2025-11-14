#include <fcntl.h>  // Required for open() and file control flags
#include <unistd.h> // Required for close(), read(), write()

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <fstream>

namespace fs = std::filesystem;

static std::string getenv_or_empty(const std::string &name)
{
    const char *v = std::getenv(name.c_str());
    return v ? std::string(v) : std::string{};
}

static std::string trim(const std::string &s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a]))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1]))
        --b;
    return s.substr(a, b - a);
}



enum class TokenType
{
    Word,
    Op
};

struct Token
{
    TokenType type;
    std::string text;
};

constexpr std::string_view getTokenTypeName(TokenType tokenType)
{
    switch (tokenType)
    {
    case TokenType::Word:
        return "Word";
    case TokenType::Op:
        return "Op";
    default:
        return "?????";
    }
}

std::ostream &operator<<(std::ostream &out, const Token &t)
{
    out << t.text << ' ' << getTokenTypeName(t.type);
    return out;
}

class Tokenizer
{
public:
    explicit Tokenizer(std::string_view line)
        : m_line(line), m_i(0)
    {
    }

    std::vector<Token> tokenize()
    {
        std::vector<Token> out{};
        while (skipWhitespace(), !eof())
        {
            char c{peek()};
            if (isOperatorStart(c))
            {
                out.push_back(Token{TokenType::Op, readOperator()});
            }
            else
            {
                out.push_back(Token{TokenType::Word, readWord()});
            }
        }

        return out;
    }

private:
    std::string_view m_line{};
    size_t m_i{};

    bool eof() const { return m_i >= m_line.size(); }
    char peek() const { return eof() ? '\0' : m_line[m_i]; }
    char get() { return eof() ? '\0' : m_line[m_i++]; }

    void skipWhitespace()
    {
        while (!eof() && std::isspace((unsigned char)peek()))
            ++m_i;
    }

    static bool isOperatorStart(char c)
    {
        return c == '|' || c == '>' || c == '<' || c == '&' || c == ';';
    }

    std::string readOperator()
    {
        char c = get();
        if (c == '>')
        {
            if (!eof() && peek() == '>')
            {
                get();
                return std::string{">>"};
            }
            return std::string{">"};
        }
        // single-char operators
        return std::string(1, c);
    }

    std::string readWord()
    {
        std::string out;
        while (!eof())
        {
            char c = peek();
            if (std::isspace((unsigned char)c) || isOperatorStart(c))
                break;
            if (c == '\'')
            {
                get(); // consume '
                // everything until next '
                while (!eof() && peek() != '\'')
                {
                    out.push_back(get());
                }
                if (!eof() && peek() == '\'')
                    get(); // consume closing '
                continue;
            }
            if (c == '"')
            {
                get(); // consume "
                while (!eof() && peek() != '"')
                {
                    char d = get();
                    if (d == '\\' && !eof())
                    {
                        // allow escaping inside double quotes for common cases
                        out.push_back(get());
                    }
                    else
                    {
                        out.push_back(d);
                    }
                }
                if (!eof() && peek() == '"')
                    get(); // consume closing "
                continue;
            }
            if (c == '\\')
            {
                get(); // consume backslash
                if (!eof())
                    out.push_back(get());
                continue;
            }
            out.push_back(get());
        }
        return out;
    }
};

using Words = std::vector<std::string>;

static std::vector<Words> splitIntoPipeline(const std::vector<Token> &tokens)
{
    std::vector<Words> pipeline{};
    Words current{};

    for (size_t k = 0; k < tokens.size(); ++k)
    {
        if (tokens[k].type == TokenType::Op && tokens[k].text == "|")
        {
            pipeline.push_back(std::move(current));
            current.clear();
        }
        else if (tokens[k].type == TokenType::Word)
        {
            current.push_back(tokens[k].text);
        }
        // else
        // {
        //     current.push_back(tokens[k].text);
        // }
    }
    
    if (!current.empty())
        pipeline.push_back(std::move(current));
    return pipeline;
}

struct RedirectionInfo
{
    std::string stdin_file{};
    std::string stdout_file{};
    bool stdout_append{false};
};

static std::pair<Words, RedirectionInfo> extractRedirections(const Words &cmdTokens)
{
    Words argv;
    RedirectionInfo r;
    for (size_t i = 0; i < cmdTokens.size(); ++i)
    {
        const std::string &t = cmdTokens[i];
        if (t == "<")
        {
            if (i + 1 < cmdTokens.size())
            {
                r.stdin_file = trim(cmdTokens[i + 1]);
                ++i;
            }
        }
        else if (t == ">" || t == ">>")
        {
            bool append = (t == ">>");
            if (i + 1 < cmdTokens.size())
            {
                r.stdout_file = trim(cmdTokens[i + 1]);
                r.stdout_append = append;
                ++i;
            }
        }
        else
        {
            argv.push_back(t);
        }
    }
    return {argv, r};
}

class Builtins
{
public:
    bool tryRunInParent(const Words &words)
    {
        if (words.empty())
            return false;
        const std::string &cmd{words[0]};
        if (cmd == "cd")
        {
            // cd [dir] -> change directory in parent shell
            if (words.size() == 1)
            {
                std::string home = getenv_or_empty("HOME");
                if (home.empty())
                {
                    std::cerr << "cd: HOME not set\n";
                    return true;
                }
                try
                {
                    fs::current_path(home);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "cd: " << e.what() << '\n';
                }
            }
            else
            {
                try
                {
                    fs::current_path(words[1]);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "cd: " << e.what() << '\n';
                }
            }
            return true;
        }
        if (cmd == "exit")
        {
            // we let caller handle exit; indicate it was builtin to stop elsewhere
            // but here return true so main knows it's a builtin
            return true;
        }
        if (cmd == "pwd")
        {
            std::cout << fs::current_path().string() << '\n';
            return true;
        }
        if (cmd == "echo")
        {
            for (size_t i = 1; i < words.size(); ++i)
            {
                const std::string &w = words[i];
                if (!w.empty() && w[0] == '$')
                {
                    std::cout << getenv_or_empty(w.substr(1));
                }
                else
                {
                    std::cout << w;
                }
                if (i + 1 < words.size())
                    std::cout << ' ';
            }
            std::cout << '\n';
            return true;
        }
        return false;
    }
};

class Executor
{
public:
    Executor() = default;

    // Run a pipeline: vector of commands (each command is vector<string> tokens including args).
    // Returns exit status of the last command (or -1 on failure).
    int runPipeline(const std::vector<Words> &pipeline)
    {
        if (pipeline.empty())
            return 0;
        // Special-case: if pipeline is single command and builtin that must run in parent (cd/pwd/exit/echo),
        // caller should have already handled it. We'll still allow exec of builtin in child if not handled.
        // Create N-1 pipes for N commands
        size_t n = pipeline.size();
        std::vector<int> pipefds; // flattened pairs: [r0,w0, r1,w1, ...]
        pipefds.reserve((n > 1 ? (n - 1) * 2 : 0));
        for (size_t i = 0; i + 1 < n; ++i)
        {
            int fds[2];
            if (pipe(fds) == -1)
            {
                perror("pipe");
                return -1;
            }
            pipefds.push_back(fds[0]);
            pipefds.push_back(fds[1]);
        }

        std::vector<pid_t> pids;
        pids.reserve(n);

        for (size_t idx = 0; idx < n; ++idx)
        {
            // extract redirections for this command
            auto [argvWords, rinfo] = extractRedirections(pipeline[idx]);
            if (argvWords.empty())
            {
                // nothing to run for this segment
                continue;
            }

            pid_t pid = fork();
            if (pid < 0)
            {
                perror("fork");
                // cleanup: close pipe fds
                closeAll(pipefds);
                return -1;
            }
            if (pid == 0)
            {
                // Child
                // Setup input from previous pipe if any
                if (idx > 0)
                {
                    int rfd = pipefds[(idx - 1) * 2 + 0];
                    if (dup2(rfd, STDIN_FILENO) == -1)
                    {
                        perror("dup2 stdin");
                        _exit(127);
                    }
                }
                // Setup output to next pipe if any
                if (idx + 1 < n)
                {
                    int wfd = pipefds[idx * 2 + 1];
                    if (dup2(wfd, STDOUT_FILENO) == -1)
                    {
                        perror("dup2 stdout");
                        _exit(127);
                    }
                }

                // Close all pipe fds in child (they are duplicated already if needed)
                closeAll(pipefds);

                // Handle redirections
                if (!rinfo.stdin_file.empty())
                {
                    int in_fd = open(rinfo.stdin_file.c_str(), O_RDONLY);
                    if (in_fd < 0)
                    {
                        perror(("open " + rinfo.stdin_file).c_str());
                        _exit(127);
                    }
                    if (dup2(in_fd, STDIN_FILENO) == -1)
                    {
                        perror("dup2 stdin-file");
                        _exit(127);
                    }
                    close(in_fd);
                }
                if (!rinfo.stdout_file.empty())
                {
                    int flags = O_WRONLY | O_CREAT | (rinfo.stdout_append ? O_APPEND : O_TRUNC);
                    int out_fd = open(rinfo.stdout_file.c_str(), flags, 0644);
                    if (out_fd < 0)
                    {
                        perror(("open " + rinfo.stdout_file).c_str());
                        _exit(127);
                    }
                    if (dup2(out_fd, STDOUT_FILENO) == -1)
                    {
                        perror("dup2 stdout-file");
                        _exit(127);
                    }
                    close(out_fd);
                }

                // Build argv: execvp expects char* const argv[]
                std::vector<char *> argv = buildArgv(argvWords);
                execvp(argv[0], argv.data());
                // If execvp returns, it failed:
                perror(("execvp: " + std::string(argv[0])).c_str());
                _exit(127);
            }
            else
            {
                // Parent
                pids.push_back(pid);
                // parent continues to next command
            }
        } // end for each command

        // Parent: close all pipe fds
        closeAll(pipefds);

        // Wait for all children; return status of last
        int status = 0;
        for (pid_t pid : pids)
        {
            if (waitpid(pid, &status, 0) == -1)
            {
                perror("waitpid");
            }
        }
        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        return -1;
    }

private:
    static std::vector<char *> buildArgv(const Words &words)
    {
        // Build a temporary vector<char*> from words' c_str() pointers.
        // This is safe here because we are in the child (after fork) and will exec immediately.
        std::vector<char *> argv;
        argv.reserve(words.size() + 1);
        for (const auto &w : words)
        {
            argv.push_back(const_cast<char *>(w.c_str()));
        }
        argv.push_back(nullptr);
        return argv;
    }

    static void closeAll(const std::vector<int> &fds)
    {
        for (int fd : fds)
        {
            ::close(fd);
        }
    }
};

void printPrompt()
{
    std::cout << "myshelll> " << std::flush;
}

int main()
{
    Builtins builtins;
    Executor executor;

    while (true)
    {
        printPrompt();
        std::string line{};
        if (!std::getline(std::cin, line))
        {
            std::cout << '\n';
            break;
        }

        if (trim(line).empty())
            continue;

        Tokenizer tokenizer(line);
        std::vector<Token> tokens{tokenizer.tokenize()};

        if (tokens.size() == 1 && tokens[0].type == TokenType::Word && tokens[0].text == "exit")
        {
            break;
        }

        std::vector<Words> pipeline{splitIntoPipeline(tokens)};
        if (pipeline.empty())
            continue;

        if (pipeline.size() == 1)
        {
            auto [argvWords, rinfo]{extractRedirections(pipeline.front())};

            if (argvWords.empty())
                continue;

            if (builtins.tryRunInParent(argvWords))
            {
                continue;
            }
        }

        int exit_status = executor.runPipeline(pipeline);
        (void)exit_status; // in a real shell you might save $? etc.
    }

    return 0;
}