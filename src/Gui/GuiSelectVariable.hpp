#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../commons.hpp"
#include "GuiHelper.hpp"
#include "GuiVariableTreeView.hpp"
#include "ImguiPlugins.hpp"
#include "Variable.hpp"
#include "VariableHandler.hpp"

class SelectVariableWindow
{
   public:
	SelectVariableWindow(VariableHandler* variableHandler, std::set<std::string>* selection, int id)
		: variableHandler(variableHandler), selection(selection),
		  treeView(
			// Name extractor
			[](const std::shared_ptr<Variable>& var) { return var->getName(); },
			// Address extractor  
			[](const std::shared_ptr<Variable>& var) { return "0x" + GuiHelper::intToHexString(var->getAddress()); },
			// Selection checker
			[this](const std::shared_ptr<Variable>& var) { return this->selection->contains(var->getName()); },
			// Selection toggler
			[this](const std::shared_ptr<Variable>& var, bool selected) {
				if (selected) this->selection->insert(var->getName());
				else this->selection->erase(var->getName());
			},
			// Item filter
			[](const std::shared_ptr<Variable>& var, const std::string& filter) {
				return toLower(var->getName()).find(toLower(filter)) != std::string::npos;
			}
		  )
	{
		popupName = "Select Variables##" + std::to_string(id);	// Unique name
	}

	void draw()
	{
		if (show)
			ImGui::OpenPopup(popupName.c_str());

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(500 * GuiHelper::contentScale, -1), ImGuiCond_Once);

		if (ImGui::BeginPopupModal(popupName.c_str(), &show, 0))
		{
			const char* searchLabel = "search ";
			uint32_t dupa = ImGui::GetItemRectSize().x - 25 * GuiHelper::contentScale;
			uint32_t dupa2 = ImGui::CalcTextSize(searchLabel).x;
			ImGui::PushItemWidth(dupa - dupa2);
			ImGui::Dummy(ImVec2(-1, 5));
			static std::string search{};
			ImGui::Text("%s", searchLabel);
			ImGui::SameLine();

			static bool focusSet = false;
			if (!focusSet)
			{
				ImGui::SetKeyboardFocusHere();
				focusSet = true;
			}

			ImGui::InputText("##search", &search, 0, NULL, NULL);
			ImGui::PopItemWidth();
			ImGui::Dummy(ImVec2(-1, 5));

			// Convert VariableHandler to vector for tree view
			std::vector<std::shared_ptr<Variable>> variables;
			for (auto var : *variableHandler) {
				variables.push_back(var);
			}
			
			treeView.draw(variables, search, 400 * GuiHelper::contentScale);

			std::string importBtnName{"Select ("};
			importBtnName += std::to_string(selection->size()) + std::string(")");

			if (ImGui::Button("Done", ImVec2(-1, 25 * GuiHelper::contentScale)))
			{
				show = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void setShowState(bool state)
	{
		show = state;
	}

   private:
	VariableHandler* variableHandler;
	std::set<std::string>* selection;
	std::string popupName;
	bool show = false;
	
	// Shared tree view component
	VariableTreeView<std::shared_ptr<Variable>> treeView;
};