#include "Variable.hpp"

#include <limits>
#include <cmath>
#include <sstream>
#include <regex>
#include <algorithm>

const char* Variable::types[8] = {"unknown",
								  "uint8_t",
								  "int8_t",
								  "uint16_t",
								  "int16_t",
								  "uint32_t",
								  "int32_t",
								  "float"};

const char* Variable::highLevelTypes[4] = {"-",
										   "signed fixed point",
										   "unsigned fixed point",
										   "virtual"};

Variable::Variable(std::string name) : name(name)
{
	name.reserve(100);
}

Variable::Variable(std::string name, Variable::Type type, double value) : name(name), type(type), value(value)
{
	name.reserve(100);
}

void Variable::setType(Type type)
{
	this->type = type;
}

Variable::Type Variable::getType() const
{
	return type;
}

std::string Variable::getTypeStr() const
{
	return std::string(types[static_cast<uint8_t>(type)]);
}

void Variable::setRawValue(uint32_t rawValue)
{
	this->rawValue = (rawValue >> shift) & mask;
}

void Variable::setValue(double val)
{
	value = val;
}

double Variable::getValue() const
{
	return value;
}

void Variable::setAddress(uint32_t addr)
{
	address = addr;
}
uint32_t Variable::getAddress() const
{
	return address;
}

std::string Variable::getName()
{
	return name;
}

void Variable::rename(const std::string& newName)
{
	name = newName;

	if (!isTrackedNameDifferent)
		trackedName = newName;
}

void Variable::setColor(float r, float g, float b, float a)
{
	color.r = r;
	color.g = g;
	color.b = b;
	color.a = a;
}

void Variable::setColor(uint32_t AaBbGgRr)
{
	using u8 = std::numeric_limits<uint8_t>;
	color.r = static_cast<float>((AaBbGgRr & 0x000000ff) / static_cast<float>(u8::max()));
	color.g = static_cast<float>(((AaBbGgRr & 0x0000ff00) >> 8) / static_cast<float>(u8::max()));
	color.b = static_cast<float>(((AaBbGgRr & 0x00ff0000) >> 16) / static_cast<float>(u8::max()));
	color.a = static_cast<float>(((AaBbGgRr & 0xff000000) >> 24) / static_cast<float>(u8::max()));
}

Variable::Color& Variable::getColor()
{
	return color;
}

uint32_t Variable::getColorU32() const
{
	using u8 = std::numeric_limits<uint8_t>;
	uint32_t a = u8::max() * color.a;
	uint32_t r = u8::max() * color.r;
	uint32_t g = u8::max() * color.g;
	uint32_t b = u8::max() * color.b;

	return static_cast<uint32_t>((a << 24) | (b << 16) | (g << 8) | r);
}

bool Variable::getIsFound() const
{
	if (shouldUpdateFromElf)
		return isFound;
	return true;
}

void Variable::setIsFound(bool found)
{
	isFound = found;
}

uint8_t Variable::getSize()
{
	switch (type)
	{
		case Type::U8:
		case Type::I8:
			return 1;
		case Type::U16:
		case Type::I16:
			return 2;
		case Type::U32:
		case Type::I32:
		case Type::F32:
			return 4;
		default:
			return 1;
	}
}

