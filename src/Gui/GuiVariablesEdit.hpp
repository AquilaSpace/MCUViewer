#ifndef _GUI_VARIABLESEDIT_HPP
#define _GUI_VARIABLESEDIT_HPP

#include <algorithm>
#include <cstring>

#include "GuiHelper.hpp"
#include "GuiSelectVariable.hpp"
#include "Popup.hpp"
#include "Variable.hpp"
#include "VariableHandler.hpp"
#include "imgui.h"

class VariableEditWindow
{
   public:
	VariableEditWindow(VariableHandler* variableHandler) : variableHandler(variableHandler)
	{
		selectVariableWindow = std::make_unique<SelectVariableWindow>(variableHandler, &selection, 1);
		selectVariableWindowBase = std::make_unique<SelectVariableWindow>(variableHandler, &selectionBase, 2);
	}

	void draw()
	{
		if (showVariableEditWindow)
			ImGui::OpenPopup("Variable Edit");

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(800 * GuiHelper::contentScale, 500 * GuiHelper::contentScale));
		if (ImGui::BeginPopupModal("Variable Edit", &showVariableEditWindow, 0))
		{
			drawVariableEditSettings();

			const float buttonHeight = 25.0f * GuiHelper::contentScale;
			ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowSize().y - buttonHeight / 2.0f - ImGui::GetFrameHeightWithSpacing()));

			if (ImGui::Button("Done", ImVec2(-1, buttonHeight)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				showVariableEditWindow = false;
				selection.clear();
				selectionBase.clear();
				ImGui::CloseCurrentPopup();
			}

			popup.handle();
			selectVariableWindow->draw();
			selectVariableWindowBase->draw();
			ImGui::EndPopup();
		}
	}

	void setVariableToEdit(std::shared_ptr<Variable> variable)
	{
		editedVariable = variable;
	}

	void setShowVariableEditWindowState(bool state)
	{
		if (showVariableEditWindow != state)
			stateChanged = true;
		showVariableEditWindow = state;
	}

	void drawVariableEditSettings()
	{
		if (editedVariable == nullptr)
			return;

		std::string name = editedVariable->getName();
		std::string address = std::string("0x") + std::string(GuiHelper::intToHexString(editedVariable->getAddress()));
		std::string size = std::to_string(editedVariable->getSize());
		std::string shift = std::to_string(editedVariable->getShift());
		std::string mask = std::string("0x") + std::string(GuiHelper::intToHexString(editedVariable->getMask()));
		bool shouldUpdateFromElf = editedVariable->getShouldUpdateFromElf();
		bool selectNameManually = editedVariable->getIsTrackedNameDifferent();

		ImGui::Dummy(ImVec2(-1, 5));
		GuiHelper::drawCenteredText("General");
		ImGui::Separator();

		/* VARIABLE TYPE SELECTION */
		int variableTypeSelection = editedVariable->isVirtual() ? 1 : 0;
		
		GuiHelper::drawTextAlignedToSize("variable type:", alignment);
		ImGui::SameLine();
		if (ImGui::RadioButton("Real", &variableTypeSelection, 0))
		{
			if (editedVariable->isVirtual())
			{
				editedVariable->setHighLevelType(Variable::HighLevelType::NONE);
				editedVariable->setShouldUpdateFromElf(true);
			}
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Virtual", &variableTypeSelection, 1))
		{
			if (!editedVariable->isVirtual())
			{
				editedVariable->setHighLevelType(Variable::HighLevelType::VIRTUAL);
				editedVariable->setShouldUpdateFromElf(false);
				Variable::Virtual virtual_;
				virtual_.expression = "";
				virtual_.isValid = false;
				virtual_.errorMessage = "No expression defined";
				editedVariable->setVirtual(virtual_);
			}
		}
		ImGui::SameLine();
		ImGui::HelpMarker("Real variables are sampled from memory. Virtual variables are calculated from mathematical expressions.");

		if (stateChanged)
		{
			ImGui::SetKeyboardFocusHere(0);
			stateChanged = false;
		}
		/* NAME */
		GuiHelper::drawTextAlignedToSize("name:", alignment);
		ImGui::SameLine();
		ImGui::InputText("##name", &name, ImGuiInputTextFlags_None, NULL, NULL);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (!variableHandler->contains(name))
			{
				variableHandler->renameVariable(editedVariable->getName(), name);
			}
			else
				popup.show("Error!", "Variable already exists!", 1.5f);
		}

		ImGui::SameLine();
		ImGui::HelpMarker("Name has to be unique - it can be different from tracked variable name if you specify it manually below.");

		/* VIRTUAL VARIABLE EXPRESSION BUILDER */
		if (editedVariable->isVirtual())
		{
			Variable::Virtual virtual_ = editedVariable->getVirtual();
			
			// Builder mode selection
			static bool useDropdownBuilder = true;
			GuiHelper::drawTextAlignedToSize("builder mode:", alignment);
			ImGui::SameLine();
			if (ImGui::RadioButton("Dropdown", useDropdownBuilder)) {
				useDropdownBuilder = true;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Custom", !useDropdownBuilder)) {
				useDropdownBuilder = false;
			}
			
			// Get list of available variables
			std::vector<std::string> availableVariables;
			for (auto var : *variableHandler) {
				if (var->getName() != editedVariable->getName()) { // Don't include self
					availableVariables.push_back(var->getName());
				}
			}
			
			if (useDropdownBuilder)
			{
				// DROPDOWN BUILDER
				static struct DropdownBuilder {
					enum class Operation { 
						Add, Subtract, Multiply, Divide, Power, Modulo,
						Sin, Cos, Tan, Sqrt, Abs, Log, Exp,
						Min, Max 
					};
					enum class OperandType { Variable, Constant, Time };
					
					Operation operation = Operation::Add;
					
					// Operand A
					OperandType operandAType = OperandType::Variable;
					std::string operandAVariable = "";
					double operandAConstant = 0.0;
					int operandAVarIndex = -1; // -1 means no selection
					
					// Operand B
					OperandType operandBType = OperandType::Variable;
					std::string operandBVariable = "";
					double operandBConstant = 0.0;
					int operandBVarIndex = -1; // -1 means no selection
					
					bool hasChanged = false; // Track if any dropdown has changed
					bool isInitialized = false; // Track if builder has been initialized for current variable
					
					const char* getOperationString() const {
						switch (operation) {
							case Operation::Add: return "+";
							case Operation::Subtract: return "-";
							case Operation::Multiply: return "*";
							case Operation::Divide: return "/";
							case Operation::Power: return "^";
							case Operation::Modulo: return "%";
							case Operation::Sin: return "sin";
							case Operation::Cos: return "cos";
							case Operation::Tan: return "tan";
							case Operation::Sqrt: return "sqrt";
							case Operation::Abs: return "abs";
							case Operation::Log: return "log";
							case Operation::Exp: return "exp";
							case Operation::Min: return "min";
							case Operation::Max: return "max";
							default: return "+";
						}
					}
					
					bool isUnaryOperation() const {
						return operation == Operation::Sin || operation == Operation::Cos || 
							   operation == Operation::Tan || operation == Operation::Sqrt ||
							   operation == Operation::Abs || operation == Operation::Log ||
							   operation == Operation::Exp;
					}
					
					bool isBinaryFunction() const {
						return operation == Operation::Min || operation == Operation::Max;
					}
				} builder;
				
				// Initialize builder from existing virtual variable expression if not already done
				if (!builder.isInitialized && editedVariable->isVirtual() && !virtual_.expression.empty()) {
					loadBuilderFromExpression(builder, virtual_.expression, availableVariables);
					builder.isInitialized = true;
				} else if (!editedVariable->isVirtual() || virtual_.expression.empty()) {
					// Reset builder for new variables or when switching to virtual
					if (builder.isInitialized) {
						builder = DropdownBuilder{}; // Reset to defaults
						builder.isInitialized = false;
					}
				}
				
				// Operand A
				GuiHelper::drawTextAlignedToSize("operand A:", alignment);
				ImGui::SameLine();
				ImGui::PushItemWidth(80 * GuiHelper::contentScale);
				int operandATypeInt = static_cast<int>(builder.operandAType);
				const char* operandTypes[] = {"Variable", "Constant", "Time"};
				if (ImGui::Combo("##operandAType", &operandATypeInt, operandTypes, 3)) {
					builder.operandAType = static_cast<DropdownBuilder::OperandType>(operandATypeInt);
					builder.hasChanged = true;
				}
				ImGui::PopItemWidth();
				
				ImGui::SameLine();
				if (builder.operandAType == DropdownBuilder::OperandType::Variable) {
					if (!availableVariables.empty()) {
						// Create dropdown with "Select..." only if no variable is selected
						std::vector<const char*> varNames;
						int displayIndex;
						
						if (builder.operandAVarIndex == -1) {
							// No selection - show "Select..." as first option
							varNames.push_back("Select...");
							for (const auto& var : availableVariables) {
								varNames.push_back(var.c_str());
							}
							displayIndex = 0; // "Select..." is selected
						} else {
							// Variable is selected - show variables without "Select..."
							for (const auto& var : availableVariables) {
								varNames.push_back(var.c_str());
							}
							displayIndex = builder.operandAVarIndex;
						}
						
						if (ImGui::Combo("##operandAVar", &displayIndex, varNames.data(), static_cast<int>(varNames.size()))) {
							if (builder.operandAVarIndex == -1 && displayIndex == 0) {
								// "Select..." was clicked - do nothing
							} else if (builder.operandAVarIndex == -1) {
								// Selection made from "Select..." dropdown
								builder.operandAVarIndex = displayIndex - 1;
								builder.operandAVariable = availableVariables[builder.operandAVarIndex];
								builder.hasChanged = true;
							} else {
								// Selection changed from existing variable dropdown
								builder.operandAVarIndex = displayIndex;
								builder.operandAVariable = availableVariables[builder.operandAVarIndex];
								builder.hasChanged = true;
							}
						}
					} else {
						ImGui::Text("No variables available");
					}
				} else if (builder.operandAType == DropdownBuilder::OperandType::Constant) {
					ImGui::PushItemWidth(100 * GuiHelper::contentScale);
					if (ImGui::InputDouble("##operandAConst", &builder.operandAConstant)) {
						builder.hasChanged = true;
					}
					ImGui::PopItemWidth();
				} else { // Time
					ImGui::Text("time");
				}
				
				// Operation selection
				const char* operations[] = {"+", "-", "*", "/", "^", "%", "sin", "cos", "tan", "sqrt", "abs", "log", "exp", "min", "max"};
				int currentOperation = static_cast<int>(builder.operation);
				
				GuiHelper::drawTextAlignedToSize("operation:", alignment);
				ImGui::SameLine();
				ImGui::PushItemWidth(80 * GuiHelper::contentScale);
				if (ImGui::Combo("##operation", &currentOperation, operations, IM_ARRAYSIZE(operations))) {
					builder.operation = static_cast<DropdownBuilder::Operation>(currentOperation);
					builder.hasChanged = true;
				}
				ImGui::PopItemWidth();
				
				// Operand B (disabled for unary operations)
				bool needsOperandB = !builder.isUnaryOperation();
				
				ImGui::BeginDisabled(!needsOperandB);
				GuiHelper::drawTextAlignedToSize("operand B:", alignment);
				ImGui::SameLine();
				ImGui::PushItemWidth(80 * GuiHelper::contentScale);
				int operandBTypeInt = static_cast<int>(builder.operandBType);
				if (ImGui::Combo("##operandBType", &operandBTypeInt, operandTypes, 3)) {
					builder.operandBType = static_cast<DropdownBuilder::OperandType>(operandBTypeInt);
					builder.hasChanged = true;
				}
				ImGui::PopItemWidth();
				
				ImGui::SameLine();
				if (builder.operandBType == DropdownBuilder::OperandType::Variable) {
					if (!availableVariables.empty()) {
						// Create dropdown with "Select..." only if no variable is selected
						std::vector<const char*> varNames;
						int displayIndex;
						
						if (builder.operandBVarIndex == -1) {
							// No selection - show "Select..." as first option
							varNames.push_back("Select...");
							for (const auto& var : availableVariables) {
								varNames.push_back(var.c_str());
							}
							displayIndex = 0; // "Select..." is selected
						} else {
							// Variable is selected - show variables without "Select..."
							for (const auto& var : availableVariables) {
								varNames.push_back(var.c_str());
							}
							displayIndex = builder.operandBVarIndex;
						}
						
						if (ImGui::Combo("##operandBVar", &displayIndex, varNames.data(), static_cast<int>(varNames.size()))) {
							if (builder.operandBVarIndex == -1 && displayIndex == 0) {
								// "Select..." was clicked - do nothing
							} else if (builder.operandBVarIndex == -1) {
								// Selection made from "Select..." dropdown
								builder.operandBVarIndex = displayIndex - 1;
								builder.operandBVariable = availableVariables[builder.operandBVarIndex];
								builder.hasChanged = true;
							} else {
								// Selection changed from existing variable dropdown
								builder.operandBVarIndex = displayIndex;
								builder.operandBVariable = availableVariables[builder.operandBVarIndex];
								builder.hasChanged = true;
							}
						}
					} else {
						ImGui::Text("No variables available");
					}
				} else if (builder.operandBType == DropdownBuilder::OperandType::Constant) {
					ImGui::PushItemWidth(100 * GuiHelper::contentScale);
					if (ImGui::InputDouble("##operandBConst", &builder.operandBConstant)) {
						builder.hasChanged = true;
					}
					ImGui::PopItemWidth();
				} else { // Time
					ImGui::Text("time");
				}
				ImGui::EndDisabled();
				
				// Auto-generate expression when anything changes
				if (builder.hasChanged) {
					builder.hasChanged = false;
					
					std::string expression;
					
					// Build operand A string
					std::string operandA;
					if (builder.operandAType == DropdownBuilder::OperandType::Variable && !builder.operandAVariable.empty()) {
						operandA = builder.operandAVariable;
					} else if (builder.operandAType == DropdownBuilder::OperandType::Constant) {
						operandA = std::to_string(builder.operandAConstant);
					} else if (builder.operandAType == DropdownBuilder::OperandType::Time) {
						operandA = "time";
					}
					
					// Build operand B string
					std::string operandB;
					if (needsOperandB) {
						if (builder.operandBType == DropdownBuilder::OperandType::Variable && !builder.operandBVariable.empty()) {
							operandB = builder.operandBVariable;
						} else if (builder.operandBType == DropdownBuilder::OperandType::Constant) {
							operandB = std::to_string(builder.operandBConstant);
						} else if (builder.operandBType == DropdownBuilder::OperandType::Time) {
							operandB = "time";
						}
					}
					
					// Build expression based on operation type
					if (builder.isUnaryOperation()) {
						// Unary functions: sin(operandA)
						if (!operandA.empty()) {
							expression = std::string(builder.getOperationString()) + "(" + operandA + ")";
						}
					} else if (builder.isBinaryFunction()) {
						// Binary functions: min(operandA, operandB)
						if (!operandA.empty() && !operandB.empty()) {
							expression = std::string(builder.getOperationString()) + "(" + operandA + ", " + operandB + ")";
						}
					} else {
						// Binary operations: operandA + operandB
						if (!operandA.empty() && !operandB.empty()) {
							expression = operandA + " " + builder.getOperationString() + " " + operandB;
						}
					}
					
					if (!expression.empty()) {
						updateVirtualExpression(expression, virtual_, editedVariable);
					}
				}
			}
			else
			{
				// CUSTOM BUILDER
				GuiHelper::drawTextAlignedToSize("custom expression:", alignment);
				ImGui::SameLine();
				ImGui::HelpMarker("Write custom mathematical expressions.\n\nSupported operators: + - * / ^ %\nFunctions: sin() cos() tan() sqrt() abs() log() exp() min() max()\nUse parentheses for grouping: (a + b) * c\n\nExamples:\n  var1 + var2 * 3.14\n  sqrt(var1^2 + var2^2)\n  min(var1, max(var2, 0))");
				
				// Current expression editor
				static char customExpressionBuffer[1024] = "";
				static bool customExpressionInitialized = false;
				static std::string lastVirtualExpression = "";
				
				// Only update buffer if the virtual expression changed externally (not from user input)
				if (!customExpressionInitialized || (virtual_.expression != lastVirtualExpression && virtual_.expression != std::string(customExpressionBuffer))) {
					strncpy(customExpressionBuffer, virtual_.expression.c_str(), sizeof(customExpressionBuffer) - 1);
					customExpressionBuffer[sizeof(customExpressionBuffer) - 1] = '\0';
					customExpressionInitialized = true;
				}
				lastVirtualExpression = virtual_.expression;
				
				GuiHelper::drawTextAlignedToSize("expression:", alignment);
				ImGui::SameLine();
				ImGui::InputTextMultiline("##customExpression", customExpressionBuffer, sizeof(customExpressionBuffer), ImVec2(-1, 60 * GuiHelper::contentScale), ImGuiInputTextFlags_CtrlEnterForNewLine);
				
				// Apply button
				if (ImGui::Button("Apply Expression")) {
					std::string expression(customExpressionBuffer);
					updateVirtualExpression(expression, virtual_, editedVariable);
					lastVirtualExpression = expression; // Update to prevent override
				}
			}
			
			// Display current expression
			GuiHelper::drawTextAlignedToSize("current expression:", alignment);
			ImGui::SameLine();
			ImGui::Text("%s", virtual_.expression.empty() ? "none" : virtual_.expression.c_str());
			
			if (!virtual_.isValid) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				GuiHelper::drawTextAlignedToSize("error:", alignment);
				ImGui::SameLine();
				ImGui::TextWrapped("%s", virtual_.errorMessage.c_str());
				ImGui::PopStyleColor();
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
				GuiHelper::drawTextAlignedToSize("dependencies:", alignment);
				ImGui::SameLine();
				std::string depStr;
				for (size_t i = 0; i < virtual_.dependencies.size(); ++i) {
					if (i > 0) depStr += ", ";
					depStr += virtual_.dependencies[i];
				}
				if (depStr.empty()) depStr = "none";
				ImGui::TextWrapped("%s", depStr.c_str());
				ImGui::PopStyleColor();
			}
			
			GuiHelper::drawTextAlignedToSize("current value:", alignment);
			ImGui::SameLine();
			ImGui::Text("%.6g", editedVariable->getValue());
		}

		/* REAL VARIABLE CONFIGURATION */
		if (!editedVariable->isVirtual())
		{
			/* TRACKED NAME */
			ImGui::BeginDisabled(!shouldUpdateFromElf);

		GuiHelper::drawTextAlignedToSize("specify tracked name:", alignment);
		ImGui::SameLine();
		if (ImGui::Checkbox("##selectNameManually", &selectNameManually))
			editedVariable->setIsTrackedNameDifferent(selectNameManually);

		ImGui::SameLine();
		ImGui::HelpMarker("Check if you'd like to specify a different variable name that will be sampled.");

		ImGui::EndDisabled();

		ImGui::BeginDisabled(!selectNameManually);

		GuiHelper::drawTextAlignedToSize("tracked variable:", alignment);
		ImGui::SameLine();

		std::string trackedVarName = selection.empty() ? editedVariable->getTrackedName() : *selection.begin();

		if (ImGui::InputText("##trackedVarName", &trackedVarName, ImGuiInputTextFlags_None, NULL, NULL) || editedVariable->getTrackedName() != trackedVarName)
		{
			if (variableHandler->contains(trackedVarName))
			{
				editedVariable->setTrackedName(trackedVarName);
				editedVariable->setAddress(variableHandler->getVariable(trackedVarName)->getAddress());
				editedVariable->setType(variableHandler->getVariable(trackedVarName)->getType());
				selection.clear();
			}
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (!variableHandler->contains(trackedVarName))
				popup.show("Error!", "Tracked variable doesn't exist!", 1.5f);
		}

		ImGui::SameLine();
		if (ImGui::Button("select...", ImVec2(65 * GuiHelper::contentScale, 19 * GuiHelper::contentScale)))
			selectVariableWindow->setShowState(true);

		ImGui::SameLine();
		ImGui::HelpMarker("Select or type an imported variable name from the *.elf file.");

		ImGui::EndDisabled();

		/* SHOULD UPDATE FROM ELF */
		GuiHelper::drawTextAlignedToSize("update from *.elf:", alignment);
		ImGui::SameLine();
		if (ImGui::Checkbox("##shouldUpdateFromElf", &shouldUpdateFromElf))
		{
			editedVariable->setShouldUpdateFromElf(shouldUpdateFromElf);
			editedVariable->setIsTrackedNameDifferent(false);
		}
		ImGui::SameLine();
		ImGui::HelpMarker("Check if the variable address and size should be automatically updated from *.elf file.");

		/* ADDRESS */
		ImGui::BeginDisabled(shouldUpdateFromElf);

		GuiHelper::drawTextAlignedToSize("address:", alignment);
		ImGui::SameLine();
		if (ImGui::InputText("##address", &address, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase, NULL, NULL))
		{
			uint32_t addressDec = GuiHelper::hexStringToDecimal(address);
			editedVariable->setAddress(addressDec);
		}
		/* BASE TYPE*/
		GuiHelper::drawTextAlignedToSize("base type:", alignment);
		ImGui::SameLine();
		int32_t type = static_cast<int32_t>(editedVariable->getType());

		if (ImGui::Combo("##varType", &type, Variable::types, IM_ARRAYSIZE(Variable::types)))
		{
			editedVariable->setType(static_cast<Variable::Type>(type));
		}

		ImGui::EndDisabled();

		/* POSTPROCESSING */
		ImGui::Dummy(ImVec2(-1, 5));
		GuiHelper::drawCenteredText("Postprocessing");
		ImGui::SameLine();
		ImGui::HelpMarker("Applies transformation [(value >> shift) & mask] after sampling and before interpretation.");
		ImGui::Separator();

		GuiHelper::drawTextAlignedToSize("shift right:", alignment);
		ImGui::SameLine();
		if (ImGui::InputText("##shift", &shift, ImGuiInputTextFlags_CharsDecimal, NULL, NULL))
		{
			uint32_t shiftDec = GuiHelper::convertStringToNumber<uint32_t>(shift);
			shiftDec = std::clamp<uint32_t>(shiftDec, 0, (editedVariable->getSize() * 8) - 1);
			editedVariable->setShift(shiftDec);
		}

		GuiHelper::drawTextAlignedToSize("mask:", alignment);
		ImGui::SameLine();
		if (ImGui::InputText("##mask", &mask, ImGuiInputTextFlags_None, NULL, NULL))
		{
			uint32_t maskDec = GuiHelper::hexStringToDecimal(mask);
			editedVariable->setMask(maskDec);
		}

		/* INTERPRETATION */
		ImGui::Dummy(ImVec2(-1, 5));
		GuiHelper::drawCenteredText("Interpretation");
		ImGui::SameLine();
		ImGui::HelpMarker("Interpret integer numbers as fixed point variables.");
		ImGui::Separator();

		GuiHelper::drawTextAlignedToSize("type:", alignment);
		ImGui::SameLine();
		int32_t highLeveltype = static_cast<int32_t>(editedVariable->getHighLevelType());

		const char* interpretationTypes[] = {"-", "signed fixed point", "unsigned fixed point"};
		if (ImGui::Combo("##varHighLevelType", &highLeveltype, interpretationTypes, 3))
		{
			editedVariable->setHighLevelType(static_cast<Variable::HighLevelType>(highLeveltype));
		}

		if (editedVariable->isFractional())
		{
			Variable::Fractional fractional = editedVariable->getFractional();

			std::string fractionalBits = std::to_string(fractional.fractionalBits);
			bool shouldUpdate = false;

			GuiHelper::drawTextAlignedToSize("fractional bits:", alignment);
			ImGui::SameLine();
			if (ImGui::InputText("##fractional", &fractionalBits, ImGuiInputTextFlags_CharsDecimal, NULL, NULL))
				shouldUpdate = true;

			/* BASE */

			std::string base = "";

			if (!selectionBase.empty())
			{
				base = *selectionBase.begin();
				shouldUpdate = true;
			}
			else if (fractional.baseVariable != nullptr)
				base = fractional.baseVariable->getName();
			else
				base = std::to_string(fractional.base);

			GuiHelper::drawTextAlignedToSize("base:", alignment);
			ImGui::SameLine();

			if (ImGui::InputText("##base", &base, ImGuiInputTextFlags_None, NULL, NULL))
				shouldUpdate = true;

			if (fractional.baseVariable != nullptr && base != fractional.baseVariable->getName())
				shouldUpdate = true;

			ImGui::SameLine();
			ImGui::PushID("fractional");
			if (ImGui::Button("select...", ImVec2(65 * GuiHelper::contentScale, 19 * GuiHelper::contentScale)))
				selectVariableWindowBase->setShowState(true);
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::HelpMarker("Either type a new base (multiplier), or select a variable defined as base from the list of imported variables.");

			if (shouldUpdate)
			{
				fractional.fractionalBits = std::clamp<uint32_t>(GuiHelper::convertStringToNumber<uint32_t>(fractionalBits), 0, (editedVariable->getSize() * 8) - 1);

				if (variableHandler->contains(base))
					fractional.baseVariable = variableHandler->getVariable(base).get();
				else
				{
					fractional.base = GuiHelper::convertStringToNumber<double>(base);
					fractional.baseVariable = nullptr;
				}
				selectionBase.clear();
				// TODO check if not zero or negative and set to 1 in such cases and show a popup
				editedVariable->setFractional(fractional);
			}
		}
		} // End real variable configuration
	}

   private:
	// Forward declaration for DropdownBuilder struct
	struct DropdownBuilder;
	
	template<typename BuilderType>
	void loadBuilderFromExpression(BuilderType& builder, const std::string& expression, const std::vector<std::string>& availableVariables)
	{
		// Simple expression parsing to populate the dropdown builder
		// This handles basic cases like "var1 + var2", "sin(var1)", "min(var1, var2)", etc.
		
		std::string trimmedExpr = expression;
		// Remove spaces for easier parsing
		trimmedExpr.erase(std::remove_if(trimmedExpr.begin(), trimmedExpr.end(), ::isspace), trimmedExpr.end());
		
		// Check for binary functions: min(a,b) or max(a,b)
		if (trimmedExpr.find("min(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(13); // Min
			size_t commaPos = trimmedExpr.find(',');
			size_t closePos = trimmedExpr.find_last_of(')');
			if (commaPos != std::string::npos && closePos != std::string::npos) {
				std::string operandA = trimmedExpr.substr(4, commaPos - 4);
				std::string operandB = trimmedExpr.substr(commaPos + 1, closePos - commaPos - 1);
				parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
				parseOperand(operandB, builder.operandBType, builder.operandBVariable, builder.operandBConstant, builder.operandBVarIndex, availableVariables);
			}
		}
		else if (trimmedExpr.find("max(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(14); // Max
			size_t commaPos = trimmedExpr.find(',');
			size_t closePos = trimmedExpr.find_last_of(')');
			if (commaPos != std::string::npos && closePos != std::string::npos) {
				std::string operandA = trimmedExpr.substr(4, commaPos - 4);
				std::string operandB = trimmedExpr.substr(commaPos + 1, closePos - commaPos - 1);
				parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
				parseOperand(operandB, builder.operandBType, builder.operandBVariable, builder.operandBConstant, builder.operandBVarIndex, availableVariables);
			}
		}
		// Check for unary functions: sin(a), cos(a), etc.
		else if (trimmedExpr.find("sin(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(6); // Sin
			std::string operandA = trimmedExpr.substr(4, trimmedExpr.find_last_of(')') - 4);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		else if (trimmedExpr.find("cos(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(7); // Cos
			std::string operandA = trimmedExpr.substr(4, trimmedExpr.find_last_of(')') - 4);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		else if (trimmedExpr.find("tan(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(8); // Tan
			std::string operandA = trimmedExpr.substr(4, trimmedExpr.find_last_of(')') - 4);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		else if (trimmedExpr.find("sqrt(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(9); // Sqrt
			std::string operandA = trimmedExpr.substr(5, trimmedExpr.find_last_of(')') - 5);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		else if (trimmedExpr.find("abs(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(10); // Abs
			std::string operandA = trimmedExpr.substr(4, trimmedExpr.find_last_of(')') - 4);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		else if (trimmedExpr.find("log(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(11); // Log
			std::string operandA = trimmedExpr.substr(4, trimmedExpr.find_last_of(')') - 4);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		else if (trimmedExpr.find("exp(") == 0) {
			builder.operation = static_cast<decltype(builder.operation)>(12); // Exp
			std::string operandA = trimmedExpr.substr(4, trimmedExpr.find_last_of(')') - 4);
			parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
		}
		// Check for binary operations: a + b, a - b, etc.
		else {
			// Find the operator
			size_t opPos = std::string::npos;
			auto op = static_cast<decltype(builder.operation)>(0); // Add by default
			
			if ((opPos = trimmedExpr.find('+')) != std::string::npos) {
				op = static_cast<decltype(builder.operation)>(0); // Add
			} else if ((opPos = trimmedExpr.find('-')) != std::string::npos) {
				op = static_cast<decltype(builder.operation)>(1); // Subtract
			} else if ((opPos = trimmedExpr.find('*')) != std::string::npos) {
				op = static_cast<decltype(builder.operation)>(2); // Multiply
			} else if ((opPos = trimmedExpr.find('/')) != std::string::npos) {
				op = static_cast<decltype(builder.operation)>(3); // Divide
			} else if ((opPos = trimmedExpr.find('^')) != std::string::npos) {
				op = static_cast<decltype(builder.operation)>(4); // Power
			} else if ((opPos = trimmedExpr.find('%')) != std::string::npos) {
				op = static_cast<decltype(builder.operation)>(5); // Modulo
			}
			
			if (opPos != std::string::npos) {
				builder.operation = op;
				std::string operandA = trimmedExpr.substr(0, opPos);
				std::string operandB = trimmedExpr.substr(opPos + 1);
				parseOperand(operandA, builder.operandAType, builder.operandAVariable, builder.operandAConstant, builder.operandAVarIndex, availableVariables);
				parseOperand(operandB, builder.operandBType, builder.operandBVariable, builder.operandBConstant, builder.operandBVarIndex, availableVariables);
			}
		}
	}
	
	template<typename OperandTypeEnum>
	void parseOperand(const std::string& operand, OperandTypeEnum& operandType, std::string& operandVariable, double& operandConstant, int& operandVarIndex, const std::vector<std::string>& availableVariables)
	{
		if (operand == "time") {
			operandType = static_cast<OperandTypeEnum>(2); // Time
		} else {
			// Check if it's a number
			try {
				operandConstant = std::stod(operand);
				operandType = static_cast<OperandTypeEnum>(1); // Constant
			} catch (...) {
				// It's a variable
				operandType = static_cast<OperandTypeEnum>(0); // Variable
				operandVariable = operand;
				// Find the index in available variables
				for (size_t i = 0; i < availableVariables.size(); ++i) {
					if (availableVariables[i] == operand) {
						operandVarIndex = static_cast<int>(i);
						break;
					}
				}
			}
		}
	}

	void updateVirtualExpression(const std::string& expression, Variable::Virtual& virtual_, std::shared_ptr<Variable> variable)
	{
		if (expression.empty()) return;
		
		auto dependencies = Variable::extractDependencies(expression);
		
		if (variableHandler->hasCircularDependency(variable->getName(), dependencies)) {
			auto path = variableHandler->getCircularDependencyPath(variable->getName(), dependencies);
			std::string pathStr;
			for (size_t i = 0; i < path.size(); ++i) {
				if (i > 0) pathStr += " -> ";
				pathStr += path[i];
			}
			popup.show("Circular Dependency!", ("Circular dependency detected: " + pathStr).c_str(), 3.0f);
		} else {
			virtual_.expression = expression;
			virtual_.dependencies = dependencies;
			virtual_.isValid = true;
			virtual_.errorMessage.clear();
			variable->setVirtual(virtual_);
			
			auto getVarFunc = [this](const std::string& varName) -> Variable* {
				if (variableHandler->contains(varName))
					return variableHandler->getVariable(varName).get();
				return nullptr;
			};
			
			try {
				variable->evaluateExpression(getVarFunc);
			} catch (const std::exception& e) {
				virtual_.isValid = false;
				virtual_.errorMessage = e.what();
				variable->setVirtual(virtual_);
			}
		}
	}

	//Text alignemnt in front of the input fields
	static constexpr size_t alignment = 22;

	bool showVariableEditWindow = false;
	bool stateChanged = false;

	std::shared_ptr<Variable> editedVariable = nullptr;

	VariableHandler* variableHandler;

	Popup popup;

	std::set<std::string> selection{};
	std::unique_ptr<SelectVariableWindow> selectVariableWindow;

	std::set<std::string> selectionBase{};
	std::unique_ptr<SelectVariableWindow> selectVariableWindowBase;
};

#endif