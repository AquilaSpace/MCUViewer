#include <future>
#include <string>
#include <unordered_map>
#include <utility>

#include "GdbParser.hpp"
#include "Gui.hpp"
#include "ImguiPlugins.hpp"
#include "Popup.hpp"
#include "VariableHandler.hpp"

class ImportVariablesWindow
{
   public:
	ImportVariablesWindow(GdbParser* parser, std::string* projectElfPath, std::string* projectConfigPath, VariableHandler* variableHandler) : parser(parser), projectElfPath(projectElfPath), projectConfigPath(projectConfigPath), variableHandler(variableHandler)
	{
	}

	void draw()
	{
		static std::unordered_map<std::string, uint32_t> selection;
		static std::future<bool> refreshThread{};
		static bool wasPreviouslyOpened = false;
		static bool shouldUpdateOnOpen = false;

		if (showImportVariablesWindow)
		{
			ImGui::OpenPopup("Import Variables");

			if (!wasPreviouslyOpened)
			{
				selection.clear();
				shouldUpdateOnOpen = true;
			}
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.4f, ImGui::GetMainViewport()->WorkSize.y * 0.7f), ImGuiCond_Once);

		if (ImGui::BeginPopupModal("Import Variables", &showImportVariablesWindow, 0))
		{
			char buttonText[30]{};

			if (refreshThread.valid() && refreshThread.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				snprintf(buttonText, 30, "Refresh %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);
			else
			{
				if (refreshThread.valid())
                {
                    if(!refreshThread.get())
					    acqusitionErrorPopup.show("Error!", "Update error. Please check the *.elf file path!", 2.0f);
                    
                    varsForDisplay = parser->getParsedData();
                    rebuildTree = true;
                }
				snprintf(buttonText, 30, "Refresh");
			}

			float buttonHeight = 25 * GuiHelper::contentScale;
			float expandButtonWidth = 120 * GuiHelper::contentScale;
			float refreshButtonWidth = ImGui::GetContentRegionAvail().x - expandButtonWidth - ImGui::GetStyle().ItemSpacing.x;

			if (ImGui::Button(buttonText, ImVec2(refreshButtonWidth, buttonHeight)) || shouldUpdateOnOpen)
			{
				stopRequested = false;
				refreshThread = std::async(std::launch::async, &GdbParser::parse, parser, GuiHelper::convertProjectPathToAbsolute(projectElfPath, projectConfigPath),  std::ref(stopRequested));
				shouldUpdateOnOpen = false;
			}

			ImGui::SameLine();
			if (ImGui::Button(expandAllState ? "Collapse All" : "Expand All", ImVec2(expandButtonWidth, buttonHeight)))
			{
				expandAllState = !expandAllState;
				forceStateFrame = ImGui::GetFrameCount();
			}

			static std::string search{};
			ImGui::Text("search ");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			if (ImGui::InputText("##search", &search, 0, NULL, NULL))
			{
				ImGui::SetKeyboardFocusHere(-1);
			}
			
			if (cachedSearchString != search)
			{
				cachedSearchString = search;
				rebuildTree = true;
			}

			ImGui::Spacing();
			
            drawImportVariablesTable(varsForDisplay, selection, search);
			
			std::string importBtnName{"Import ("};
			importBtnName += std::to_string(selection.size()) + std::string(")");

			if (ImGui::Button(importBtnName.c_str(), ImVec2(-1, 25 * GuiHelper::contentScale)))
			{
				std::vector<std::string> namesAlreadyImported;
				for (auto& [newName, newAddress] : selection)
				{
					if (!variableHandler->contains(newName))
					{
						variableHandler->addNewVariable(newName);
						variableHandler->getVariable(newName)->setAddress(newAddress);
					}
					else
						namesAlreadyImported.push_back(newName);
				}

				if (namesAlreadyImported.size() > 0)
				{
					std::string message = "The following variables were already imported: \n";

					for (auto& name : namesAlreadyImported)
					{
						message += "-" + name;
						if (name != namesAlreadyImported.back())
							message += ", ";
						message += "\n";
					}

					importWarningPopup.show("Warning!", message.c_str(), 2.0f);
				}
				if (!refreshThread.valid())
					shouldUpdate = true;
			}

			if (ImGui::Button("Done", ImVec2(-1, 25 * GuiHelper::contentScale)))
			{
				stopRequested = true;

				if (refreshThread.valid())
					refreshThread.wait();
				
				showImportVariablesWindow = false;
				ImGui::CloseCurrentPopup();
			}

			acqusitionErrorPopup.handle();
			importWarningPopup.handle();

			ImGui::EndPopup();
		}

		wasPreviouslyOpened = showImportVariablesWindow;
	}

	void setShowImportVariablesWindow(bool show)
	{
		showImportVariablesWindow = show;
	}

	bool shouldPerformVariableUpdate()
	{
		bool temp = shouldUpdate;
		shouldUpdate = false;
		return temp;
	}

   private:
	struct TreeNode
	{
		std::map<std::string, std::shared_ptr<TreeNode>> children;
		std::vector<const std::pair<const std::string, GdbParser::VariableData>*> variables;
	};

	void getAllVarsInSubtree(std::shared_ptr<TreeNode> node, std::vector<const std::pair<const std::string, GdbParser::VariableData>*>& vars)
	{
		vars.insert(vars.end(), node->variables.begin(), node->variables.end());
		for (auto& [name, child] : node->children)
		{
			getAllVarsInSubtree(child, vars);
		}
	}

	bool checkAllSelectedRecursive(const std::shared_ptr<TreeNode>& node, const std::unordered_map<std::string, uint32_t>& selection, bool& hasAnyVars)
    {
        if(!node->variables.empty()) hasAnyVars = true;

        for (const auto& var : node->variables) {
            if (selection.find(var->first) == selection.end()) {
                return false;
            }
        }
        for (const auto& child : node->children) {
            if (!checkAllSelectedRecursive(child.second, selection, hasAnyVars)) {
                return false;
            }
        }
        return true;
    }

	void drawVariableTreeNode(const std::string& groupName, const std::shared_ptr<TreeNode>& node, std::unordered_map<std::string, uint32_t>& selection, int level)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		// Add group selection checkbox
		bool hasAnyVars = false;
		bool allSelected = checkAllSelectedRecursive(node, selection, hasAnyVars);
		if (!hasAnyVars)
			allSelected = false;


		ImGui::PushID(groupName.c_str());
		if (ImGui::Checkbox("##groupSelect", &allSelected))
		{
			std::vector<const std::pair<const std::string, GdbParser::VariableData>*> all_vars_in_group;
			getAllVarsInSubtree(node, all_vars_in_group);
			if (allSelected)
			{
				for (auto& varData : all_vars_in_group)
				{
					selection[varData->first] = varData->second.address;
				}
			}
			else
			{
				for (auto& var : all_vars_in_group)
				{
					selection.erase(var->first);
				}
			}
		}
		ImGui::PopID();

		ImGui::TableSetColumnIndex(1);

		if (forceStateFrame == ImGui::GetFrameCount())
			ImGui::SetNextItemOpen(expandAllState);

		ImGui::Dummy(ImVec2(level * ImGui::GetStyle().IndentSpacing, 0.0f));
		ImGui::SameLine();
		const bool is_open = ImGui::CollapsingHeader(groupName.c_str());

		if (is_open)
		{
			// Draw direct variables
			for (const auto& var : node->variables)
			{
				drawVariableRow(var, selection, level + 1);
			}
			// Draw child groups
			for (auto& [childName, childNode] : node->children)
			{
				drawVariableTreeNode(childName, childNode, selection, level + 1);
			}
		}
	}

