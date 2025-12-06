#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>

std::vector<std::string> split(const std::string &str, const char delimiter);
bool is_executable(const std::filesystem::path &p);
std::string searchExecutable(const std::string &filename);

#if _WIN32
char separator = ';';
#else
char separator = ':';
#endif

std::unordered_set<std::string> builtin = {"exit", "echo", "type"};
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
		{
			return 0;
		}
		else if (command == "echo")
		{
			for (int i = 1; i < tokens.size(); i++)
			{
				std::cout << tokens.at(i) << " ";
			}
			std::cout << std::endl;
		}
		else if (command == "type")
		{
			std::string file = tokens.at(1);
			if (builtin.find(file) != builtin.end())
			{
				std::cout << tokens.at(1) << " is a shell builtin" << std::endl;
				continue;
			}
			bool found = false;
			for (auto &dir : dirs)
			{
				if (dir.empty())
				{
					continue;
				}
				std::filesystem::path full = std::filesystem::path(dir) / file;
				if (std::filesystem::exists(full) && is_executable(full))
				{
					std::cout << file << " is " << full.string() << std::endl;
					found = true;
					break;
				}
			}
			if (!found)
				std::cout << file << ": not found" << std::endl;
		}
		else
		{
			// custom_exe_1234 alice
			// search for exec with given name
			// if found, execute
			// pass arguments to program too
			// make sure to fork it

			std::vector<char *> args; // extract arguments from tokens
			for (int i = 0; i < tokens.size(); i++)
			{
				args.push_back(const_cast<char *>(tokens.at(i).c_str()));
			}
			args.push_back(nullptr);

			bool found = false; // existence check
			for (auto &dir : dirs)
			{
				if (dir.empty())
				{
					continue;
				}
				std::filesystem::path full = std::filesystem::path(dir) / command;
				if (std::filesystem::exists(full) && is_executable(full))
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				std::cout << command << ": not found" << std::endl;
			}
			else
			{
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
					std::cout << "Program was passed " << args.size() << " args (including program name)." << std::endl;
				}
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

bool is_executable(const std::filesystem::path &p)
{
	using namespace std::filesystem;
	auto pr = status(p).permissions();
	return (pr & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
}