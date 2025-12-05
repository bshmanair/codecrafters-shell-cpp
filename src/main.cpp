#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>

std::vector<std::string> split(const std::string &str)
{
	std::stringstream ss(str);
	std::string token;
	std::vector<std::string> tokens;
	while (std::getline(ss, token, ' '))
		tokens.push_back(token);
	return tokens;
}

int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	std::string input;

	// TODO: Uncomment the code below to pass the first stage
	do
	{
		std::cout << "$ ";
		std::getline(std::cin, input);
		std::vector<std::string> tokens = split(input);
		std::string command = tokens[0];

		std::unordered_set<std::string> builtin = {"exit", "echo", "type"};

		if (command == "exit")
			return 0;
		else if (command == "echo")
		{
			for (int i = 1; i < tokens.size(); i++)
				std::cout << tokens[i] << " ";
			std::cout << std::endl;
		}
		else if (command == "type" && builtin.find(tokens[1]) != builtin.end())
			std::cout << tokens[1] << " is a shell builtin" << std::endl;
		else
			std::cout << command << ": command not found" << std::endl;

	} while (true);

	return 0;
}