	void buildTree(const std::map<std::string, GdbParser::VariableData>& importedVars, const std::string& substring)
	{
		variableTreeRoot = std::make_shared<TreeNode>();
		const std::string lowerSubstring = toLower(substring);
		for (auto const& var : importedVars)
		{
			const auto& name = var.first;
			
			if (!substring.empty() && toLower(name).find(lowerSubstring) == std::string::npos)
				continue;

			std::string tempName = name;
			size_t pos = 0;
			while ((pos = tempName.find("::", pos)) != std::string::npos)
			{
				tempName.replace(pos, 2, ".");
			}

			std::vector<std::string> tokens;
			std::stringstream ss(tempName);
			std::string token;
			while (std::getline(ss, token, '.'))
			{
				tokens.push_back(token);
			}

			auto currentNode = variableTreeRoot;
			if (tokens.size() > 1)
			{
				for (size_t i = 0; i < tokens.size() - 1; ++i)
				{
					const std::string& groupName = tokens[i];
					if (currentNode->children.find(groupName) == currentNode->children.end())
					{
						currentNode->children[groupName] = std::make_shared<TreeNode>();
					}
					currentNode = currentNode->children[groupName];
				}
			}
			currentNode->variables.push_back(&var);
		}
		rebuildTree = false;
	}

