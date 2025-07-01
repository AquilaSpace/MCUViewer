#include <future>
#include <string>
#include <unordered_map>
#include <utility>

#include "GdbParser.hpp"
#include "Gui.hpp"
#include "GuiVariableTreeView.hpp"
#include "ImguiPlugins.hpp"
#include "Popup.hpp"
#include "VariableHandler.hpp"

class ImportVariablesWindow
{
   public:
	ImportVariablesWindow(GdbParser* parser, std::string* projectElfPath, std::string* projectConfigPath, VariableHandler* variableHandler)
		: parser(parser), projectElfPath(projectElfPath), projectConfigPath(projectConfigPath), variableHandler(variableHandler), treeView(
																																	  // Name extractor
																																	  [](const std::pair<const std::string, GdbParser::VariableData>* var)
																																	  { return var->first; },
																																	  // Address extractor
																																	  [](const std::pair<const std::string, GdbParser::VariableData>* var)
																																	  { return "0x" + GuiHelper::intToHexString(var->second.address); },
																																	  // Selection checker
																																	  [this](const std::pair<const std::string, GdbParser::VariableData>* var)
																																	  { return this->currentSelection.contains(var->first); },
																																	  // Selection toggler
																																	  [this](const std::pair<const std::string, GdbParser::VariableData>* var, bool selected)
																																	  {
																																		  if (selected)
																																			  this->currentSelection[var->first] = var->second.address;
																																		  else
																																			  this->currentSelection.erase(var->first);
																																	  },
																																	  // Item filter
																																	  [](const std::pair<const std::string, GdbParser::VariableData>* var, const std::string& filter)
																																	  {
																																		  return toLower(var->first).find(toLower(filter)) != std::string::npos;
																																	  })
	{
	}

	void draw()
	{
		static std::future<bool> refreshThread{};
		static bool wasPreviouslyOpened = false;
		static bool shouldUpdateOnOpen = false;

		if (showImportVariablesWindow)
		{
			ImGui::OpenPopup("Import Variables");

			if (!wasPreviouslyOpened)
			{
				currentSelection.clear();
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
					// Non-blocking check for result
					if (refreshThread.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
					{
						if (!refreshThread.get())
							acqusitionErrorPopup.show("Error!", "Update error. Please check the *.elf file path!", 2.0f);

						varsForDisplay = parser->getParsedData();
						treeView.rebuildTree();
					}
				}
				snprintf(buttonText, 30, "Refresh");
			}

			float buttonHeight = 25 * GuiHelper::contentScale;
			float expandButtonWidth = 120 * GuiHelper::contentScale;
			float refreshButtonWidth = ImGui::GetContentRegionAvail().x - expandButtonWidth - ImGui::GetStyle().ItemSpacing.x;

			if (ImGui::Button(buttonText, ImVec2(refreshButtonWidth, buttonHeight)) || shouldUpdateOnOpen)
			{
				stopRequested = false;
				refreshThread = std::async(std::launch::async, &GdbParser::parse, parser, GuiHelper::convertProjectPathToAbsolute(projectElfPath, projectConfigPath), std::ref(stopRequested));
				shouldUpdateOnOpen = false;
			}

			ImGui::SameLine();
			if (ImGui::Button(expandAllState ? "Collapse All" : "Expand All", ImVec2(expandButtonWidth, buttonHeight)))
			{
				if (expandAllState)
				{
					treeView.collapseAll();
				}
				else
				{
					treeView.expandAll();
				}
				expandAllState = !expandAllState;
			}

			static std::string search{};
			ImGui::Text("search ");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			if (ImGui::InputText("##search", &search, 0, NULL, NULL))
			{
				ImGui::SetKeyboardFocusHere(-1);
			}

			// Search change detection is handled automatically by the tree view

			ImGui::Spacing();

			// Convert map to vector of pointers for tree view
			std::vector<const std::pair<const std::string, GdbParser::VariableData>*> variablePtrs;
			for (const auto& var : varsForDisplay)
			{
				variablePtrs.push_back(&var);
			}

			treeView.draw(variablePtrs, search, ImGui::GetContentRegionAvail().y - 60 * GuiHelper::contentScale, false);

			std::string importBtnName{"Import ("};
			importBtnName += std::to_string(currentSelection.size()) + std::string(")");

			if (ImGui::Button(importBtnName.c_str(), ImVec2(-1, 25 * GuiHelper::contentScale)))
			{
				std::vector<std::string> namesAlreadyImported;
				for (auto& [newName, newAddress] : currentSelection)
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
	size_t lastVarCount = 0;
	std::map<std::string, GdbParser::VariableData> varsForDisplay;

	// Shared tree view component
	VariableTreeView<const std::pair<const std::string, GdbParser::VariableData>*> treeView;
	// Current selection for import
	std::unordered_map<std::string, uint32_t> currentSelection;
};