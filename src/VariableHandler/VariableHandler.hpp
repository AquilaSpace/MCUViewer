#pragma once

#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "Variable.hpp"

class VariableHandler
{
   public:
	using VariableMap = std::map<std::string, std::shared_ptr<Variable>>;

   public:
	void addVariable(std::shared_ptr<Variable> var);

	std::shared_ptr<Variable> getVariable(const std::string& name);

	void clear();

	bool isEmpty();

	void erase(const std::string& nameToDelete);

	bool contains(const std::string& name);

	void addNewVariable(std::string newName);

	void renameVariable(const std::string& currentName, const std::string& newName);

	bool addVirtualVariable(const std::string& name, const std::string& expression);
	bool updateVirtualVariable(const std::string& name, const std::string& expression);
	void updateVirtualVariables();
	void updateVirtualVariables(double currentTime);
	bool hasCircularDependency(const std::string& varName, const std::vector<std::string>& dependencies);
	std::vector<std::string> getCircularDependencyPath(const std::string& varName, const std::vector<std::string>& dependencies);
	std::vector<std::string> getDependents(const std::string& varName);

	class iterator
	{
	   public:
		using iterator_category = std::forward_iterator_tag;
		explicit iterator(std::map<std::string, std::shared_ptr<Variable>>::iterator iter);
		iterator& operator++();
		iterator operator++(int);
		bool operator==(const iterator& other) const;
		bool operator!=(const iterator& other) const;
		std::shared_ptr<Variable> operator*();

	   private:
		std::map<std::string, std::shared_ptr<Variable>>::iterator m_iter;
	};

	iterator begin();
	iterator end();

   public:
	std::function<void(const std::string&, const std::string&)> renameCallback;

   private:
	bool detectCircularDependencyRecursive(const std::string& varName, const std::vector<std::string>& dependencies,
											std::set<std::string>& visited, std::set<std::string>& recursionStack,
											std::vector<std::string>& path);

   private:
	VariableMap variableMap;
};