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

struct Redirection
{
	bool redirectStdout = false;
	bool redirectStderr = false;

	bool appendStdout = false;
	bool appendStderr = false;

	std::string stdoutFile;
	std::string stderrFile;
};
void restoreStderr(int saved);
int applyStderrRedirection(const Redirection &r);
std::vector<std::string> split(const std::string &str, const char delimiter);
bool isExecutable(const std::filesystem::path &p);
std::optional<std::filesystem::path> searchExecutable(const std::string &filename);
std::vector<std::string> tokenize(const std::string &input);
Redirection parseRedirection(std::vector<std::string> &tokens);
int applyStdoutRedirection(const Redirection &r);
void restoreStdout(int saved);

#if _WIN32
char separator = ';';
#else
char separator = ':';
#endif

const std::unordered_set<std::string> builtin = {"exit", "echo", "type", "pwd", "cd"};
const char *path = std::getenv("PATH");
std::vector<std::string> dirs = split(path, separator);

int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	std::string input;
	do
	{
		std::cout << "$ ";
		std::getline(std::cin, input);
		std::vector<std::string> tokens = tokenize(input);
		Redirection redir = parseRedirection(tokens);
		std::string command = tokens.at(0);
		if (command == "exit")
		{
			return 0;
		}
		else if (command == "cd")
		{
			// get target folder. if user doesn't input a 2nd argument, assume HOME
			const char *targetFolder;
			if (tokens.size() == 1 || (tokens.size() == 2 && (tokens.at(1) == "~")))
				targetFolder = std::getenv("HOME");
			else
				targetFolder = tokens.at(1).c_str();
			if (!std::filesystem::exists(targetFolder) || !std::filesystem::is_directory(targetFolder))
			{
				std::cout << targetFolder << ": No such file or directory" << std::endl;
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
				std::cout << tokens.at(i) << " ";
			std::cout << std::endl;
			restoreStdout(savedOut);
			restoreStderr(savedErr);
		}
		else if (command == "type")
		{
			std::string file = tokens.at(1);
			// See if it's a builtin
			if (builtin.find(file) != builtin.end())
			{
				std::cout << tokens.at(1) << " is a shell builtin" << std::endl;
				continue;
			}

			// Check if it's an executable
			auto filePath = searchExecutable(file);
			if (filePath)
				std::cout << file << " is " << filePath->string() << std::endl;
			else
				std::cout << file << ": not found" << std::endl;
		}
		else if (command == "pwd") // argument length check needed
		{
			std::cout << std::filesystem::current_path().string() << std::endl;
		}
		else
		{
			std::vector<char *> args;
			for (size_t i = 0; i < tokens.size(); i++)
				args.push_back(const_cast<char *>(tokens.at(i).c_str()));
			args.push_back(nullptr);

			auto execPath = searchExecutable(command);
			if (!execPath)
			{
				std::cout << command << ": not found" << std::endl;
				continue;
			}
			pid_t processID = fork();
			if (processID == 0)
			{
				if (redir.redirectStdout)
				{
					int flags = O_WRONLY | O_CREAT;
					if (redir.appendStdout)
						flags |= O_APPEND;
					else
						flags |= O_TRUNC;
					int fd = open(redir.stdoutFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}
					dup2(fd, STDOUT_FILENO);
					close(fd);
				}
				if (redir.redirectStderr)
				{
					int flags = O_WRONLY | O_CREAT;
					if (redir.appendStderr)
						flags |= O_APPEND;
					else
						flags |= O_TRUNC;

					int fd = open(redir.stderrFile.c_str(), flags, 0644);
					if (fd == -1)
					{
						perror("open");
						std::exit(1);
					}

					dup2(fd, STDERR_FILENO);
					close(fd);
				}

				execvp(args[0], args.data());
				std::cerr << "execvp failed" << std::endl;
				std::exit(1);
			}
			else
			{
				int status;
				waitpid(processID, &status, 0);
			}
		}
	} while (true);

	return 1; // program must not get here
}

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

/*
Tokenizer algorithm:
===================

There would be 3 states: normal state, single quote state and double quote state.

In normal state,
- if single quote detected, switch to single quote state
- if double quote, double quote state
- append any other character LITERALLY

In single quote state,
- if a single quote is detected, go back to normal state
- append any other character LITERALLY

In double quote state,
- if double quote detected, go back to normal state
- append any other character LITERALLY

*/
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
				// Escape next character literally
				if (i + 1 < input.size())
				{
					current.push_back(input[++i]);
				}
			}
			else if (std::isspace(c))
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
			// Everything is literal
			if (c == '\'')
			{
				state = NORMAL;
			}
			else
			{
				current.push_back(c);
			}
			break;

		case IN_DOUBLE:
			if (c == '\\')
			{
				// Only \" and \\ are escaped in this stage
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
						// Backslash is literal for all other characters
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
	{
		tokens.push_back(current);
	}

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

	int flags = O_WRONLY | O_CREAT;
	if (r.appendStdout)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC;

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

void restoreStdout(int saved)
{
	if (saved != -1)
	{
		dup2(saved, STDOUT_FILENO);
		close(saved);
	}
}

int applyStderrRedirection(const Redirection &r)
{
	if (!r.redirectStderr)
		return -1;

	int flags = O_WRONLY | O_CREAT;
	if (r.appendStderr)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC;

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

void restoreStderr(int saved)
{
	if (saved != -1)
	{
		dup2(saved, STDERR_FILENO);
		close(saved);
	}
}
