#include <iostream>
#include <fcntl.h>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <optional>
#include <algorithm>
#include <cctype>
#include <cstdlib>

struct Redirection
{
	bool redirectStdout = false;
	bool redirectStderr = false;

	bool appendStdout = false;
	bool appendStderr = false;

	std::string stdoutFile;
	std::string stderrFile;
};

#if _WIN32
char separator = ';';
#else
char separator = ':';
#endif

// --- Forward decls ---
void runBuiltin(const std::vector<std::string> &tokens);
std::vector<std::string> split(const std::string &str, char delimiter);
bool isExecutable(const std::filesystem::path &p);
std::optional<std::filesystem::path> searchExecutable(const std::string &filename);
std::vector<std::string> tokenize(const std::string &input);

Redirection parseRedirection(std::vector<std::string> &tokens);

int applyStdoutRedirection(const Redirection &r);
int applyStderrRedirection(const Redirection &r);
void restoreStdout(int saved);
void restoreStderr(int saved);

std::vector<char *> makeArgv(const std::vector<std::string> &tokens);

// Builtins
const std::unordered_set<std::string> builtin = {"exit", "echo", "type", "pwd", "cd"};

const char *pathEnv = std::getenv("PATH");
std::vector<std::string> dirs = split(pathEnv ? pathEnv : "", separator);

