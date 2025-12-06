#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <filesystem>

std::unordered_set<std::string> builtin = {"exit", "echo", "type"};

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

int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

#if _WIN32
	char separator = ';';
#else
	char separator = ':';
#endif

	std::string input;

	// TODO: Uncomment the code below to pass the first stage
	do
	{
		std::cout << "$ ";
		std::getline(std::cin, input);
		std::vector<std::string> tokens = split(input, ' ');
		std::string command = tokens.at(0);

		if (command == "exit")
			return 0;
		else if (command == "echo")
		{
			for (int i = 1; i < tokens.size(); i++)
				std::cout << tokens.at(i) << " ";
			std::cout << std::endl;
		}
		else if (command == "type")
		{
			std::string file = tokens.at(1);
			const char *path = std::getenv("PATH");
			std::vector<std::string> dirs = split(path, separator); // make it os-agnostic

			if (builtin.find(tokens.at(1)) != builtin.end())
				std::cout << tokens.at(1) << " is a shell builtin";
			else
			{
				for (auto &dir : dirs)
				{
					std::string pathToFile = dir + tokens.at(1);
					if (std::filesystem::exists(pathToFile))
					{
						if (is_executable(pathToFile))
							std::cout << file << " is " << pathToFile;
						continue;
					}
					else
					{
						std::cout << file << ": not found";
						break;
					}
				}
			}
			std::cout << std::endl;
		}

		else
			std::cout << command << ": command not found" << std::endl;

	} while (true);

	return 0;
}