double Variable::transformToDouble()
{
	uint32_t size = getSize();

	if (HighLevelType::SIGNEDFRAC == highLevelType)
	{
		if (fractional.baseVariable != nullptr)
			fractional.base = fractional.baseVariable->getValue();

		switch (size)
		{
			case 1:
			{
				int8_t temp = rawValue & 0xff;
				value = (static_cast<double>(temp) / (1 << (fractional.fractionalBits))) * fractional.base;
				break;
			}
			case 2:
			{
				int16_t temp = rawValue & 0xffff;
				value = (static_cast<double>(temp) / (1 << (fractional.fractionalBits))) * fractional.base;
				break;
			}
			case 4:
			{
				int32_t temp = rawValue;
				value = (static_cast<double>(temp) / (1 << (fractional.fractionalBits))) * fractional.base;
				break;
			}
		}
		return value;
	}
	else if (HighLevelType::UNSIGNEDFRAC == highLevelType)
	{
		if (fractional.baseVariable != nullptr)
			fractional.base = fractional.baseVariable->getValue();

		switch (size)
		{
			case 1:
			{
				uint8_t temp = rawValue & 0xff;
				value = (static_cast<double>(temp) / (1 << fractional.fractionalBits)) * fractional.base;
				break;
			}
			case 2:
			{
				uint16_t temp = rawValue & 0xffff;
				value = (static_cast<double>(temp) / (1 << fractional.fractionalBits)) * fractional.base;
				break;
			}
			case 4:
			{
				uint32_t temp = rawValue;
				value = (static_cast<double>(temp) / (1 << fractional.fractionalBits)) * fractional.base;
				break;
			}
		}
		return value;
	}

	switch (type)
	{
		case Variable::Type::U8:
			value = static_cast<double>(*reinterpret_cast<uint8_t*>(&rawValue));
			break;
		case Variable::Type::I8:
			value = static_cast<double>(*reinterpret_cast<int8_t*>(&rawValue));
			break;
		case Variable::Type::U16:
			value = static_cast<double>(*reinterpret_cast<uint16_t*>(&rawValue));
			break;
		case Variable::Type::I16:
			value = static_cast<double>(*reinterpret_cast<int16_t*>(&rawValue));
			break;
		case Variable::Type::U32:
			value = static_cast<double>(*reinterpret_cast<uint32_t*>(&rawValue));
			break;
		case Variable::Type::I32:
			value = static_cast<double>(*reinterpret_cast<int32_t*>(&rawValue));
			break;
		case Variable::Type::F32:
			value = static_cast<double>(*reinterpret_cast<float*>(&rawValue));
			break;
		default:
			value = static_cast<double>(*reinterpret_cast<uint32_t*>(&rawValue));
			break;
	}

	return value;
}

uint32_t Variable::getRawFromDouble(double value)
{
	if (isFractional())
		return static_cast<uint32_t>((value / fractional.base) * (1 << fractional.fractionalBits));

	switch (getType())
	{
		case Variable::Type::U8:
			return static_cast<uint8_t>(value);
		case Variable::Type::I8:
			return static_cast<int8_t>(value);
		case Variable::Type::U16:
			return static_cast<uint16_t>(value);
		case Variable::Type::I16:
			return static_cast<int16_t>(value);
		case Variable::Type::U32:
			return static_cast<uint32_t>(value);
		case Variable::Type::I32:
			return static_cast<int32_t>(value);
		case Variable::Type::F32:
		{
			float valf = static_cast<float>(value);
			return *reinterpret_cast<uint32_t*>(&valf);
		}
		default:
			return 0;
	}
}

bool Variable::getShouldUpdateFromElf() const
{
	return shouldUpdateFromElf;
}

void Variable::setShouldUpdateFromElf(bool shouldUpdateFromElf)
{
	this->shouldUpdateFromElf = shouldUpdateFromElf;
}

bool Variable::getIsTrackedNameDifferent() const
{
	return isTrackedNameDifferent;
}

void Variable::setIsTrackedNameDifferent(bool isDifferent)
{
	this->isTrackedNameDifferent = isDifferent;
}

std::string Variable::getTrackedName() const
{
	return trackedName;
}

void Variable::setTrackedName(const std::string& trackedName)
{
	this->trackedName = trackedName;
}

void Variable::setShift(uint32_t shift)
{
	this->shift = shift;
}

uint32_t Variable::getShift() const
{
	return shift;
}

void Variable::setMask(uint32_t mask)
{
	this->mask = mask;
}

uint32_t Variable::getMask() const
{
	return mask;
}

void Variable::setHighLevelType(HighLevelType varType)
{
	highLevelType = varType;
}

Variable::HighLevelType Variable::getHighLevelType() const
{
	return highLevelType;
}

void Variable::setFractional(Fractional fractional)
{
	this->fractional = fractional;
}

Variable::Fractional Variable::getFractional() const
{
	return fractional;
}

bool Variable::isFractional() const
{
	return highLevelType == HighLevelType::SIGNEDFRAC || highLevelType == HighLevelType::UNSIGNEDFRAC;
}

void Variable::setIsCurrentlySampled(bool isCurrentlySampled)
{
	this->isCurrentlySampled = isCurrentlySampled;
}

bool Variable::getIsCurrentlySampled() const
{
	return isCurrentlySampled;
}

void Variable::setVirtual(Virtual virtual_)
{
	this->virtual_ = virtual_;
}

Variable::Virtual Variable::getVirtual() const
{
	return virtual_;
}