// ---------------- main ----------------
int main()
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	std::string input;
	while (true)
	{
		std::cout << "$ ";
		if (!std::getline(std::cin, input))
			break;

		// Tokenize first
		std::vector<std::string> tokens = tokenize(input);
		if (tokens.empty())
			continue;

		// Detect pipe BEFORE stripping redirections (so we can parse per-side)
		auto pipeIt = std::find(tokens.begin(), tokens.end(), "|");
		bool hasPipe = (pipeIt != tokens.end());

		// ---------------- Pipeline path (two external commands) ----------------
		if (hasPipe)
		{
			std::vector<std::string> leftTokens(tokens.begin(), pipeIt);
			std::vector<std::string> rightTokens(pipeIt + 1, tokens.end());

			if (leftTokens.empty() || rightTokens.empty())
			{
				std::cerr << "invalid pipeline" << std::endl;
				continue;
			}

			// Parse redirections per side
			Redirection leftRedir = parseRedirection(leftTokens);
			Redirection rightRedir = parseRedirection(rightTokens);

			// Only external commands for pipelines in this stage
			if (leftTokens.empty() || rightTokens.empty())
			{
				std::cerr << "invalid pipeline" << std::endl;
				continue;
			}

			// Resolve executables (optional but gives nicer errors)
			if (!searchExecutable(leftTokens[0]))
			{
				std::cout << leftTokens[0] << ": not found" << std::endl;
				continue;
			}
			if (!searchExecutable(rightTokens[0]))
			{
				std::cout << rightTokens[0] << ": not found" << std::endl;
				continue;
			}

			int pipefd[2];
			if (pipe(pipefd) == -1)
			{
				perror("pipe");
				continue;
			}

			pid_t leftPid = fork();
			if (leftPid == -1)
			{
				perror("fork");
				close(pipefd[0]);
				close(pipefd[1]);
				continue;
			}

			if (leftPid == 0)
			{
				// LEFT CHILD: stdout -> pipe (unless stdout redirected), stderr redirection allowed
				if (!leftRedir.redirectStdout)
				{
					dup2(pipefd[1], STDOUT_FILENO);
				}
				close(pipefd[0]);
				close(pipefd[1]);

				// Apply stderr redirection if requested
				if (leftRedir.redirectStderr)
				{
					int flags = O_WRONLY | O_CREAT | (leftRedir.appendStderr ? O_APPEND : O_TRUNC);
					int fd = open(leftRedir.stderrFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDERR_FILENO);
					close(fd);
				}

				// Apply stdout redirection if requested (overrides pipe)
				if (leftRedir.redirectStdout)
				{
					int flags = O_WRONLY | O_CREAT | (leftRedir.appendStdout ? O_APPEND : O_TRUNC);
					int fd = open(leftRedir.stdoutFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDOUT_FILENO);
					close(fd);
				}

				if (builtin.count(leftTokens[0]))
				{
					runBuiltin(leftTokens);
					std::exit(0);
				}
				else
				{
					auto argv = makeArgv(leftTokens);
					execvp(argv[0], argv.data());
					perror("execvp");
					std::exit(1);
				}

				perror("execvp");
				std::exit(1);
			}

			pid_t rightPid = fork();
			if (rightPid == -1)
			{
				perror("fork");
				close(pipefd[0]);
				close(pipefd[1]);
				waitpid(leftPid, nullptr, 0);
				continue;
			}

			if (rightPid == 0)
			{
				// RIGHT CHILD: stdin <- pipe, stdout normal (unless redirected), stderr redirection allowed
				dup2(pipefd[0], STDIN_FILENO);
				close(pipefd[1]);
				close(pipefd[0]);

				// Apply stderr redirection if requested
				if (rightRedir.redirectStderr)
				{
					int flags = O_WRONLY | O_CREAT | (rightRedir.appendStderr ? O_APPEND : O_TRUNC);
					int fd = open(rightRedir.stderrFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDERR_FILENO);
					close(fd);
				}

				// Apply stdout redirection if requested
				if (rightRedir.redirectStdout)
				{
					int flags = O_WRONLY | O_CREAT | (rightRedir.appendStdout ? O_APPEND : O_TRUNC);
					int fd = open(rightRedir.stdoutFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDOUT_FILENO);
					close(fd);
				}

				if (builtin.count(rightTokens[0]))
				{
					runBuiltin(rightTokens);
					std::exit(0);
				}
				else
				{
					auto argv = makeArgv(rightTokens);
					execvp(argv[0], argv.data());
					perror("execvp");
					std::exit(1);
				}

				perror("execvp");
				std::exit(1);
			}

			// PARENT
			close(pipefd[0]);
			close(pipefd[1]);

			waitpid(leftPid, nullptr, 0);
			waitpid(rightPid, nullptr, 0);
			continue;
		}

		// ---------------- Non-pipeline path ----------------
		Redirection redir = parseRedirection(tokens);
		if (tokens.empty())
			continue;

		std::string command = tokens.at(0);

		if (command == "exit")
		{
			return 0;
		}
		else if (command == "cd")
		{
			const char *targetFolder;
			if (tokens.size() == 1 || (tokens.size() == 2 && tokens.at(1) == "~"))
				targetFolder = std::getenv("HOME");
			else
				targetFolder = tokens.at(1).c_str();

			if (!targetFolder || !std::filesystem::exists(targetFolder) || !std::filesystem::is_directory(targetFolder))
			{
				std::cout << (targetFolder ? targetFolder : "") << ": No such file or directory" << std::endl;
				continue;
			}

			if (chdir(targetFolder) == -1)
			{
				std::cerr << "chdir() error: " << targetFolder << std::endl;
				continue;
			}
		}
		else if (command == "echo")
		{
			int savedOut = applyStdoutRedirection(redir);
			int savedErr = applyStderrRedirection(redir);

			for (size_t i = 1; i < tokens.size(); i++)
			{
				std::cout << tokens.at(i);
				if (i + 1 < tokens.size())
					std::cout << " ";
			}
			std::cout << std::endl;

			restoreStdout(savedOut);
			restoreStderr(savedErr);
		}
		else if (command == "type")
		{
			int savedOut = applyStdoutRedirection(redir);
			int savedErr = applyStderrRedirection(redir);

			if (tokens.size() < 2)
			{
				restoreStdout(savedOut);
				restoreStderr(savedErr);
				continue;
			}

			std::string file = tokens.at(1);

			if (builtin.find(file) != builtin.end())
			{
				std::cout << file << " is a shell builtin" << std::endl;
			}
			else
			{
				auto filePath = searchExecutable(file);
				if (filePath)
					std::cout << file << " is " << filePath->string() << std::endl;
				else
					std::cout << file << ": not found" << std::endl;
			}

			restoreStdout(savedOut);
			restoreStderr(savedErr);
		}
		else if (command == "pwd")
		{
			int savedOut = applyStdoutRedirection(redir);
			int savedErr = applyStderrRedirection(redir);

			std::cout << std::filesystem::current_path().string() << std::endl;

			restoreStdout(savedOut);
			restoreStderr(savedErr);
		}
		else
		{
			// External command
			auto execPath = searchExecutable(command);
			if (!execPath)
			{
				std::cout << command << ": not found" << std::endl;
				continue;
			}

			pid_t processID = fork();
			if (processID == -1)
			{
				perror("fork");
				continue;
			}

			if (processID == 0)
			{
				// stdout redirection
				if (redir.redirectStdout)
				{
					int flags = O_WRONLY | O_CREAT | (redir.appendStdout ? O_APPEND : O_TRUNC);
					int fd = open(redir.stdoutFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDOUT_FILENO);
					close(fd);
				}

				// stderr redirection
				if (redir.redirectStderr)
				{
					int flags = O_WRONLY | O_CREAT | (redir.appendStderr ? O_APPEND : O_TRUNC);
					int fd = open(redir.stderrFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDERR_FILENO);
					close(fd);
				}

				auto argv = makeArgv(tokens);
				execvp(argv[0], argv.data());
				perror("execvp");
				std::exit(1);
			}
			else
			{
				waitpid(processID, nullptr, 0);
			}
		}
	}

	return 0;
}