	void drawImportVariablesTable(const std::map<std::string, GdbParser::VariableData>& importedVars, std::unordered_map<std::string, uint32_t>& selection, const std::string& substring)
	{
		if (rebuildTree || lastVarCount != importedVars.size())
		{
			buildTree(importedVars, substring);
			lastVarCount = importedVars.size();
		}

		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;

		if (ImGui::BeginTable("table_scrolly", 3, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 60 * GuiHelper::contentScale)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() + 8 * GuiHelper::contentScale);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0xAAAAAAAA").x);
			ImGui::TableHeadersRow();

			if(variableTreeRoot)
			{
				// Draw top-level groups first
				for (auto& [groupName, node] : variableTreeRoot->children)
				{
					drawVariableTreeNode(groupName, node, selection, 0);
				}

				// Then draw root-level variables
				for (const auto& var : variableTreeRoot->variables)
				{
					drawVariableRow(var, selection, 0);
				}
			}

			ImGui::EndTable();
		}
	}

	void drawVariableRow(const std::pair<const std::string, GdbParser::VariableData>* varDataPair, std::unordered_map<std::string, uint32_t>& selection, int level)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		const auto& name = varDataPair->first;
		const auto& varData = varDataPair->second;

		bool isSelected = selection.contains(name);
		if (ImGui::Checkbox(("##var_" + name).c_str(), &isSelected))
		{
			if (isSelected)
				selection[name] = varData.address;
			else
				selection.erase(name);
		}

		ImGui::TableSetColumnIndex(1);

		ImGui::Dummy(ImVec2(level * ImGui::GetStyle().IndentSpacing, 0.0f));
		ImGui::SameLine();
		ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
		if (ImGui::Selectable(name.c_str(), isSelected, selectable_flags, ImVec2(0, 12 * GuiHelper::contentScale)))
		{
			// Clicking the selectable also toggles
			if (isSelected)
				selection.erase(name);
			else
				selection[name] = varData.address;
		}
		ImGui::TableSetColumnIndex(2);
		ImGui::Text("%s", ("0x" + std::string(GuiHelper::intToHexString(varData.address))).c_str());
	}

   private:
	GdbParser* parser;
	std::string* projectElfPath;
	std::string* projectConfigPath;
	VariableHandler* variableHandler;
	Popup acqusitionErrorPopup;
	Popup importWarningPopup;
	bool showImportVariablesWindow = false;
	bool shouldUpdate = false;
	std::atomic<bool> stopRequested = false;
	bool expandAllState = false;
	int forceStateFrame = -1;
	std::shared_ptr<TreeNode> variableTreeRoot;
	bool rebuildTree = true;
	std::string cachedSearchString;
	size_t lastVarCount = 0;
    std::map<std::string, GdbParser::VariableData> varsForDisplay;
};