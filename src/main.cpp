#include <iostream>
#include <string>
#include <sstream>
#include <vector>

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

	// TODO: Uncomment the code below to pass the first stage
	do
	{
		std::cout << "$ ";

		std::string input;
		std::getline(std::cin, input);
		std::vector<std::string> tokens = split(input);
		std::string command = tokens[0];

		if (command == "exit")
			return 0;
		else if (command == "echo")
		{
			for (int i = 1; i < tokens.size(); i++)
				std::cout << tokens[i] << " ";
			std::cout << std::endl;
		}
		else
			std::cout << command << ": command not found" << std::endl;

	} while (true);

	return 0;
}