// ---------------- Helpers ----------------

std::vector<std::string> split(const std::string &str, char delimiter)
{
	std::stringstream ss(str);
	std::string token;
	std::vector<std::string> tokens;
	while (std::getline(ss, token, delimiter))
		tokens.push_back(token);
	return tokens;
}

bool isExecutable(const std::filesystem::path &p)
{
	using namespace std::filesystem;
	auto pr = status(p).permissions();
	return (pr & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
}

std::optional<std::filesystem::path> searchExecutable(const std::string &filename)
{
	for (const auto &dir : dirs)
	{
		if (dir.empty())
			continue;
		std::filesystem::path full = std::filesystem::path(dir) / filename;
		if (std::filesystem::exists(full) && isExecutable(full))
			return full;
	}
	return std::nullopt;
}

std::vector<std::string> tokenize(const std::string &input)
{
	std::vector<std::string> tokens;
	std::string current;

	enum State
	{
		NORMAL,
		IN_SINGLE,
		IN_DOUBLE
	};

	State state = NORMAL;

	for (size_t i = 0; i < input.size(); ++i)
	{
		char c = input[i];

		switch (state)
		{
		case NORMAL:
			if (c == '\\')
			{
				if (i + 1 < input.size())
					current.push_back(input[++i]);
			}
			else if (std::isspace(static_cast<unsigned char>(c)))
			{
				if (!current.empty())
				{
					tokens.push_back(current);
					current.clear();
				}
			}
			else if (c == '\'')
			{
				state = IN_SINGLE;
			}
			else if (c == '"')
			{
				state = IN_DOUBLE;
			}
			else
			{
				current.push_back(c);
			}
			break;

		case IN_SINGLE:
			if (c == '\'')
				state = NORMAL;
			else
				current.push_back(c);
			break;

		case IN_DOUBLE:
			if (c == '\\')
			{
				if (i + 1 < input.size())
				{
					char next = input[i + 1];
					if (next == '"' || next == '\\')
					{
						current.push_back(next);
						++i;
					}
					else
					{
						current.push_back('\\');
					}
				}
				else
				{
					current.push_back('\\');
				}
			}
			else if (c == '"')
			{
				state = NORMAL;
			}
			else
			{
				current.push_back(c);
			}
			break;
		}
	}

	if (!current.empty())
		tokens.push_back(current);

	return tokens;
}

Redirection parseRedirection(std::vector<std::string> &tokens)
{
	Redirection r;

	for (size_t i = 0; i < tokens.size();)
	{
		if ((tokens[i] == ">" || tokens[i] == "1>") && i + 1 < tokens.size())
		{
			r.redirectStdout = true;
			r.appendStdout = false;
			r.stdoutFile = tokens[i + 1];
			tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
		}
		else if ((tokens[i] == ">>" || tokens[i] == "1>>") && i + 1 < tokens.size())
		{
			r.redirectStdout = true;
			r.appendStdout = true;
			r.stdoutFile = tokens[i + 1];
			tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
		}
		else if (tokens[i] == "2>" && i + 1 < tokens.size())
		{
			r.redirectStderr = true;
			r.appendStderr = false;
			r.stderrFile = tokens[i + 1];
			tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
		}
		else if (tokens[i] == "2>>" && i + 1 < tokens.size())
		{
			r.redirectStderr = true;
			r.appendStderr = true;
			r.stderrFile = tokens[i + 1];
			tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
		}
		else
		{
			++i;
		}
	}

	return r;
}

int applyStdoutRedirection(const Redirection &r)
{
	if (!r.redirectStdout)
		return -1;

	int flags = O_WRONLY | O_CREAT | (r.appendStdout ? O_APPEND : O_TRUNC);
	int fd = open(r.stdoutFile.c_str(), flags, 0644);
	if (fd == -1)
	{
		perror("open");
		return -1;
	}

	int saved = dup(STDOUT_FILENO);
	dup2(fd, STDOUT_FILENO);
	close(fd);
	return saved;
}

int applyStderrRedirection(const Redirection &r)
{
	if (!r.redirectStderr)
		return -1;

	int flags = O_WRONLY | O_CREAT | (r.appendStderr ? O_APPEND : O_TRUNC);
	int fd = open(r.stderrFile.c_str(), flags, 0644);
	if (fd == -1)
	{
		perror("open");
		return -1;
	}

	int saved = dup(STDERR_FILENO);
	dup2(fd, STDERR_FILENO);
	close(fd);
	return saved;
}

void restoreStdout(int saved)
{
	if (saved != -1)
	{
		dup2(saved, STDOUT_FILENO);
		close(saved);
	}
}

void restoreStderr(int saved)
{
	if (saved != -1)
	{
		dup2(saved, STDERR_FILENO);
		close(saved);
	}
}

std::vector<char *> makeArgv(const std::vector<std::string> &tokens)
{
	std::vector<char *> argv;
	argv.reserve(tokens.size() + 1);
	for (const auto &t : tokens)
		argv.push_back(const_cast<char *>(t.c_str()));
	argv.push_back(nullptr);
	return argv;
}

void runBuiltin(const std::vector<std::string> &tokens)
{
	const std::string &cmd = tokens[0];

	if (cmd == "echo")
	{
		for (size_t i = 1; i < tokens.size(); ++i)
		{
			std::cout << tokens[i];
			if (i + 1 < tokens.size())
				std::cout << " ";
		}
		std::cout << std::endl;
	}
	else if (cmd == "type")
	{
		if (tokens.size() < 2)
			return;

		const std::string &arg = tokens[1];
		if (builtin.count(arg))
		{
			std::cout << arg << " is a shell builtin" << std::endl;
		}
		else
		{
			auto path = searchExecutable(arg);
			if (path)
				std::cout << arg << " is " << path->string() << std::endl;
			else
				std::cout << arg << ": not found" << std::endl;
		}
	}
	else if (cmd == "pwd")
	{
		std::cout << std::filesystem::current_path().string() << std::endl;
	}
}
