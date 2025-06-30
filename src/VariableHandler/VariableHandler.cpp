#include "VariableHandler.hpp"
#include <algorithm>

void VariableHandler::addVariable(std::shared_ptr<Variable> var)
{
	variableMap.emplace(var->getName(), var);
}

std::shared_ptr<Variable> VariableHandler::getVariable(const std::string& name)
{
	return variableMap.at(name);
}

void VariableHandler::clear()
{
	variableMap.clear();
}

bool VariableHandler::isEmpty()
{
	return variableMap.empty();
}

void VariableHandler::erase(const std::string& nameToDelete)
{
	variableMap.erase(nameToDelete);

	/* update tracked vars references */
	for (auto& [name, var] : variableMap)
	{
		if (var->getTrackedName() == nameToDelete)
		{
			var->setTrackedName(name);
			var->setIsTrackedNameDifferent(false);
		}

		if (var->isFractional() && var->getFractional().baseVariable != nullptr && var->getFractional().baseVariable->getName() == nameToDelete)
		{
			auto fractional = var->getFractional();
			fractional.base = 1.0;
			fractional.baseVariable = nullptr;
			var->setFractional(fractional);
		}

		if (var->isVirtual())
		{
			auto virtual_ = var->getVirtual();
			bool dependsOnDeleted = false;
			for (const auto& dep : virtual_.dependencies)
			{
				if (dep == nameToDelete)
				{
					dependsOnDeleted = true;
					break;
				}
			}
			if (dependsOnDeleted)
			{
				virtual_.isValid = false;
				virtual_.errorMessage = "Dependency '" + nameToDelete + "' was deleted";
				var->setVirtual(virtual_);
			}
		}
	}
}

bool VariableHandler::contains(const std::string& name)
{
	return variableMap.find(name) != variableMap.end();
}

void VariableHandler::addNewVariable(std::string newName)
{
	bool copy = false;
	std::string originalName = newName;

	auto incrementName = [&](std::string name) -> std::string
	{
		uint32_t num = 0;
		if (name.find("_copy_") != std::string::npos)
			name = name.substr(0, name.find("_copy_"));

		while (variableMap.find(name + "_copy_" + std::to_string(num)) != variableMap.end())
			num++;

		while (variableMap.find(name + std::to_string(num)) != variableMap.end())
			num++;

		if (name == "-new")
			return name + std::to_string(num);
		else
			return name + "_copy_" + std::to_string(num);
	};

	if (newName.empty())
		newName = incrementName("-new");
	else if (variableMap.find(newName) != variableMap.end())
	{
		copy = true;
		newName = incrementName(newName);
	}

	std::shared_ptr<Variable> newVar = std::make_shared<Variable>(newName);
	std::random_device rd{};
	std::mt19937 gen{rd()};
	std::uniform_int_distribution<uint32_t> dist{0, UINT32_MAX};
	uint32_t randomColor = dist(gen);

	if (copy)
	{
		std::shared_ptr<Variable> copiedVar = variableMap.at(originalName);
		newVar->setTrackedName(copiedVar->getTrackedName());
		newVar->setAddress(copiedVar->getAddress());
		newVar->setType(copiedVar->getType());
		newVar->setShift(copiedVar->getShift());
		newVar->setHighLevelType(copiedVar->getHighLevelType());
		newVar->setIsFound(copiedVar->getIsFound());
	}
	else
		newVar->setTrackedName(newName);

	newVar->setColor(randomColor);
	variableMap.emplace(newName, newVar);
}

void VariableHandler::renameVariable(const std::string& currentName, const std::string& newName)
{
	auto temp = variableMap.extract(currentName);
	temp.key() = std::string(newName);
	variableMap.insert(std::move(temp));

	variableMap[newName]->rename(newName);

	if (renameCallback)
		renameCallback(currentName, newName);

	/* update tracked vars references */
	for (auto& [name, var] : variableMap)
	{
		if (var->getTrackedName() == currentName)
			var->setTrackedName(newName);

		if (var->isFractional() && var->getFractional().baseVariable != nullptr && var->getFractional().baseVariable->getName() == currentName)
		{
			auto fractional = var->getFractional();
			fractional.baseVariable = variableMap[newName].get();
			var->setFractional(fractional);
		}

		if (var->isVirtual())
		{
			auto virtual_ = var->getVirtual();
			bool needsUpdate = false;
			for (size_t i = 0; i < virtual_.dependencies.size(); ++i)
			{
				if (virtual_.dependencies[i] == currentName)
				{
					virtual_.dependencies[i] = newName;
					needsUpdate = true;
				}
			}
			if (needsUpdate)
			{
				std::string oldExpr = virtual_.expression;
				size_t pos = 0;
				while ((pos = oldExpr.find(currentName, pos)) != std::string::npos)
				{
					if ((pos == 0 || !std::isalnum(oldExpr[pos-1])) && 
						(pos + currentName.length() == oldExpr.length() || !std::isalnum(oldExpr[pos + currentName.length()])))
					{
						oldExpr.replace(pos, currentName.length(), newName);
						pos += newName.length();
					}
					else
					{
						pos += currentName.length();
					}
				}
				virtual_.expression = oldExpr;
				var->setVirtual(virtual_);
			}
		}
	}
}

VariableHandler::iterator::iterator(std::map<std::string, std::shared_ptr<Variable>>::iterator iter)
	: m_iter(iter)
{
}

VariableHandler::iterator& VariableHandler::iterator::operator++()
{
	++m_iter;
	return *this;
}

VariableHandler::iterator VariableHandler::iterator::operator++(int)
{
	iterator tmp = *this;
	++(*this);
	return tmp;
}

