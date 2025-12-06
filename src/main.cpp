#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <optional>

std::vector<std::string> split(const std::string &str, const char delimiter);
bool isExecutable(const std::filesystem::path &p);
std::optional<std::filesystem::path> searchExecutable(const std::string &filename);

#if _WIN32
char separator = ';';
#else
char separator = ':';
#endif

std::unordered_set<std::string> builtin = {"exit", "echo", "type", "pwd", "cd"};
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
		std::vector<std::string> tokens = split(input, ' ');
		std::string command = tokens.at(0);
		if (command == "exit")
			return 0;
		else if (command == "cd")
		{
			std::string target;
			if (tokens.size() == 1)
			{
				char *home = std::getenv("HOME");
				target = home ? home : "/";
			}
			else
				target = tokens[1];

			if (chdir(target.c_str()) != 0)
				perror("cd");
		}
		else if (command == "echo")
		{
			for (int i = 1; i < tokens.size(); i++)
				std::cout << tokens.at(i) << " ";
			std::cout << std::endl;
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
			auto path = searchExecutable(file);
			if (path)
				std::cout << file << " is " << path->string() << std::endl;
			else
				std::cout << file << ": not found" << std::endl;
		}
		else if (command == "pwd") // argument length check needed
			std::cout << std::filesystem::current_path().string() << std::endl;
		else
		{
			std::vector<char *> args;
			for (int i = 0; i < tokens.size(); i++)
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

	return 0;
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