bool Variable::isVirtual() const
{
	return highLevelType == HighLevelType::VIRTUAL;
}

std::vector<std::string> Variable::extractDependencies(const std::string& expression)
{
	std::vector<std::string> dependencies;
	std::regex var_regex(R"([a-zA-Z_][a-zA-Z0-9_]*(?:::[a-zA-Z_][a-zA-Z0-9_]*)*(?:\.[a-zA-Z_][a-zA-Z0-9_]*)*)");
	std::sregex_iterator iter(expression.begin(), expression.end(), var_regex);
	std::sregex_iterator end;
	
	for (; iter != end; ++iter)
	{
		std::string match = iter->str();
		if (match != "sin" && match != "cos" && match != "tan" && match != "sqrt" && 
			match != "abs" && match != "log" && match != "exp" && match != "min" && match != "max")
		{
			if (std::find(dependencies.begin(), dependencies.end(), match) == dependencies.end())
			{
				dependencies.push_back(match);
			}
		}
	}
	
	return dependencies;
}

bool Variable::evaluateExpression(const std::function<Variable*(const std::string&)>& getVariable)
{
	if (!isVirtual())
		return false;
		
	return evaluateExpression(getVariable, 0.0); // Default time to 0 for backwards compatibility
}

bool Variable::evaluateExpression(const std::function<Variable*(const std::string&)>& getVariable, double currentTime)
{
	if (!isVirtual())
		return false;
		
	try
	{
		std::string expr = virtual_.expression;
		
		// First replace "time" with the current time value
		size_t pos = 0;
		while ((pos = expr.find("time", pos)) != std::string::npos)
		{
			if ((pos == 0 || !std::isalnum(expr[pos-1])) && 
				(pos + 4 == expr.length() || !std::isalnum(expr[pos + 4])))
			{
				expr.replace(pos, 4, std::to_string(currentTime));
				pos += std::to_string(currentTime).length();
			}
			else
			{
				pos += 4;
			}
		}
		
		// Then replace variable dependencies
		for (const auto& dep : virtual_.dependencies)
		{
			if (dep == "time") continue; // Already handled above
			
			Variable* depVar = getVariable(dep);
			if (!depVar)
			{
				virtual_.isValid = false;
				virtual_.errorMessage = "Dependency '" + dep + "' not found";
				return false;
			}
			
			// Regular variables must be found in ELF, virtual variables are always valid
			if (!depVar->isVirtual() && !depVar->getIsFound())
			{
				virtual_.isValid = false;
				virtual_.errorMessage = "Dependency '" + dep + "' not found in ELF";
				return false;
			}
			
			std::string replacement = std::to_string(depVar->getValue());
			pos = 0;
			while ((pos = expr.find(dep, pos)) != std::string::npos)
			{
				if ((pos == 0 || !std::isalnum(expr[pos-1])) && 
					(pos + dep.length() == expr.length() || !std::isalnum(expr[pos + dep.length()])))
				{
					expr.replace(pos, dep.length(), replacement);
					pos += replacement.length();
				}
				else
				{
					pos += dep.length();
				}
			}
		}
		
		double result = evaluateMathExpression(expr);
		setValue(result);
		virtual_.isValid = true;
		virtual_.errorMessage.clear();
		return true;
	}
	catch (const std::exception& e)
	{
		virtual_.isValid = false;
		virtual_.errorMessage = e.what();
		return false;
	}
}

double Variable::evaluateMathExpression(const std::string& expr)
{
	std::string cleanExpr = expr;
	cleanExpr.erase(std::remove_if(cleanExpr.begin(), cleanExpr.end(), ::isspace), cleanExpr.end());
	
	return parseExpression(cleanExpr, 0).first;
}

std::pair<double, size_t> Variable::parseExpression(const std::string& expr, size_t pos)
{
	auto [left, newPos] = parseTerm(expr, pos);
	
	while (newPos < expr.length() && (expr[newPos] == '+' || expr[newPos] == '-'))
	{
		char op = expr[newPos];
		auto [right, nextPos] = parseTerm(expr, newPos + 1);
		left = (op == '+') ? left + right : left - right;
		newPos = nextPos;
	}
	
	return {left, newPos};
}