bool VariableHandler::iterator::operator==(const iterator& other) const
{
	return m_iter == other.m_iter;
}

bool VariableHandler::iterator::operator!=(const iterator& other) const
{
	return !(*this == other);
}

std::shared_ptr<Variable> VariableHandler::iterator::operator*()
{
	return m_iter->second;
}

VariableHandler::iterator VariableHandler::begin()
{
	return iterator(variableMap.begin());
}

VariableHandler::iterator VariableHandler::end()
{
	return iterator(variableMap.end());
}

bool VariableHandler::addVirtualVariable(const std::string& name, const std::string& expression)
{
	if (contains(name))
		return false;

	auto dependencies = Variable::extractDependencies(expression);
	
	if (hasCircularDependency(name, dependencies))
		return false;

	auto virtualVar = std::make_shared<Variable>(name);
	virtualVar->setHighLevelType(Variable::HighLevelType::VIRTUAL);
	virtualVar->setShouldUpdateFromElf(false);
	virtualVar->setIsFound(true);
	
	Variable::Virtual virtual_;
	virtual_.expression = expression;
	virtual_.dependencies = dependencies;
	virtual_.isValid = true;
	virtualVar->setVirtual(virtual_);

	std::random_device rd{};
	std::mt19937 gen{rd()};
	std::uniform_int_distribution<uint32_t> dist{0, UINT32_MAX};
	virtualVar->setColor(dist(gen));

	addVariable(virtualVar);
	updateVirtualVariables();
	
	return true;
}

bool VariableHandler::updateVirtualVariable(const std::string& name, const std::string& expression)
{
	if (!contains(name))
		return false;

	auto var = getVariable(name);
	if (!var->isVirtual())
		return false;

	auto dependencies = Variable::extractDependencies(expression);
	
	if (hasCircularDependency(name, dependencies))
		return false;

	Variable::Virtual virtual_;
	virtual_.expression = expression;
	virtual_.dependencies = dependencies;
	virtual_.isValid = true;
	var->setVirtual(virtual_);

	updateVirtualVariables();
	return true;
}

void VariableHandler::updateVirtualVariables()
{
	updateVirtualVariables(0.0); // Default time
}

void VariableHandler::updateVirtualVariables(double currentTime)
{
	std::set<std::string> updated;
	bool changed = true;
	
	while (changed)
	{
		changed = false;
		for (auto& [name, var] : variableMap)
		{
			if (var->isVirtual() && updated.find(name) == updated.end())
			{
				auto virtual_ = var->getVirtual();
				bool canEvaluate = true;
				
				for (const auto& dep : virtual_.dependencies)
				{
					if (dep == "time") continue; // Time is always available
					
					if (!contains(dep))
					{
						canEvaluate = false;
						virtual_.isValid = false;
						virtual_.errorMessage = "Dependency '" + dep + "' not found";
						break;
					}
					
					auto depVar = getVariable(dep);
					if (depVar->isVirtual() && updated.find(dep) == updated.end())
					{
						canEvaluate = false;
						break;
					}
				}
				
				if (canEvaluate)
				{
					auto getVarFunc = [this](const std::string& varName) -> Variable* {
						if (contains(varName))
							return getVariable(varName).get();
						return nullptr;
					};
					
					if (var->evaluateExpression(getVarFunc, currentTime))
					{
						updated.insert(name);
						changed = true;
					}
				}
			}
		}
	}
}

bool VariableHandler::hasCircularDependency(const std::string& varName, const std::vector<std::string>& dependencies)
{
	std::set<std::string> visited;
	std::set<std::string> recursionStack;
	std::vector<std::string> path;
	
	return detectCircularDependencyRecursive(varName, dependencies, visited, recursionStack, path);
}

std::vector<std::string> VariableHandler::getCircularDependencyPath(const std::string& varName, const std::vector<std::string>& dependencies)
{
	std::set<std::string> visited;
	std::set<std::string> recursionStack;
	std::vector<std::string> path;
	
	if (detectCircularDependencyRecursive(varName, dependencies, visited, recursionStack, path))
		return path;
	
	return {};
}

std::vector<std::string> VariableHandler::getDependents(const std::string& varName)
{
	std::vector<std::string> dependents;
	
	for (const auto& [name, var] : variableMap)
	{
		if (var->isVirtual())
		{
			const auto& deps = var->getVirtual().dependencies;
			if (std::find(deps.begin(), deps.end(), varName) != deps.end())
			{
				dependents.push_back(name);
			}
		}
		else if (var->isFractional() && var->getFractional().baseVariable != nullptr &&
				 var->getFractional().baseVariable->getName() == varName)
		{
			dependents.push_back(name);
		}
	}
	
	return dependents;
}

bool VariableHandler::detectCircularDependencyRecursive(const std::string& varName, const std::vector<std::string>& dependencies,
														std::set<std::string>& visited, std::set<std::string>& recursionStack,
														std::vector<std::string>& path)
{
	visited.insert(varName);
	recursionStack.insert(varName);
	path.push_back(varName);
	
	for (const auto& dep : dependencies)
	{
		if (recursionStack.find(dep) != recursionStack.end())
		{
			path.push_back(dep);
			return true;
		}
		
		if (visited.find(dep) == visited.end() && contains(dep))
		{
			auto depVar = getVariable(dep);
			if (depVar->isVirtual())
			{
				const auto& depDependencies = depVar->getVirtual().dependencies;
				if (detectCircularDependencyRecursive(dep, depDependencies, visited, recursionStack, path))
					return true;
			}
		}
	}
	
	recursionStack.erase(varName);
	path.pop_back();
	return false;
}