std::pair<double, size_t> Variable::parseTerm(const std::string& expr, size_t pos)
{
	auto [left, newPos] = parseFactor(expr, pos);
	
	while (newPos < expr.length() && (expr[newPos] == '*' || expr[newPos] == '/' || expr[newPos] == '%'))
	{
		char op = expr[newPos];
		auto [right, nextPos] = parseFactor(expr, newPos + 1);
		if (op == '*')
			left *= right;
		else if (op == '/')
		{
			if (right == 0.0)
				throw std::runtime_error("Division by zero");
			left /= right;
		}
		else if (op == '%')
		{
			if (right == 0.0)
				throw std::runtime_error("Modulo by zero");
			left = std::fmod(left, right);
		}
		newPos = nextPos;
	}
	
	return {left, newPos};
}

std::pair<double, size_t> Variable::parseFactor(const std::string& expr, size_t pos)
{
	if (pos >= expr.length())
		throw std::runtime_error("Unexpected end of expression");
		
	if (expr[pos] == '(')
	{
		auto [result, newPos] = parseExpression(expr, pos + 1);
		if (newPos >= expr.length() || expr[newPos] != ')')
			throw std::runtime_error("Missing closing parenthesis");
		return {result, newPos + 1};
	}
	
	if (expr[pos] == '-')
	{
		auto [result, newPos] = parsePower(expr, pos + 1);
		return {-result, newPos};
	}
	
	if (expr[pos] == '+')
	{
		return parsePower(expr, pos + 1);
	}
	
	size_t start = pos;
	if (std::isalpha(expr[pos]))
	{
		while (pos < expr.length() && (std::isalnum(expr[pos]) || expr[pos] == '_'))
			pos++;
			
		std::string func = expr.substr(start, pos - start);
		
		if (pos < expr.length() && expr[pos] == '(')
		{
			auto [arg, newPos] = parseExpression(expr, pos + 1);
			if (newPos >= expr.length() || expr[newPos] != ')')
				throw std::runtime_error("Missing closing parenthesis for function");
				
			double result;
			if (func == "sin") result = std::sin(arg);
			else if (func == "cos") result = std::cos(arg);
			else if (func == "tan") result = std::tan(arg);
			else if (func == "sqrt") result = std::sqrt(arg);
			else if (func == "abs") result = std::abs(arg);
			else if (func == "log") result = std::log(arg);
			else if (func == "exp") result = std::exp(arg);
			else throw std::runtime_error("Unknown function: " + func);
			
			return {result, newPos + 1};
		}
		else if (func == "min" || func == "max")
		{
			if (pos >= expr.length() || expr[pos] != '(')
				throw std::runtime_error("Expected '(' after " + func);
				
			auto [arg1, pos1] = parseExpression(expr, pos + 1);
			if (pos1 >= expr.length() || expr[pos1] != ',')
				throw std::runtime_error("Expected ',' in " + func + " function");
				
			auto [arg2, pos2] = parseExpression(expr, pos1 + 1);
			if (pos2 >= expr.length() || expr[pos2] != ')')
				throw std::runtime_error("Missing closing parenthesis for " + func);
				
			double result = (func == "min") ? std::min(arg1, arg2) : std::max(arg1, arg2);
			return {result, pos2 + 1};
		}
	}
	
	return parsePower(expr, start);
}

std::pair<double, size_t> Variable::parsePower(const std::string& expr, size_t pos)
{
	auto [left, newPos] = parseNumber(expr, pos);
	
	if (newPos < expr.length() && expr[newPos] == '^')
	{
		auto [right, nextPos] = parseFactor(expr, newPos + 1);
		left = std::pow(left, right);
		newPos = nextPos;
	}
	
	return {left, newPos};
}

std::pair<double, size_t> Variable::parseNumber(const std::string& expr, size_t pos)
{
	if (pos >= expr.length())
		throw std::runtime_error("Expected number");
		
	size_t start = pos;
	bool hasDecimal = false;
	
	if (expr[pos] == '-' || expr[pos] == '+')
		pos++;
		
	while (pos < expr.length() && (std::isdigit(expr[pos]) || (expr[pos] == '.' && !hasDecimal)))
	{
		if (expr[pos] == '.')
			hasDecimal = true;
		pos++;
	}
	
	if (pos == start || (pos == start + 1 && (expr[start] == '-' || expr[start] == '+')))
		throw std::runtime_error("Invalid number format");
		
	double value = std::stod(expr.substr(start, pos - start));
	return {value, pos};
}